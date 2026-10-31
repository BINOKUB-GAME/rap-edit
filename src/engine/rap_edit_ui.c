/* ==============================================================================
 * SCRIPT      : src/engine/rap_edit_ui.c
 * RÉVISION    : Rev 4.3 (Explicit Visual Rendering of ^M and ^@ Characters)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Rendu visuel direct des retours chariot Windows sous la forme
 *               explicite ^M en rouge vif et des octets nuls 0x00 sous la forme
 *               ^@ en magenta. Élimination totale de l'invisibilité des corruptions.
 * FONCTIONS   :
 *   1. rap_ui_buf_append (Concaténation bornée dans le tampon BSS)
 *   2. rap_ui_buf_append_str (Concaténation rapide de chaînes statiques)
 *   3. rap_ui_is_comment_line (Détection déterministe d'une ligne de commentaire)
 *   4. rap_ui_update_dimensions (Interrogation des dimensions TTY via ioctl)
 *   5. rap_ui_render_screen (Rendu coordonné avec affichage UTF-8, ^M et ^@)
 *   6. rap_ui_show_help_screen (Écran d'aide exhaustif)
 * ============================================================================== */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include "rap_edit_types.h"
#include "rap_edit_config.h"
#include "rap_edit_hud.h"
#include "rap_edit_ops.h"

#define RAP_FRAME_BUF_SIZE (32 * 1024)
static char s_frame_buf[RAP_FRAME_BUF_SIZE];
static uint32_t s_frame_len = 0;

static void rap_ui_buf_append(const char *str, size_t len) {
    if (s_frame_len + len >= RAP_FRAME_BUF_SIZE) return;
    memcpy(&s_frame_buf[s_frame_len], str, len);
    s_frame_len += (uint32_t)len;
}

static void rap_ui_buf_append_str(const char *str) {
    rap_ui_buf_append(str, strlen(str));
}

static bool rap_ui_is_comment_line(const rap_context_t *ctx, uint32_t line_index) {
    if (!ctx || line_index >= ctx->total_lines) return false;

    uint32_t offset = ctx->lines[line_index].offset;
    uint16_t len    = ctx->lines[line_index].length;

    for (uint16_t k = 0; k < len; ++k) {
        unsigned char ch = ctx->raw_buffer[offset + k];
        if (ch == ' ' || ch == '\t') continue;

        if (ch == '#' || ch == ';') {
            return true;
        }
        if (ch == '/' && (k + 1 < len) && ctx->raw_buffer[offset + k + 1] == '/') {
            return true;
        }
        return false;
    }
    return false;
}

void rap_ui_update_dimensions(rap_context_t *ctx) {
    if (!ctx) return;
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 20 && ws.ws_row > 5) {
        ctx->term_cols = (int)ws.ws_col;
        ctx->term_rows = (int)ws.ws_row;
    } else {
        ctx->term_cols = 80;
        ctx->term_rows = 24;
    }
}

void rap_ui_render_screen(rap_context_t *ctx) {
    if (!ctx) return;

    rap_ui_update_dimensions(ctx);
    rap_ops_clamp_cursor(ctx);

    const int reserved_rows = 3;
    int edit_area_height = ctx->term_rows - reserved_rows;
    if (edit_area_height < 1) edit_area_height = 1;

    if (ctx->cursor_row < ctx->viewport_top) {
        ctx->viewport_top = ctx->cursor_row;
    }
    if (ctx->cursor_row >= ctx->viewport_top + (uint32_t)edit_area_height) {
        ctx->viewport_top = ctx->cursor_row - (uint32_t)edit_area_height + 1;
    }

    const int margin_cols = 8;
    int max_text_cols = ctx->term_cols - margin_cols - 1;
    if (max_text_cols < 1) max_text_cols = 1;

    uint32_t cursor_vcol = 0;
    if (ctx->total_lines > 0 && ctx->cursor_row < ctx->total_lines) {
        uint32_t cur_line_off = ctx->lines[ctx->cursor_row].offset;
        uint16_t cur_line_len = ctx->lines[ctx->cursor_row].length;
        uint32_t target_byte  = ctx->cursor_col;
        if (target_byte > cur_line_len) target_byte = cur_line_len;

        uint32_t bi = 0;
        while (bi < target_byte) {
            unsigned char lead = ctx->raw_buffer[cur_line_off + bi];
            uint32_t ulen = rap_utf8_char_len(lead);
            if (bi + ulen > cur_line_len) ulen = 1;
            bi += ulen;
            cursor_vcol++;
        }
    }

    if (cursor_vcol < ctx->viewport_left) {
        ctx->viewport_left = cursor_vcol;
    }
    if (cursor_vcol >= ctx->viewport_left + (uint32_t)max_text_cols) {
        ctx->viewport_left = cursor_vcol - (uint32_t)max_text_cols + 1;
    }

    s_frame_len = 0;
    rap_ui_buf_append_str("\033[?25l");

    rap_hud_render_to_buf(ctx, s_frame_buf, &s_frame_len, sizeof(s_frame_buf));

    char num_buf[32];
    char pos_buf[16];

    uint32_t sel_min_row = ctx->sel_start_row, sel_max_row = ctx->cursor_row;
    uint32_t sel_min_col = ctx->sel_start_col, sel_max_col = ctx->cursor_col;

    if (ctx->is_selecting) {
        if (sel_min_row > sel_max_row || (sel_min_row == sel_max_row && sel_min_col > sel_max_col)) {
            uint32_t tmp_r = sel_min_row; sel_min_row = sel_max_row; sel_max_row = tmp_r;
            uint32_t tmp_c = sel_min_col; sel_min_col = sel_max_col; sel_max_col = tmp_c;
        }
    }

    for (int i = 0; i < edit_area_height; ++i) {
        int physical_row = 3 + i;
        uint32_t line_index = ctx->viewport_top + (uint32_t)i;

        int plen = snprintf(pos_buf, sizeof(pos_buf), "\033[%d;1H", physical_row);
        rap_ui_buf_append(pos_buf, (size_t)plen);

        if (line_index < ctx->total_lines) {
            int nlen = snprintf(num_buf, sizeof(num_buf), RAP_ANSI_BOLD "%5u | " RAP_ANSI_RESET, line_index + 1);
            rap_ui_buf_append(num_buf, (size_t)nlen);

            uint32_t offset = ctx->lines[line_index].offset;
            uint16_t len    = ctx->lines[line_index].length;

            bool is_comment = rap_ui_is_comment_line(ctx, line_index);
            if (is_comment) {
                rap_ui_buf_append_str(RAP_ANSI_COMMENT);
            }

            uint32_t curr_vcol = 0;
            int printed_cols = 0;
            bool highlight_active = false;
            uint32_t byte_idx = 0;

            while (byte_idx < len && printed_cols < max_text_cols) {
                unsigned char ch = ctx->raw_buffer[offset + byte_idx];
                uint32_t seq_len = 1;

                if (ch >= 0x80) {
                    seq_len = rap_utf8_char_len(ch);
                    if (byte_idx + seq_len > len) {
                        seq_len = 1;
                    } else {
                        for (uint32_t k = 1; k < seq_len; ++k) {
                            if ((ctx->raw_buffer[offset + byte_idx + k] & 0xC0) != 0x80) {
                                seq_len = 1;
                                break;
                            }
                        }
                    }
                }

                bool in_selection = false;
                if (ctx->is_selecting) {
                    if (line_index > sel_min_row && line_index < sel_max_row) in_selection = true;
                    else if (line_index == sel_min_row && line_index == sel_max_row) {
                        if (byte_idx >= sel_min_col && byte_idx <= sel_max_col) in_selection = true;
                    } else if (line_index == sel_min_row && byte_idx >= sel_min_col) in_selection = true;
                    else if (line_index == sel_max_row && byte_idx <= sel_max_col) in_selection = true;
                }

                if (curr_vcol >= ctx->viewport_left) {
                    if (in_selection && !highlight_active) {
                        rap_ui_buf_append_str("\033[7m");
                        highlight_active = true;
                    } else if (!in_selection && highlight_active) {
                        rap_ui_buf_append_str(RAP_ANSI_RESET);
                        if (is_comment) {
                            rap_ui_buf_append_str(RAP_ANSI_COMMENT);
                        }
                        highlight_active = false;
                    }

                    /* Rendu explicite et sans fard des anomalies */
                    if (ch == '\r') {
                        /* Retour chariot Windows rendu visible sous la forme ^M en rouge */
                        rap_ui_buf_append_str(RAP_ANSI_RED "^M" RAP_ANSI_RESET);
                        if (is_comment) rap_ui_buf_append_str(RAP_ANSI_COMMENT);
                        printed_cols += 2;
                    } else if (ch == '\0') {
                        /* Octet nul rendu visible sous la forme ^@ en magenta */
                        rap_ui_buf_append_str(RAP_ANSI_MAGENTA "^@" RAP_ANSI_RESET);
                        if (is_comment) rap_ui_buf_append_str(RAP_ANSI_COMMENT);
                        printed_cols += 2;
                    } else if (ch == '\n') {
                        /* Fin de ligne Unix */
                    } else if (ch < 32 && ch != '\t') {
                        rap_ui_buf_append(".", 1);
                        printed_cols++;
                    } else {
                        rap_ui_buf_append((const char *)&ctx->raw_buffer[offset + byte_idx], seq_len);
                        printed_cols++;
                    }
                }

                curr_vcol++;
                byte_idx += seq_len;
            }

            if (highlight_active || is_comment) {
                rap_ui_buf_append_str(RAP_ANSI_RESET);
            }
        } else {
            rap_ui_buf_append_str(RAP_ANSI_CYAN "~" RAP_ANSI_RESET);
        }

        rap_ui_buf_append_str("\033[K");
    }

    int cmd_row = ctx->term_rows;
    int clen_pos = snprintf(pos_buf, sizeof(pos_buf), "\033[%d;1H", cmd_row);
    rap_ui_buf_append(pos_buf, (size_t)clen_pos);

    if (ctx->status_msg[0] != '\0') {
        rap_ui_buf_append_str(ctx->status_msg);
    } else if (ctx->in_command_mode) {
        rap_ui_buf_append_str(":");
    } else if (ctx->is_selecting) {
        rap_ui_buf_append_str(RAP_ANSI_YELLOW " [SÉLECTION ACTIVE] Flèches: Étendre | Ctrl+C: Copier | Ctrl+X: Couper | Échap: Annuler " RAP_ANSI_RESET);
    } else {
        rap_ui_buf_append_str(RAP_ANSI_REVERSE " [VISUEL] Ctrl+W: Onglet | :clean: Assainir | :%s/anc/nouv/g: Remplacer | :u: Undo " RAP_ANSI_RESET);
    }
    rap_ui_buf_append_str("\033[K");

    int screen_y = 2 + (int)(ctx->cursor_row - ctx->viewport_top) + 1;
    int screen_x = margin_cols + (int)(cursor_vcol - ctx->viewport_left) + 1;

    if (screen_x < margin_cols + 1) screen_x = margin_cols + 1;
    if (screen_x >= ctx->term_cols) screen_x = ctx->term_cols - 1;
    if (screen_y >= ctx->term_rows) screen_y = ctx->term_rows - 1;

    char cursor_pos[32];
    int clen = snprintf(cursor_pos, sizeof(cursor_pos), "\033[%d;%dH\033[?25h", screen_y, screen_x);
    rap_ui_buf_append(cursor_pos, (size_t)clen);

    write(STDOUT_FILENO, s_frame_buf, s_frame_len);
}

void rap_ui_show_help_screen(void) {
    printf("\033[H\033[2J");
    printf(RAP_ANSI_REVERSE " ============================= RAP-EDIT : AIDE ============================= " RAP_ANSI_RESET "\n\n");

    printf(RAP_ANSI_BOLD " 1. ONGLETS ET MULTI-DOCUMENTS :" RAP_ANSI_RESET "\n");
    printf("   :#1, :#2, :#3, :#4       : Bascule directe vers l'onglet 1, 2, 3 ou 4\n");
    printf("   Ctrl + W                 : Raccourci immédiat pour cycler entre les onglets\n");
    printf("   :p  /  :n                : Onglet précédent / suivant\n");
    printf("   :b                       : Affiche la liste des tampons ouverts et modifiés (*)\n");
    printf("   :bd  /  :bd!             : Ferme l'onglet actif (sécurisé / forcé)\n\n");

    printf(RAP_ANSI_BOLD " 2. ASSAINISSEMENT TACTIQUE ANTI-CORRUPTION :" RAP_ANSI_RESET "\n");
    printf("   :clean                   : Purge globale combinée (supprime ^M et ^@)\n");
    printf("   :dos2unix  (ou :d2u)     : Supprime les retours chariot Windows (^M / \\r)\n");
    printf("   :strip-null (ou :null)   : Élimine les octets nuls corrompus (^@ / \\0)\n");
    printf("   Rendu visuel             : ^M s'affiche en rouge, ^@ s'affiche en magenta\n");
    printf("   Annulation (:u)          : L'état initial reste restaurable instantanément\n\n");

    printf(RAP_ANSI_BOLD " 3. PRESSE-PAPIERS ET COLLAGE (3 MODES) :" RAP_ANSI_RESET "\n");
    printf("   Mode Visuel              : Shift + Flèches puis Ctrl+C / Ctrl+X / Ctrl+V\n");
    printf("   Mode CLI 2 étapes        : :10c (copie ligne 10) puis :15w ou :15pp (colle en 15)\n");
    printf("   Mode Macro directe       : :1:10c:15w (copie) | :1:10x:15w (déplace)\n");
    printf("   Presse-papiers unifié    : Copiez dans un fichier (#1), collez dans un autre (#2)\n\n");

    printf(RAP_ANSI_BOLD " 4. NAVIGATION, RECHERCHE ET ÉDITION :" RAP_ANSI_RESET "\n");
    printf("   Page-Up / Page-Down      : Défilement rapide d'une page écran complète\n");
    printf("   :s/anc/nouv/g            : Remplace sur la ligne courante (:%%s partout)\n");
    printf("   :<num>d / :<deb>:<fin>d  : Supprime une ligne ou une plage (ex: :2d, :3:90d)\n");
    printf("   :rch <motif>             : Recherche circulaire sans latence\n");
    printf("   :u  /  Ctrl + Z          : Annulation / Rétablissement instantané\n");
    printf("   :w!  /  :q!              : Sauvegarde atomique synchro (fsync) / Quitter\n\n");

    printf(RAP_ANSI_REVERSE " [Appuyez sur une touche pour revenir à l'éditeur...] " RAP_ANSI_RESET);
    fflush(stdout);

    char c;
    read(STDIN_FILENO, &c, 1);
}
