/* ==============================================================================
 * SCRIPT      : src/plugins/mod_delete.c
 * RÉVISION    : Rev 1.2 (Instant Status Notifications)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Notification instantanée sans sleep via ctx->status_msg lors de la
 *               suppression d'une ligne (:2d) ou d'une plage entière (:3:90d).
 * FONCTIONS   :
 *   1. rap_delete_lines (Suppression physique par memmove et réindexation BSS)
 *   2. rap_delete_parse_cmd (Parseur des syntaxes :d, :2d et :3:90d)
 *   3. rap_plugin_mod_delete_init (Enregistrement du greffon dans la table BSS)
 * ============================================================================== */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

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

/* 2. Parseur pour :d, :<num>d et :<deb>:<fin>d */
int rap_delete_parse_cmd(rap_context_t *ctx, const char *cmd) {
    if (!ctx || !cmd) return -1;

    while (*cmd == ' ' || *cmd == ':') {
        cmd++;
    }
    if (*cmd == '\0') return -1;

    uint32_t start = 0, end = 0, single = 0;
    char op = 0;

    /* Format 1 : Plage de lignes <deb>:<fin>d (ex: 3:90d) */
    if (sscanf(cmd, "%u:%u%c", &start, &end, &op) >= 3) {
        op = (char)tolower((unsigned char)op);
        if (op == 'd') {
            if (start < 1 || end < start || start > ctx->total_lines) {
                snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                         RAP_ANSI_YELLOW "[!] Bornes invalides : %u à %u (Total: %u)" RAP_ANSI_RESET,
                         start, end, ctx->total_lines);
                return 0;
            }
            rap_delete_lines(ctx, start, end);
            return 0;
        }
    }

    /* Format 2 : Ligne unique <num>d (ex: 2d) */
    if (sscanf(cmd, "%u%c", &single, &op) >= 2) {
        op = (char)tolower((unsigned char)op);
        if (op == 'd') {
            if (single < 1 || single > ctx->total_lines) {
                snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                         RAP_ANSI_YELLOW "[!] Ligne %u hors limites (Total: %u)" RAP_ANSI_RESET,
                         single, ctx->total_lines);
                return 0;
            }
            rap_delete_lines(ctx, single, single);
            return 0;
        }
    }

    /* Format 3 : Commande 'd' isolée */
    if (strcmp(cmd, "d") == 0 || strcmp(cmd, "D") == 0) {
        uint32_t current_line = ctx->cursor_row + 1;
        rap_delete_lines(ctx, current_line, current_line);
        return 0;
    }

    return -127;
}

/* 3. Enregistrement */
int rap_plugin_mod_delete_init(void) {
    rap_plugin_t plug_del = {
        .cmd_trigger = "d",
        .description = "Supprime la ligne courante (:d), une ligne (:2d) ou une plage (:3:90d)",
        .execute     = NULL
    };
    rap_plugin_register(&plug_del);
    return 0;
}
