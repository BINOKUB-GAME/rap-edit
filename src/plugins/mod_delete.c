/* ==============================================================================
 * SCRIPT      : src/plugins/mod_delete.c
 * RÉVISION    : Rev 2.0 (Strict Relative & Absolute Batch Deletion Resync)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Prise en charge exacte et unifiée de :d et :dd (ligne courante),
 *               :<N>dd (suppression relative par lot depuis le curseur),
 *               :<num>d (ligne absolue) et :<deb>:<fin>d (plage absolue).
 *               Resynchronisation stricte avec rap_core_index_lines(),
 *               rap_undo_snapshot() et conservation du prototype 1-based.
 * FONCTIONS   :
 *   1. rap_delete_lines (Suppression physique par memmove, snapshot undo et réindexation)
 *   2. rap_delete_parse_cmd (Parseur strict discriminant :d, :dd, :<N>dd, :<num>d, :<deb>:<fin>d)
 *   3. rap_plugin_mod_delete_init (Enregistrement du greffon dans la table BSS)
 * ============================================================================== */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#include "rap_edit_delete.h"
#include "rap_edit_config.h"
#include "rap_edit_plugin.h"
#include "rap_edit_undo.h"
#include "rap_edit_ops.h"

extern int rap_core_index_lines(rap_context_t *ctx);

/* 1. Suppression physique de lignes avec compaction mémoire BSS */
int rap_delete_lines(rap_context_t *ctx, uint32_t start_line, uint32_t end_line) {
    if (!ctx || ctx->is_readonly || ctx->total_lines == 0) return -1;

    if (start_line < 1) start_line = 1;
    if (start_line > ctx->total_lines) return -1;
    if (end_line > ctx->total_lines) end_line = ctx->total_lines;
    if (start_line > end_line) return -1;

    rap_undo_snapshot(ctx);

    uint32_t start_idx = start_line - 1;
    uint32_t end_idx = end_line - 1;

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

    rap_ops_clamp_cursor(ctx);

    uint32_t count = (end_line - start_line) + 1;
    snprintf(ctx->status_msg, sizeof(ctx->status_msg),
             RAP_ANSI_BOLD "[✓] %u ligne%s supprimée%s" RAP_ANSI_RESET,
             count, count > 1 ? "s" : "", count > 1 ? "s" : "");

    return 0;
}

/* 2. Parseur strict discriminant :d, :dd, :<N>dd, :<num>d, :<deb>:<fin>d */
int rap_delete_parse_cmd(rap_context_t *ctx, const char *cmd) {
    if (!ctx || !cmd) return -1;

    while (*cmd == ' ' || *cmd == ':') {
        cmd++;
    }
    if (*cmd == '\0') return -1;

    /* A. Ligne courante isolée : :d ou :dd */
    if (strcmp(cmd, "d") == 0 || strcmp(cmd, "D") == 0 ||
        strcmp(cmd, "dd") == 0 || strcmp(cmd, "DD") == 0) {
        if (ctx->total_lines == 0) return 0;
        uint32_t current_line = ctx->cursor_row + 1;
        rap_delete_lines(ctx, current_line, current_line);
        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                 RAP_ANSI_BOLD "[✓] 1 ligne supprimée (courante L:%u)" RAP_ANSI_RESET,
                 current_line);
        return 0;
    }

    size_t len = strlen(cmd);

    /* B. Paquet relatif depuis la position courante : :<N>dd (ex: 10dd) */
    if (len >= 3 && (cmd[len - 2] == 'd' || cmd[len - 2] == 'D') &&
                    (cmd[len - 1] == 'd' || cmd[len - 1] == 'D')) {
        bool all_digits = true;
        for (size_t i = 0; i < len - 2; ++i) {
            if (!isdigit((unsigned char)cmd[i])) {
                all_digits = false;
                break;
            }
        }
        if (all_digits) {
            uint32_t count = (uint32_t)strtoul(cmd, NULL, 10);
            if (count == 0) {
                snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                         RAP_ANSI_YELLOW "[!] Compteur relatif invalide : 0" RAP_ANSI_RESET);
                return 0;
            }
            if (ctx->total_lines == 0) return 0;

            uint32_t cur = ctx->cursor_row + 1;
            uint32_t end = cur + count - 1;
            if (end > ctx->total_lines) {
                end = ctx->total_lines;
            }
            rap_delete_lines(ctx, cur, end);
            uint32_t deleted = (end - cur) + 1;
            snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                     RAP_ANSI_BOLD "[✓] %u ligne%s supprimée%s (relatif depuis L:%u)" RAP_ANSI_RESET,
                     deleted, deleted > 1 ? "s" : "", deleted > 1 ? "s" : "", cur);
            return 0;
        }
    }

    /* C. Plage absolue : :<deb>:<fin>d (ex: 3:90d) */
    uint32_t start = 0, end = 0;
    char op = 0;
    char trailing = 0;
    if (sscanf(cmd, "%u:%u%c%c", &start, &end, &op, &trailing) == 3) {
        op = (char)tolower((unsigned char)op);
        if (op == 'd') {
            if (start < 1 || end < start || start > ctx->total_lines) {
                snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                         RAP_ANSI_YELLOW "[!] Bornes invalides : %u à %u (Total: %u)" RAP_ANSI_RESET,
                         start, end, ctx->total_lines);
                return 0;
            }
            if (end > ctx->total_lines) end = ctx->total_lines;
            rap_delete_lines(ctx, start, end);
            return 0;
        }
    }

    /* D. Ligne absolue unique : :<num>d (ex: 2d, 42d avec un seul 'd') */
    if (len >= 2 && (cmd[len - 1] == 'd' || cmd[len - 1] == 'D') &&
                    (cmd[len - 2] != 'd' && cmd[len - 2] != 'D')) {
        bool all_digits = true;
        for (size_t i = 0; i < len - 1; ++i) {
            if (!isdigit((unsigned char)cmd[i])) {
                all_digits = false;
                break;
            }
        }
        if (all_digits) {
            uint32_t single = (uint32_t)strtoul(cmd, NULL, 10);
            if (single < 1 || single > ctx->total_lines) {
                snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                         RAP_ANSI_YELLOW "[!] Ligne %u hors limites (Total: %u)" RAP_ANSI_RESET,
                         single, ctx->total_lines);
                return 0;
            }
            rap_delete_lines(ctx, single, single);
            snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                     RAP_ANSI_BOLD "[✓] Ligne %u supprimée (absolu)" RAP_ANSI_RESET,
                     single);
            return 0;
        }
    }

    return -127;
}

/* 3. Enregistrement en table BSS */
int rap_plugin_mod_delete_init(void) {
    static rap_plugin_t plug_del = {
        .cmd_trigger = "d",
        .description = "Supprime la ligne courante (:d, :dd), par lot (:10dd), une ligne (:2d) ou une plage (:3:90d)",
        .execute     = NULL
    };
    rap_plugin_register(&plug_del);
    return 0;
}
