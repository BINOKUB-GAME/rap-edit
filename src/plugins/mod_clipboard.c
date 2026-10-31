/* ==============================================================================
 * SCRIPT      : src/plugins/mod_clipboard.c
 * RÉVISION    : Rev 1.7 (Universal CLI Copy/Cut/Paste & Targeted Macro Engine)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Prise en charge complète du workflow en 2 étapes (:10c puis :15w/:15pp),
 *               des commandes unitaires (:c, :x), de plages (:1:10c, :1:10x), du collage
 *               au curseur (:pp) et des macros combinées (:1:10c:15w, :10c:15w).
 *               Renvoi strict de -127 pour préserver mod_delete (:5d, :1:10d). 0-heap BSS.
 * FONCTIONS   :
 *   1. rap_clip_copy_range (Copie de lignes complètes vers le presse-papiers BSS)
 *   2. rap_clip_cut_range (Coupe de lignes avec instantané d'annulation)
 *   3. rap_clip_paste_line (Collage de lignes avec instantané d'annulation)
 *   4. rap_clip_copy_bytes (Copie de sélection précise d'octets)
 *   5. rap_clip_cut_bytes (Coupe de sélection précise avec instantané d'annulation)
 *   6. rap_clip_paste_bytes (Collage précis avec repositionnement du curseur)
 *   7. rap_cmd_clip_paste_pp (Point d'entrée CLI pour la commande :pp au curseur)
 *   8. rap_cmd_clip_info (Point d'entrée CLI pour la commande :clip d'inspection)
 *   9. rap_clip_parse_line_macro (Parseur universel multi-syntaxe : 2 étapes et macros)
 *  10. rap_plugin_mod_clipboard_init (Enregistrement persistant des greffons en BSS)
 * ============================================================================== */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#include "rap_edit_plugin.h"
#include "rap_edit_config.h"
#include "rap_edit_undo.h"
#include "rap_edit_ops.h"

extern int rap_core_index_lines(rap_context_t *ctx);

/* 1. Copie d'une plage de lignes */
int rap_clip_copy_range(rap_context_t *ctx, uint32_t start_line, uint32_t end_line) {
    if (!ctx || start_line == 0 || end_line == 0 || start_line > end_line) {
        return -1;
    }
    if (start_line > ctx->total_lines) return -1;
    if (end_line > ctx->total_lines) end_line = ctx->total_lines;

    uint32_t start_idx = start_line - 1;
    uint32_t end_idx = end_line - 1;

    uint32_t start_offset = ctx->lines[start_idx].offset;
    uint32_t end_offset;
    bool append_nl = false;

    if (end_idx + 1 < ctx->total_lines) {
        end_offset = ctx->lines[end_idx + 1].offset;
    } else {
        end_offset = ctx->buffer_len;
        if (ctx->buffer_len > 0 && ctx->raw_buffer[ctx->buffer_len - 1] != '\n') {
            append_nl = true;
        }
    }

    uint32_t total_bytes = end_offset - start_offset;
    uint32_t needed = total_bytes + (append_nl ? 1 : 0);

    if (needed >= RAP_CLIPBOARD_CAPACITY) {
        return -2;
    }

    memcpy(ctx->clipboard.buffer, &ctx->raw_buffer[start_offset], total_bytes);
    if (append_nl) {
        ctx->clipboard.buffer[total_bytes] = '\n';
    }
    ctx->clipboard.len = needed;
    ctx->clipboard.is_line_block = true;

    return 0;
}

/* 2. Coupe d'une plage de lignes avec snapshot Undo */
int rap_clip_cut_range(rap_context_t *ctx, uint32_t start_line, uint32_t end_line) {
    if (!ctx || ctx->is_readonly) return -1;

    if (rap_clip_copy_range(ctx, start_line, end_line) != 0) {
        return -2;
    }

    rap_undo_snapshot(ctx);

    uint32_t start_idx = start_line - 1;
    uint32_t end_idx = (end_line > ctx->total_lines) ? ctx->total_lines - 1 : end_line - 1;

    uint32_t start_offset = ctx->lines[start_idx].offset;
    uint32_t end_offset;

    if (end_idx + 1 < ctx->total_lines) {
        end_offset = ctx->lines[end_idx + 1].offset;
    } else {
        end_offset = ctx->buffer_len;
    }

    uint32_t bytes_to_remove = end_offset - start_offset;
    size_t trailing_bytes = ctx->buffer_len - end_offset;

    if (trailing_bytes > 0) {
        memmove(&ctx->raw_buffer[start_offset],
                &ctx->raw_buffer[end_offset],
                trailing_bytes);
    }

    ctx->buffer_len -= bytes_to_remove;
    ctx->is_dirty = true;
    rap_core_index_lines(ctx);

    if (ctx->total_lines == 0) {
        ctx->cursor_row = 0;
        ctx->cursor_col = 0;
    } else if (start_idx < ctx->total_lines) {
        ctx->cursor_row = start_idx;
        ctx->cursor_col = 0;
    } else {
        ctx->cursor_row = ctx->total_lines - 1;
        ctx->cursor_col = 0;
    }

    return 0;
}

/* 3. Collage de lignes avec snapshot Undo */
int rap_clip_paste_line(rap_context_t *ctx, uint32_t target_line) {
    if (!ctx || ctx->is_readonly || ctx->clipboard.len == 0) return -1;
    if (target_line == 0) target_line = 1;

    rap_undo_snapshot(ctx);

    if (ctx->buffer_len > 0 && ctx->raw_buffer[ctx->buffer_len - 1] != '\n') {
        if (ctx->buffer_len + 1 >= RAP_STATIC_BUFFER_SIZE) return -2;
        ctx->raw_buffer[ctx->buffer_len++] = '\n';
        rap_core_index_lines(ctx);
    }

    if (target_line - 1 > ctx->total_lines) {
        uint32_t needed_empty_lines = (target_line - 1) - ctx->total_lines;
        if (ctx->buffer_len + needed_empty_lines + ctx->clipboard.len >= RAP_STATIC_BUFFER_SIZE) {
            return -2;
        }
        for (uint32_t m = 0; m < needed_empty_lines; ++m) {
            ctx->raw_buffer[ctx->buffer_len++] = '\n';
        }
        rap_core_index_lines(ctx);
    }

    if (ctx->buffer_len + ctx->clipboard.len >= RAP_STATIC_BUFFER_SIZE) return -2;

    uint32_t insert_offset;
    if (target_line > ctx->total_lines) {
        insert_offset = ctx->buffer_len;
    } else {
        insert_offset = ctx->lines[target_line - 1].offset;
    }

    size_t trailing_bytes = ctx->buffer_len - insert_offset;
    if (trailing_bytes > 0) {
        memmove(&ctx->raw_buffer[insert_offset + ctx->clipboard.len],
                &ctx->raw_buffer[insert_offset],
                trailing_bytes);
    }

    memcpy(&ctx->raw_buffer[insert_offset], ctx->clipboard.buffer, ctx->clipboard.len);
    ctx->buffer_len += ctx->clipboard.len;
    ctx->is_dirty = true;
    rap_core_index_lines(ctx);

    ctx->cursor_row = target_line - 1;
    ctx->cursor_col = 0;

    return 0;
}

/* 4. Copie de sélection précise */
int rap_clip_copy_bytes(rap_context_t *ctx, uint32_t start_off, uint32_t end_off) {
    if (!ctx || start_off >= end_off || end_off > ctx->buffer_len) return -1;

    uint32_t len = end_off - start_off;
    if (len >= RAP_CLIPBOARD_CAPACITY) len = RAP_CLIPBOARD_CAPACITY - 1;

    memcpy(ctx->clipboard.buffer, &ctx->raw_buffer[start_off], len);
    ctx->clipboard.len = len;
    ctx->clipboard.is_line_block = false;

    return 0;
}

/* 5. Coupe de sélection précise avec snapshot Undo */
int rap_clip_cut_bytes(rap_context_t *ctx, uint32_t start_off, uint32_t end_off) {
    if (!ctx || ctx->is_readonly) return -1;
    if (rap_clip_copy_bytes(ctx, start_off, end_off) != 0) return -2;

    rap_undo_snapshot(ctx);

    uint32_t len = end_off - start_off;
    size_t trailing_bytes = ctx->buffer_len - end_off;

    if (trailing_bytes > 0) {
        memmove(&ctx->raw_buffer[start_off], &ctx->raw_buffer[end_off], trailing_bytes);
    }

    ctx->buffer_len -= len;
    ctx->is_dirty = true;
    rap_core_index_lines(ctx);

    return 0;
}

/* 6. Collage précis avec repositionnement du curseur */
int rap_clip_paste_bytes(rap_context_t *ctx, uint32_t insert_off) {
    if (!ctx || ctx->is_readonly || ctx->clipboard.len == 0) return -1;
    if (ctx->buffer_len + ctx->clipboard.len >= RAP_STATIC_BUFFER_SIZE) return -2;

    rap_undo_snapshot(ctx);

    if (insert_off > ctx->buffer_len) insert_off = ctx->buffer_len;

    size_t trailing_bytes = ctx->buffer_len - insert_off;
    if (trailing_bytes > 0) {
        memmove(&ctx->raw_buffer[insert_off + ctx->clipboard.len],
                &ctx->raw_buffer[insert_off],
                trailing_bytes);
    }

    memcpy(&ctx->raw_buffer[insert_off], ctx->clipboard.buffer, ctx->clipboard.len);
    ctx->buffer_len += ctx->clipboard.len;
    ctx->is_dirty = true;
    rap_core_index_lines(ctx);

    rap_ops_set_cursor_from_offset(ctx, insert_off + ctx->clipboard.len);
    rap_ops_clamp_cursor(ctx);

    return 0;
}

/* 7. Point d'entrée pour la commande :pp (collage au curseur) */
static int rap_cmd_clip_paste_pp(rap_context_t *ctx, const char *args) {
    (void)args;
    if (!ctx || ctx->is_readonly) return -1;

    if (ctx->clipboard.len == 0) {
        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                 RAP_ANSI_YELLOW "[!] Presse-papiers vide. Rien à coller." RAP_ANSI_RESET);
        return 0;
    }

    if (ctx->clipboard.is_line_block) {
        uint32_t target = ctx->cursor_row + 1;
        rap_clip_paste_line(ctx, target);
        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                 RAP_ANSI_BOLD "[✓] %u octets collés à la ligne %u" RAP_ANSI_RESET,
                 ctx->clipboard.len, target);
    } else {
        uint32_t off = rap_ops_get_cursor_offset(ctx);
        rap_clip_paste_bytes(ctx, off);
        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                 RAP_ANSI_BOLD "[✓] %u octets collés avec succès" RAP_ANSI_RESET,
                 ctx->clipboard.len);
    }
    return 0;
}

/* 8. Point d'entrée pour la commande :clip (inspection du contenu) */
static int rap_cmd_clip_info(rap_context_t *ctx, const char *args) {
    (void)args;
    if (!ctx) return -1;
    snprintf(ctx->status_msg, sizeof(ctx->status_msg),
             RAP_ANSI_REVERSE " [CLIPBOARD] %u octets | Mode: %s " RAP_ANSI_RESET,
             ctx->clipboard.len,
             ctx->clipboard.is_line_block ? "Bloc de lignes" : "Sélection brute");
    return 0;
}

/* 9. Parseur universel de commandes CLI pour le presse-papiers */
int rap_clip_parse_line_macro(rap_context_t *ctx, const char *cmd) {
    if (!ctx || !cmd || *cmd == '\0') return -127;

    /* A. Commande directe :pp */
    if (strcasecmp(cmd, "pp") == 0) {
        return rap_cmd_clip_paste_pp(ctx, NULL);
    }

    /* B. Ligne courante unitaire : :c ou :x */
    if ((cmd[0] == 'c' || cmd[0] == 'C' || cmd[0] == 'x' || cmd[0] == 'X') && cmd[1] == '\0') {
        uint32_t cur = ctx->cursor_row + 1;
        if (cmd[0] == 'c' || cmd[0] == 'C') {
            int res = rap_clip_copy_range(ctx, cur, cur);
            if (res == 0) {
                snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                         RAP_ANSI_BOLD "[✓] Ligne %u copiée dans le presse-papiers (%u octets)" RAP_ANSI_RESET,
                         cur, ctx->clipboard.len);
            }
            return res;
        } else {
            int res = rap_clip_cut_range(ctx, cur, cur);
            if (res == 0) {
                snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                         RAP_ANSI_BOLD "[✓] Ligne %u coupée vers le presse-papiers (%u octets)" RAP_ANSI_RESET,
                         cur, ctx->clipboard.len);
            }
            return res;
        }
    }

    /* C. Les autres commandes débutent obligatoirement par un chiffre */
    if (cmd[0] < '0' || cmd[0] > '9') {
        return -127;
    }

    const char *p = cmd;
    int num1 = atoi(p);
    while (*p >= '0' && *p <= '9') p++;

    /* Forme C1 : Collage sous une ligne (<dest>w, <dest>pp, <dest>p, ex: :15w, :15pp) */
    if (strcasecmp(p, "w") == 0 || strcasecmp(p, "pp") == 0 || strcasecmp(p, "p") == 0) {
        if (ctx->clipboard.len == 0) {
            snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                     RAP_ANSI_YELLOW "[!] Presse-papiers vide. Rien à coller." RAP_ANSI_RESET);
            return 0;
        }
        int dest = (num1 < 1) ? 1 : num1;
        rap_clip_paste_line(ctx, (uint32_t)dest);
        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                 RAP_ANSI_BOLD "[✓] %u octets collés à la ligne %d" RAP_ANSI_RESET,
                 ctx->clipboard.len, dest);
        return 0;
    }

    /* Forme C2 : Copie/Coupe unitaire (<num>c, <num>x, ex: :10c, :10x) ou combinée :10c:15w */
    if (*p == 'c' || *p == 'C' || *p == 'x' || *p == 'X') {
        char op = (char)toupper((unsigned char)*p);
        p++;
        if (*p == '\0') {
            int line = (num1 < 1) ? 1 : num1;
            if (op == 'C') {
                int res = rap_clip_copy_range(ctx, (uint32_t)line, (uint32_t)line);
                if (res == 0) {
                    snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                             RAP_ANSI_BOLD "[✓] Ligne %d copiée dans le presse-papiers (%u octets)" RAP_ANSI_RESET,
                             line, ctx->clipboard.len);
                }
                return res;
            } else {
                int res = rap_clip_cut_range(ctx, (uint32_t)line, (uint32_t)line);
                if (res == 0) {
                    snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                             RAP_ANSI_BOLD "[✓] Ligne %d coupée vers le presse-papiers (%u octets)" RAP_ANSI_RESET,
                             line, ctx->clipboard.len);
                }
                return res;
            }
        } else if (*p == ':') {
            /* Macro combinée unitaire : :10c:15w ou :10c:15pp */
            p++;
            if (*p >= '0' && *p <= '9') {
                int dest = atoi(p);
                while (*p >= '0' && *p <= '9') p++;
                if (*p == '\0' || strcasecmp(p, "w") == 0 || strcasecmp(p, "p") == 0 || strcasecmp(p, "pp") == 0) {
                    int line = (num1 < 1) ? 1 : num1;
                    if (dest < 1) dest = 1;
                    if (op == 'C') {
                        if (rap_clip_copy_range(ctx, (uint32_t)line, (uint32_t)line) == 0) {
                            rap_clip_paste_line(ctx, (uint32_t)dest);
                            snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                                     RAP_ANSI_BOLD "[✓] Ligne %d copiée et collée en ligne %d (%u octets)" RAP_ANSI_RESET,
                                     line, dest, ctx->clipboard.len);
                            return 0;
                        }
                    } else {
                        if (rap_clip_cut_range(ctx, (uint32_t)line, (uint32_t)line) == 0) {
                            rap_clip_paste_line(ctx, (uint32_t)dest);
                            snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                                     RAP_ANSI_BOLD "[✓] Ligne %d déplacée en ligne %d (%u octets)" RAP_ANSI_RESET,
                                     line, dest, ctx->clipboard.len);
                            return 0;
                        }
                    }
                    return 0;
                }
            }
        }
        return -127;
    }

    /* Forme C3 : Plage (<deb>:<fin>c, <deb>:<fin>x, ex: :1:10c) ou combinée :1:10c:15w */
    if (*p == ':') {
        p++;
        if (*p < '0' || *p > '9') return -127;
        int num2 = atoi(p);
        while (*p >= '0' && *p <= '9') p++;

        if (*p == 'c' || *p == 'C' || *p == 'x' || *p == 'X') {
            char op = (char)toupper((unsigned char)*p);
            p++;
            if (*p == '\0') {
                int start = (num1 < 1) ? 1 : num1;
                int end = (num2 < start) ? start : num2;
                if (op == 'C') {
                    int res = rap_clip_copy_range(ctx, (uint32_t)start, (uint32_t)end);
                    if (res == 0) {
                        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                                 RAP_ANSI_BOLD "[✓] Lignes %d à %d copiées dans le presse-papiers (%u octets)" RAP_ANSI_RESET,
                                 start, end, ctx->clipboard.len);
                    }
                    return res;
                } else {
                    int res = rap_clip_cut_range(ctx, (uint32_t)start, (uint32_t)end);
                    if (res == 0) {
                        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                                 RAP_ANSI_BOLD "[✓] Lignes %d à %d coupées vers le presse-papiers (%u octets)" RAP_ANSI_RESET,
                                 start, end, ctx->clipboard.len);
                    }
                    return res;
                }
            } else if (*p == ':') {
                p++;
                if (*p >= '0' && *p <= '9') {
                    int dest = atoi(p);
                    while (*p >= '0' && *p <= '9') p++;
                    if (*p == '\0' || strcasecmp(p, "w") == 0 || strcasecmp(p, "p") == 0 || strcasecmp(p, "pp") == 0) {
                        int start = (num1 < 1) ? 1 : num1;
                        int end = (num2 < start) ? start : num2;
                        if (dest < 1) dest = 1;
                        if (op == 'C') {
                            if (rap_clip_copy_range(ctx, (uint32_t)start, (uint32_t)end) == 0) {
                                rap_clip_paste_line(ctx, (uint32_t)dest);
                                snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                                         RAP_ANSI_BOLD "[✓] Lignes %d à %d copiées et collées en ligne %d (%u octets)" RAP_ANSI_RESET,
                                         start, end, dest, ctx->clipboard.len);
                                return 0;
                            }
                        } else {
                            if (rap_clip_cut_range(ctx, (uint32_t)start, (uint32_t)end) == 0) {
                                rap_clip_paste_line(ctx, (uint32_t)dest);
                                snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                                         RAP_ANSI_BOLD "[✓] Lignes %d à %d déplacées en ligne %d (%u octets)" RAP_ANSI_RESET,
                                         start, end, dest, ctx->clipboard.len);
                                return 0;
                            }
                        }
                        return 0;
                    }
                }
            }
        }
        /* Si ce n'est ni C ni X (ex: :1:10d), laisser impérativement passer vers mod_delete */
        return -127;
    }

    return -127;
}

/* 10. Enregistrement persistant en mémoire statique BSS */
int rap_plugin_mod_clipboard_init(void) {
    static rap_plugin_t s_plug_clip = {
        .cmd_trigger = "clip",
        .description = "Affiche la taille du contenu actuel du presse-papiers",
        .execute     = rap_cmd_clip_info
    };
    static rap_plugin_t s_plug_pp = {
        .cmd_trigger = "pp",
        .description = "Colle le presse-papiers à la position courante",
        .execute     = rap_cmd_clip_paste_pp
    };

    rap_plugin_register(&s_plug_clip);
    rap_plugin_register(&s_plug_pp);
    return 0;
}
