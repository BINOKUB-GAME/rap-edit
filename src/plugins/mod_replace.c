/* ==============================================================================
 * SCRIPT      : src/plugins/mod_replace.c
 * RÉVISION    : Rev 1.0
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Moteur de substitution de chaînes 0-heap sans allocation dynamique.
 *               Prise en charge de :s, :%s, :<num>s et :<deb>:<fin>s avec ou sans 'g'.
 *               Capture automatique Undo au 1er match et translation mémoire memmove.
 * FONCTIONS   :
 *   1. rap_replace_in_range (Moteur d'application de substitution sur une plage)
 *   2. rap_replace_parse_cmd (Parseur des commandes :s/.../..., :%s, etc.)
 *   3. rap_plugin_mod_replace_init (Enregistrement du greffon dans la table BSS)
 * ============================================================================== */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#include "rap_edit_replace.h"
#include "rap_edit_config.h"
#include "rap_edit_plugin.h"
#include "rap_edit_undo.h"
#include "rap_edit_ops.h"

extern int rap_core_index_lines(rap_context_t *ctx);

/* 1. Exécution de la substitution sur une plage de lignes */
static int rap_replace_in_range(rap_context_t *ctx, uint32_t start_line, uint32_t end_line,
                                const char *find_str, const char *repl_str, bool global) {
    if (!ctx || ctx->is_readonly || ctx->total_lines == 0) return -1;
    if (!find_str || *find_str == '\0') return -1;

    size_t find_len = strlen(find_str);
    size_t repl_len = repl_str ? strlen(repl_str) : 0;

    if (start_line < 1) start_line = 1;
    if (end_line > ctx->total_lines) end_line = ctx->total_lines;
    if (start_line > end_line) return -1;

    bool snapshotted = false;
    uint32_t replacements_count = 0;

    for (uint32_t l = start_line - 1; l < end_line; ++l) {
        uint32_t line_start = ctx->lines[l].offset;
        uint32_t line_len   = ctx->lines[l].length;
        uint32_t scan_idx   = 0;

        while (scan_idx + find_len <= line_len) {
            uint32_t current_off = line_start + scan_idx;

            if (memcmp(&ctx->raw_buffer[current_off], find_str, find_len) == 0) {
                /* Snapshot Undo déclenché immédiatement avant la toute première modification */
                if (!snapshotted) {
                    rap_undo_snapshot(ctx);
                    snapshotted = true;
                }

                /* Vérification de la capacité mémoire statique */
                if (repl_len > find_len) {
                    size_t growth = repl_len - find_len;
                    if (ctx->buffer_len + growth >= RAP_STATIC_BUFFER_SIZE) {
                        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                                 RAP_ANSI_YELLOW "[!] Mémoire BSS saturée (limite 64 Ko)" RAP_ANSI_RESET);
                        return -2;
                    }
                }

                uint32_t tail_start = current_off + find_len;
                size_t tail_bytes = ctx->buffer_len - tail_start;

                /* Translation du reste du document si les longueurs diffèrent */
                if (repl_len != find_len && tail_bytes > 0) {
                    memmove(&ctx->raw_buffer[current_off + repl_len],
                            &ctx->raw_buffer[tail_start],
                            tail_bytes);
                }

                /* Copie de la nouvelle chaîne */
                if (repl_len > 0) {
                    memcpy(&ctx->raw_buffer[current_off], repl_str, repl_len);
                }

                /* Ajustement de la longueur totale du tampon */
                if (repl_len >= find_len) {
                    ctx->buffer_len += (uint32_t)(repl_len - find_len);
                } else {
                    ctx->buffer_len -= (uint32_t)(find_len - repl_len);
                }

                ctx->is_dirty = true;
                replacements_count++;

                /* Réindexation pour conserver la cohérence exacte des offsets de chaque ligne */
                rap_core_index_lines(ctx);

                line_start = ctx->lines[l].offset;
                line_len   = ctx->lines[l].length;

                scan_idx += (uint32_t)repl_len;

                /* Si l'option globale 'g' n'est pas demandée, on passe à la ligne suivante */
                if (!global) {
                    break;
                }
            } else {
                scan_idx++;
            }
        }
    }

    rap_ops_clamp_cursor(ctx);

    if (replacements_count > 0) {
        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                 RAP_ANSI_BOLD "[✓] %u remplacement%s effectué%s" RAP_ANSI_RESET,
                 replacements_count, replacements_count > 1 ? "s" : "", replacements_count > 1 ? "s" : "");
    } else {
        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                 RAP_ANSI_YELLOW "[!] Motif '%.32s' introuvable dans la plage sélectionnée" RAP_ANSI_RESET,
                 find_str);
    }

    return 0;
}

/* 2. Parseur syntaxique pour :s, :%s, :<num>s et :<deb>:<fin>s */
int rap_replace_parse_cmd(rap_context_t *ctx, const char *cmd) {
    if (!ctx || !cmd) return -1;

    while (*cmd == ' ' || *cmd == ':') {
        cmd++;
    }
    if (*cmd == '\0') return -1;

    uint32_t start_line = 0;
    uint32_t end_line   = 0;
    const char *p = cmd;

    /* Cas A : Fichier complet %s/... */
    if (*p == '%' && *(p + 1) == 's') {
        start_line = 1;
        end_line = ctx->total_lines;
        p += 2;
    }
    /* Cas B : Ligne courante s/... */
    else if (*p == 's') {
        start_line = ctx->cursor_row + 1;
        end_line = ctx->cursor_row + 1;
        p += 1;
    }
    /* Cas C : Plage numérique <num>s/... ou <deb>:<fin>s/... */
    else if (isdigit((unsigned char)*p)) {
        char *endptr = NULL;
        unsigned long n1 = strtoul(p, &endptr, 10);
        if (!endptr || endptr == p) return -127;

        if (*endptr == ':') {
            p = endptr + 1;
            if (!isdigit((unsigned char)*p)) return -127;
            unsigned long n2 = strtoul(p, &endptr, 10);
            if (!endptr || endptr == p || *endptr != 's') return -127;
            start_line = (uint32_t)n1;
            end_line   = (uint32_t)n2;
            p = endptr + 1;
        } else if (*endptr == 's') {
            start_line = (uint32_t)n1;
            end_line   = (uint32_t)n1;
            p = endptr + 1;
        } else {
            return -127;
        }
    } else {
        return -127; /* Non concerné par la substitution */
    }

    /* Le délimiteur standard est le caractère suivant (ex: '/') */
    char delim = *p;
    if (delim != '/' && delim != '#' && delim != '@') {
        return -127;
    }
    p++;

    /* Extraction du motif de recherche */
    char find_buf[RAP_MAX_LINE_INPUT];
    uint32_t f_idx = 0;
    while (*p && *p != delim && f_idx + 1 < sizeof(find_buf)) {
        find_buf[f_idx++] = *p++;
    }
    find_buf[f_idx] = '\0';

    if (*p != delim || f_idx == 0) {
        return -127;
    }
    p++; /* Saut du second délimiteur */

    /* Extraction du texte de remplacement */
    char repl_buf[RAP_MAX_LINE_INPUT];
    uint32_t r_idx = 0;
    while (*p && *p != delim && r_idx + 1 < sizeof(repl_buf)) {
        repl_buf[r_idx++] = *p++;
    }
    repl_buf[r_idx] = '\0';

    if (*p == delim) {
        p++; /* Saut du troisième délimiteur si fourni */
    }

    /* Lecture des drapeaux (ex: 'g') */
    bool global = false;
    while (*p) {
        if (*p == 'g' || *p == 'G') {
            global = true;
        }
        p++;
    }

    return rap_replace_in_range(ctx, start_line, end_line, find_buf, repl_buf, global);
}

/* 3. Enregistrement du greffon dans la table BSS */
int rap_plugin_mod_replace_init(void) {
    rap_plugin_t plug_rep = {
        .cmd_trigger = "s",
        .description = "Substitution de chaînes (:s/anc/nouv/[g] ou :%s/...)",
        .execute     = NULL
    };
    rap_plugin_register(&plug_rep);
    return 0;
}
