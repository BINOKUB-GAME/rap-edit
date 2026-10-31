/* ==============================================================================
 * SCRIPT      : src/plugins/mod_clipboard.c
 * RÉVISION    : Rev 1.5 (Strict Macro Routing Fix)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Correction du routage : renvoi impératif de -127 si l'opération
 *               n'est ni 'C' ni 'X', permettant aux commandes comme :3:90d
 *               d'atteindre le greffon mod_delete sans être court-circuitées.
 * FONCTIONS   :
 *   1. rap_clip_copy_range (Copie de lignes complètes)
 *   2. rap_clip_cut_range (Coupe de lignes avec instantané d'annulation)
 *   3. rap_clip_paste_line (Collage de lignes avec instantané d'annulation)
 *   4. rap_clip_copy_bytes (Copie de sélection précise)
 *   5. rap_clip_cut_bytes (Coupe de sélection avec instantané d'annulation)
 *   6. rap_clip_paste_bytes (Collage précis avec instantané d'annulation)
 *   7. rap_clip_parse_line_macro (Parseur universel combiné et séquentiel)
 *   8. rap_plugin_mod_clipboard_init (Enregistrement du greffon en BSS)
 * ============================================================================== */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "rap_edit_plugin.h"
#include "rap_edit_config.h"
#include "rap_edit_undo.h"

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

/* 6. Collage de sélection précise avec snapshot Undo */
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

    return 0;
}

/* 7. Parseur universel de commandes */
int rap_clip_parse_line_macro(rap_context_t *ctx, const char *cmd) {
    if (!ctx || !cmd) return -1;

    int start = 0, end = 0, dest = 0;
    char op = 0, paste_flag = 0;
    char trailing = 0;

    /* Format 1 : Macro complète <deb>:<fin>[C|X]:<dest>[W|P] */
    if (sscanf(cmd, "%d:%d%c:%d%c", &start, &end, &op, &dest, &paste_flag) == 5) {
        op = (char)toupper((unsigned char)op);
        paste_flag = (char)toupper((unsigned char)paste_flag);

        if ((op != 'C' && op != 'X') || (paste_flag != 'W' && paste_flag != 'P')) {
            return -127;
        }

        if (start < 1 || end < start || dest < 1) {
            printf("\033[%d;1H\033[K[!] Bornes invalides : deb=%d fin=%d dest=%d", ctx->term_rows, start, end, dest);
            fflush(stdout);
            return 0;
        }

        if (op == 'C') {
            if (rap_clip_copy_range(ctx, (uint32_t)start, (uint32_t)end) == 0) {
                rap_clip_paste_line(ctx, (uint32_t)dest);
                return 0;
            }
        } else if (op == 'X') {
            if (rap_clip_cut_range(ctx, (uint32_t)start, (uint32_t)end) == 0) {
                rap_clip_paste_line(ctx, (uint32_t)dest);
                return 0;
            }
        }
        return 0;
    }

    /* Format 2 : Étape unitaire couper/copier <deb>:<fin>[C|X] */
    if (sscanf(cmd, "%d:%d%c%c", &start, &end, &op, &trailing) >= 3) {
        op = (char)toupper((unsigned char)op);

        /* Si l'opérateur n'est ni C ni X (ex: 'd'), ne pas intercepter ! */
        if (op != 'C' && op != 'X') {
            return -127;
        }

        if (op == 'C') {
            if (rap_clip_copy_range(ctx, (uint32_t)start, (uint32_t)end) == 0) {
                printf("\033[%d;1H\033[K[✓] Lignes %d à %d copiées dans le presse-papiers (%u octets).", 
                       ctx->term_rows, start, end, ctx->clipboard.len);
                fflush(stdout);
                return 0;
            }
        } else if (op == 'X') {
            if (rap_clip_cut_range(ctx, (uint32_t)start, (uint32_t)end) == 0) {
                printf("\033[%d;1H\033[K[✓] Lignes %d à %d coupées vers le presse-papiers (%u octets).", 
                       ctx->term_rows, start, end, ctx->clipboard.len);
                fflush(stdout);
                return 0;
            }
        }
        return 0;
    }

    /* Format 3 : Étape unitaire coller <dest>[W|P] */
    if (sscanf(cmd, "%d%c%c", &dest, &paste_flag, &trailing) >= 2) {
        paste_flag = (char)toupper((unsigned char)paste_flag);
        if (paste_flag == 'W' || paste_flag == 'P') {
            if (ctx->clipboard.len == 0) {
                printf("\033[%d;1H\033[K[!] Presse-papiers vide. Rien à coller.", ctx->term_rows);
                fflush(stdout);
                return 0;
            }
            rap_clip_paste_line(ctx, (uint32_t)dest);
            return 0;
        }
    }

    return -127;
}

/* 8. Enregistrement */
int rap_plugin_mod_clipboard_init(void) {
    rap_plugin_t plug_clip = {
        .cmd_trigger = "clip",
        .description = "Affiche la taille du contenu actuel du presse-papiers",
        .execute     = NULL
    };
    rap_plugin_register(&plug_clip);
    return 0;
}
