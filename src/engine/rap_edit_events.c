/* ==============================================================================
 * SCRIPT      : src/engine/rap_edit_events.c
 * RÉVISION    : Rev 1.10 (Multi-Buffer Event Routing & Ctrl+W Tab Switch)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Prise en charge de Ctrl+W (cycle d'onglets) et routage dynamique
 *               du contexte via rap_buffer_get_active() à chaque tour de boucle.
 * FONCTIONS   :
 *   1. rap_events_handle_colon_command (Parseur et dispatch des commandes CLI)
 *   2. rap_events_process_escape (Décodage des séquences d'échappement VT100)
 *   3. rap_events_prompt_command (Invite de commande avec tolérance EINTR)
 *   4. rap_events_run_loop (Boucle principale multi-onglets réactive)
 * ============================================================================== */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "rap_edit_events.h"
#include "rap_edit_term.h"
#include "rap_edit_ops.h"
#include "rap_edit_config.h"
#include "rap_edit_plugin.h"
#include "rap_edit_undo.h"
#include "rap_edit_delete.h"
#include "rap_edit_replace.h"
#include "rap_edit_buffer.h"

extern void rap_ui_render_screen(rap_context_t *ctx);
extern void rap_ui_update_dimensions(rap_context_t *ctx);
extern void rap_ui_show_help_screen(void);
extern void rap_nav_bol(rap_context_t *ctx);
extern void rap_nav_eol(rap_context_t *ctx);

extern int  rap_clip_copy_bytes(rap_context_t *ctx, uint32_t start_off, uint32_t end_off);
extern int  rap_clip_cut_bytes(rap_context_t *ctx, uint32_t start_off, uint32_t end_off);
extern int  rap_clip_paste_bytes(rap_context_t *ctx, uint32_t insert_off);
extern int  rap_clip_parse_line_macro(rap_context_t *ctx, const char *cmd);

/* 1. Parseur et dispatch instantané des commandes de l'invite ':' */
static void rap_events_handle_colon_command(rap_context_t *ctx, const char *cmd) {
    if (!cmd || *cmd == '\0') return;

    if (rap_clip_parse_line_macro(ctx, cmd) == 0) {
        rap_ops_clamp_cursor(ctx);
        return;
    }

    if (rap_delete_parse_cmd(ctx, cmd) == 0) {
        rap_ops_clamp_cursor(ctx);
        return;
    }

    if (rap_replace_parse_cmd(ctx, cmd) == 0) {
        rap_ops_clamp_cursor(ctx);
        return;
    }

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        rap_ui_show_help_screen();
        return;
    }

    if (strcmp(cmd, "info") == 0 || strcmp(cmd, "count") == 0) {
        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                 RAP_ANSI_REVERSE " [INFO] Lignes: %u | Presse-papiers: %u o | Buffer: %u o " RAP_ANSI_RESET,
                 ctx->total_lines, ctx->clipboard.len, ctx->buffer_len);
        return;
    }

    if (strcmp(cmd, "$") == 0) {
        if (ctx->total_lines > 0) {
            ctx->cursor_row = ctx->total_lines - 1;
            ctx->cursor_col = 0;
            rap_undo_reset_burst();
        }
        return;
    }

    /* Commande d'onglet :1 .. :4 si précédée d'un dièse ou commande de saut direct */
    if (cmd[0] == '#' && cmd[1] >= '1' && cmd[1] <= '4') {
        rap_plugin_dispatch(ctx, cmd);
        return;
    }

    if (cmd[0] >= '0' && cmd[0] <= '9') {
        int target = atoi(cmd);
        if (target >= 1 && (uint32_t)target <= ctx->total_lines) {
            ctx->cursor_row = (uint32_t)(target - 1);
            ctx->cursor_col = 0;
            rap_undo_reset_burst();
        }
        return;
    }

    if (strcmp(cmd, "q!") == 0) {
        exit(0);
    } else if (strcmp(cmd, "w!") == 0) {
        rap_plugin_dispatch(ctx, "w!");
    } else {
        int res = rap_plugin_dispatch(ctx, cmd);
        if (res == -127) {
            snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                     RAP_ANSI_YELLOW "[!] Commande inconnue : '%.32s'. Tapez :help" RAP_ANSI_RESET, cmd);
        }
    }
    rap_ops_clamp_cursor(ctx);
}

/* 2. Décodage des séquences d'échappement VT100 */
static void rap_events_process_escape(rap_context_t *ctx) {
    if (!rap_term_has_input(35)) {
        ctx->is_selecting = false;
        return;
    }

    char seq[8];
    int n = 0;
    seq[n++] = '\033';
    while (n < 7 && rap_term_has_input(20)) {
        if (read(STDIN_FILENO, &seq[n], 1) <= 0) break;
        n++;
    }
    seq[n] = '\0';

    if (n == 3 && seq[1] == '[') {
        rap_ops_move_cursor(ctx, seq[2]);
    } else if (n == 6 && seq[1] == '[' && seq[2] == '1' && seq[3] == ';' && seq[4] == '2') {
        if (!ctx->is_selecting) {
            ctx->is_selecting = true;
            ctx->sel_start_row = ctx->cursor_row;
            ctx->sel_start_col = ctx->cursor_col;
        }
        rap_ops_move_cursor(ctx, seq[5]);
    }
}

/* 3. Saisie interactive de commande en mode brut */
static void rap_events_prompt_command(rap_context_t *ctx) {
    char cmd_buf[RAP_MAX_LINE_INPUT];
    uint32_t cmd_len = 0;
    memset(cmd_buf, 0, sizeof(cmd_buf));

    while (1) {
        if (rap_term_check_resize()) {
            rap_ui_update_dimensions(ctx);
            write(STDOUT_FILENO, "\033[H\033[2J", 7);
            rap_ui_render_screen(ctx);
        }

        printf("\033[%d;1H\033[K:%s\033[%d;%dH\033[?25h",
               ctx->term_rows,
               cmd_buf,
               ctx->term_rows,
               (int)(cmd_len + 2));
        fflush(stdout);

        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (n == 0) break;

        if (c == '\033') {
            if (!rap_term_has_input(35)) {
                return;
            }
            char dummy[4];
            while (rap_term_has_input(10)) {
                if (read(STDIN_FILENO, dummy, 1) <= 0) break;
            }
            continue;
        }

        if (c == '\r' || c == '\n') {
            cmd_buf[cmd_len] = '\0';
            rap_events_handle_colon_command(ctx, cmd_buf);
            return;
        }

        if (c == 127 || c == '\b') {
            if (cmd_len > 0) {
                cmd_len--;
                cmd_buf[cmd_len] = '\0';
            } else {
                return;
            }
            continue;
        }

        if (c >= 32 && c <= 126) {
            if (cmd_len + 1 < sizeof(cmd_buf) - 1) {
                cmd_buf[cmd_len++] = c;
                cmd_buf[cmd_len] = '\0';
            }
        }
    }
}

/* 4. Boucle principale multi-onglets réactive */
void rap_events_run_loop(rap_context_t *ctx) {
    (void)ctx;
    while (1) {
        /* Récupération dynamique du contexte du tampon actif */
        rap_context_t *cur_ctx = rap_buffer_get_active();

        if (rap_term_check_resize()) {
            write(STDOUT_FILENO, "\033[H\033[2J", 7);
        }

        rap_ops_clamp_cursor(cur_ctx);
        rap_ui_render_screen(cur_ctx);

        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (n == 0) break;

        if (c != ':' && c != 7 && cur_ctx->status_msg[0] != '\0') {
            cur_ctx->status_msg[0] = '\0';
        }

        /* Ctrl+W : Bascule d'onglet immédiate */
        if (c == 23) {
            rap_buffer_cycle(1);
            write(STDOUT_FILENO, "\033[H\033[2J", 7);
            continue;
        }

        if (c == '\033') {
            rap_events_process_escape(cur_ctx);
            continue;
        }

        if (c == 1) { /* Ctrl+A : Début de ligne */
            rap_undo_reset_burst();
            rap_nav_bol(cur_ctx);
            rap_ops_clamp_cursor(cur_ctx);
            continue;
        }

        if (c == 5) { /* Ctrl+E : Fin de ligne */
            rap_undo_reset_burst();
            rap_nav_eol(cur_ctx);
            rap_ops_clamp_cursor(cur_ctx);
            continue;
        }

        if (c == 26 || c == 21) { /* Ctrl+Z ou Ctrl+U : Undo */
            rap_undo_apply(cur_ctx);
            continue;
        }

        if (c == 3) { /* Ctrl+C : Copier */
            if (cur_ctx->is_selecting) {
                uint32_t off1 = rap_ops_get_anchor_offset(cur_ctx);
                uint32_t off2 = rap_ops_get_cursor_offset(cur_ctx);
                if (off1 > off2) { uint32_t t = off1; off1 = off2; off2 = t; }
                rap_clip_copy_bytes(cur_ctx, off1, off2);
                cur_ctx->is_selecting = false;
                snprintf(cur_ctx->status_msg, sizeof(cur_ctx->status_msg),
                         RAP_ANSI_BOLD "[✓] %u octets copiés (partagés avec tous les onglets)" RAP_ANSI_RESET,
                         cur_ctx->clipboard.len);
            }
            continue;
        }

        if (c == 24) { /* Ctrl+X : Couper */
            if (cur_ctx->is_selecting) {
                uint32_t off1 = rap_ops_get_anchor_offset(cur_ctx);
                uint32_t off2 = rap_ops_get_cursor_offset(cur_ctx);
                if (off1 > off2) { uint32_t t = off1; off1 = off2; off2 = t; }
                rap_clip_cut_bytes(cur_ctx, off1, off2);
                rap_ops_set_cursor_from_offset(cur_ctx, off1);
                cur_ctx->is_selecting = false;
                snprintf(cur_ctx->status_msg, sizeof(cur_ctx->status_msg),
                         RAP_ANSI_BOLD "[✓] %u octets coupés (partagés avec tous les onglets)" RAP_ANSI_RESET,
                         cur_ctx->clipboard.len);
            }
            continue;
        }

        if (c == 22) { /* Ctrl+V : Coller */
            uint32_t insert_off = rap_ops_get_cursor_offset(cur_ctx);
            rap_clip_paste_bytes(cur_ctx, insert_off);
            cur_ctx->is_selecting = false;
            continue;
        }

        if (c == 7) { /* Ctrl+G : Statistiques */
            snprintf(cur_ctx->status_msg, sizeof(cur_ctx->status_msg),
                     RAP_ANSI_REVERSE " [INFO] %.48s | Ligne %u/%u | %u o | Clip: %u o " RAP_ANSI_RESET,
                     cur_ctx->file_path, cur_ctx->cursor_row + 1, cur_ctx->total_lines, cur_ctx->buffer_len, cur_ctx->clipboard.len);
            continue;
        }

        if (c == 127 || c == '\b') {
            rap_ops_delete_char(cur_ctx);
            continue;
        }

        if (c == '\r' || c == '\n') {
            rap_ops_insert_char(cur_ctx, '\n');
            continue;
        }

        if (c == ':') {
            cur_ctx->status_msg[0] = '\0';
            rap_events_prompt_command(cur_ctx);
            continue;
        }

        unsigned char uc = (unsigned char)c;
        if ((uc >= 32 && uc <= 126) || uc >= 128) {
            rap_ops_insert_char(cur_ctx, (char)uc);
        }
    }
}
