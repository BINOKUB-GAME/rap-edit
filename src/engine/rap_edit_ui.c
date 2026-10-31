/* ==============================================================================
 * SCRIPT      : src/engine/rap_edit_ui.c
 * RÉVISION    : Rev 4.6 (Absolute VT100 Row Positioning & Full Pager Screen Clear)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Correction du bug d'empilement horizontal de l'aide : positionnement
 *               absolu (\033[%d;1H\033[K) pour chaque rangée du pager BSS,
 *               nettoyage global de l'écran (\033[2J) et suppression des \r parasites.
 * FONCTIONS   :
 *   1. rap_ui_buf_append (Concaténation bornée dans le tampon BSS)
 *   2. rap_ui_buf_append_str (Concaténation rapide de chaînes statiques)
 *   3. rap_ui_is_comment_line (Détection déterministe d'une ligne de commentaire)
 *   4. rap_ui_update_dimensions (Interrogation des dimensions TTY via ioctl)
 *   5. rap_ui_render_screen (Rendu coordonné avec affichage UTF-8, ^M et ^@)
 *   6. rap_ui_load_help_file (Chargement BSS et expansion des balises couleur)
 *   7. rap_ui_index_help_lines (Indexation statique et décapage des \r résiduels)
 *   8. rap_ui_show_help_screen (Boucle du pager interactif avec positionnement absolu)
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
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include "rap_edit_types.h"
#include "rap_edit_config.h"
#include "rap_edit_hud.h"
#include "rap_edit_ops.h"
#include "rap_edit_term.h"

#define RAP_FRAME_BUF_SIZE (32 * 1024)
static char s_frame_buf[RAP_FRAME_BUF_SIZE];
static uint32_t s_frame_len = 0;

/* Tampons statiques BSS pour le pager d'aide (0-heap) */
#define RAP_HELP_RAW_SIZE      8192
#define RAP_HELP_RENDER_SIZE   16384
#define RAP_HELP_MAX_LINES     512

static char     s_help_raw[RAP_HELP_RAW_SIZE];
static char     s_help_rendered[RAP_HELP_RENDER_SIZE];
static uint32_t s_help_line_offsets[RAP_HELP_MAX_LINES];
static uint16_t s_help_line_lengths[RAP_HELP_MAX_LINES];
static uint32_t s_help_total_lines = 0;

/* 1. Concaténation bornée */
static void rap_ui_buf_append(const char *str, size_t len) {
    if (s_frame_len + len >= RAP_FRAME_BUF_SIZE) return;
    memcpy(&s_frame_buf[s_frame_len], str, len);
    s_frame_len += (uint32_t)len;
}

/* 2. Concaténation rapide */
static void rap_ui_buf_append_str(const char *str) {
    rap_ui_buf_append(str, strlen(str));
}

/* 3. Détection des lignes de commentaire */
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

/* 4. Dimensions TTY */
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

/* 5. Rendu principal de l'éditeur */
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
                        if (is_comment) rap_ui_buf_append_str(RAP_ANSI_COMMENT);
                        highlight_active = false;
                    }

                    if (ch == '\r') {
                        rap_ui_buf_append_str(RAP_ANSI_RED "^M" RAP_ANSI_RESET);
                        if (is_comment) rap_ui_buf_append_str(RAP_ANSI_COMMENT);
                        printed_cols += 2;
                    } else if (ch == '\0') {
                        rap_ui_buf_append_str(RAP_ANSI_MAGENTA "^@" RAP_ANSI_RESET);
                        if (is_comment) rap_ui_buf_append_str(RAP_ANSI_COMMENT);
                        printed_cols += 2;
                    } else if (ch == '\n') {
                        /* Fin de ligne */
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

/* 6. Chargement BSS et expansion des balises couleur */
static int rap_ui_load_help_file(void) {
    const char *paths[] = {
        "doc/rap_help.txt",
        "./rap_help.txt",
        "/usr/share/rap-edit/rap_help.txt",
        NULL
    };

    int fd = -1;
    for (int i = 0; paths[i] != NULL; ++i) {
        fd = open(paths[i], O_RDONLY);
        if (fd >= 0) break;
    }

    if (fd < 0) return -1;

    ssize_t bytes_read = read(fd, s_help_raw, sizeof(s_help_raw) - 1);
    close(fd);

    if (bytes_read <= 0) return -1;
    s_help_raw[bytes_read] = '\0';

    uint32_t r = 0, w = 0;
    while (r < (uint32_t)bytes_read && w + 32 < sizeof(s_help_rendered)) {
        if (s_help_raw[r] == '{') {
            if (strncmp(&s_help_raw[r], "{BOLD}", 6) == 0) {
                memcpy(&s_help_rendered[w], RAP_ANSI_BOLD, strlen(RAP_ANSI_BOLD));
                w += (uint32_t)strlen(RAP_ANSI_BOLD);
                r += 6;
                continue;
            } else if (strncmp(&s_help_raw[r], "{REV}", 5) == 0) {
                memcpy(&s_help_rendered[w], RAP_ANSI_REVERSE, strlen(RAP_ANSI_REVERSE));
                w += (uint32_t)strlen(RAP_ANSI_REVERSE);
                r += 5;
                continue;
            } else if (strncmp(&s_help_raw[r], "{CYAN}", 6) == 0) {
                memcpy(&s_help_rendered[w], RAP_ANSI_CYAN, strlen(RAP_ANSI_CYAN));
                w += (uint32_t)strlen(RAP_ANSI_CYAN);
                r += 6;
                continue;
            } else if (strncmp(&s_help_raw[r], "{YELLOW}", 8) == 0) {
                memcpy(&s_help_rendered[w], RAP_ANSI_YELLOW, strlen(RAP_ANSI_YELLOW));
                w += (uint32_t)strlen(RAP_ANSI_YELLOW);
                r += 8;
                continue;
            } else if (strncmp(&s_help_raw[r], "{RED}", 5) == 0) {
                memcpy(&s_help_rendered[w], RAP_ANSI_RED, strlen(RAP_ANSI_RED));
                w += (uint32_t)strlen(RAP_ANSI_RED);
                r += 5;
                continue;
            } else if (strncmp(&s_help_raw[r], "{MAGENTA}", 9) == 0) {
                memcpy(&s_help_rendered[w], RAP_ANSI_MAGENTA, strlen(RAP_ANSI_MAGENTA));
                w += (uint32_t)strlen(RAP_ANSI_MAGENTA);
                r += 9;
                continue;
            } else if (strncmp(&s_help_raw[r], "{RESET}", 7) == 0) {
                memcpy(&s_help_rendered[w], RAP_ANSI_RESET, strlen(RAP_ANSI_RESET));
                w += (uint32_t)strlen(RAP_ANSI_RESET);
                r += 7;
                continue;
            }
        }
        s_help_rendered[w++] = s_help_raw[r++];
    }
    s_help_rendered[w] = '\0';

    return 0;
}

/* 7. Indexation statique et décapage des \r résiduels */
static void rap_ui_index_help_lines(void) {
    s_help_total_lines = 0;
    uint32_t off = 0;
    uint32_t len = (uint32_t)strlen(s_help_rendered);

    while (off < len && s_help_total_lines < RAP_HELP_MAX_LINES) {
        s_help_line_offsets[s_help_total_lines] = off;
        uint32_t cur = off;
        while (cur < len && s_help_rendered[cur] != '\n') {
            cur++;
        }

        uint32_t line_len = cur - off;
        if (line_len > 0 && s_help_rendered[off + line_len - 1] == '\r') {
            line_len--;
        }

        s_help_line_lengths[s_help_total_lines] = (uint16_t)line_len;
        s_help_total_lines++;
        off = cur + 1;
    }
}

/* 8. Boucle du pager interactif avec positionnement absolu */
void rap_ui_show_help_screen(void) {
    if (rap_ui_load_help_file() != 0) {
        write(STDOUT_FILENO, "\033[H\033[2J", 7);
        printf(RAP_ANSI_BOLD "[!] Impossible de charger le fichier doc/rap_help.txt" RAP_ANSI_RESET "\n\n");
        printf("Appuyez sur une touche pour revenir...");
        fflush(stdout);
        char dummy;
        read(STDIN_FILENO, &dummy, 1);
        write(STDOUT_FILENO, "\033[H\033[2J", 7);
        return;
    }

    rap_ui_index_help_lines();

    uint32_t top_line = 0;

    /* Effacement complet initial */
    write(STDOUT_FILENO, "\033[H\033[2J\033[?25l", 13);

    while (1) {
        int rows = 24;
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 5) {
            rows = (int)ws.ws_row;
        }

        int view_height = rows - 2;
        if (view_height < 1) view_height = 1;

        uint32_t max_top = 0;
        if (s_help_total_lines > (uint32_t)view_height) {
            max_top = s_help_total_lines - (uint32_t)view_height;
        }
        if (top_line > max_top) top_line = max_top;

        /* Rendu ligne par ligne avec coordonnées VT100 absolues */
        char pos_buf[32];
        for (int r = 0; r < view_height; ++r) {
            int physical_row = r + 1;
            int plen = snprintf(pos_buf, sizeof(pos_buf), "\033[%d;1H\033[K", physical_row);
            write(STDOUT_FILENO, pos_buf, (size_t)plen);

            uint32_t l_idx = top_line + (uint32_t)r;
            if (l_idx < s_help_total_lines) {
                uint32_t off = s_help_line_offsets[l_idx];
                uint16_t len = s_help_line_lengths[l_idx];
                write(STDOUT_FILENO, &s_help_rendered[off], len);
                write(STDOUT_FILENO, RAP_ANSI_RESET, strlen(RAP_ANSI_RESET));
            }
        }

        /* Ligne de séparation avant la barre d'état */
        if (rows > 2) {
            int sep_row = rows - 1;
            int plen = snprintf(pos_buf, sizeof(pos_buf), "\033[%d;1H\033[K", sep_row);
            write(STDOUT_FILENO, pos_buf, (size_t)plen);
        }

        /* Barre d'état du pager sur la dernière rangée */
        char bar_buf[192];
        uint32_t end_line = top_line + (uint32_t)view_height;
        if (end_line > s_help_total_lines) end_line = s_help_total_lines;

        int blen = snprintf(bar_buf, sizeof(bar_buf),
                            "\033[%d;1H" RAP_ANSI_REVERSE
                            " [AIDE RAP-EDIT] Lignes %u-%u/%u | Flèches/PgUp/PgDn: Défiler | q/Échap: Quitter "
                            RAP_ANSI_RESET "\033[K",
                            rows, top_line + 1, end_line, s_help_total_lines);
        write(STDOUT_FILENO, bar_buf, (size_t)blen);

        /* Capture clavier */
        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n <= 0) break;

        if (c == 'q' || c == 'Q' || c == '\r' || c == '\n') {
            break;
        }

        if (c == '\033') {
            if (!rap_term_has_input(35)) {
                break;
            }

            char seq[8];
            int si = 0;
            seq[si++] = '\033';
            while (si < 7 && rap_term_has_input(20)) {
                if (read(STDIN_FILENO, &seq[si], 1) <= 0) break;
                si++;
            }
            seq[si] = '\0';

            /* Page-Up / Page-Down (\033[5~ / \033[6~) */
            if (si == 4 && seq[1] == '[' && seq[3] == '~') {
                if (seq[2] == '5') {
                    if (top_line > (uint32_t)view_height) top_line -= (uint32_t)view_height;
                    else top_line = 0;
                } else if (seq[2] == '6') {
                    if (top_line + (uint32_t)view_height < max_top) top_line += (uint32_t)view_height;
                    else top_line = max_top;
                }
                continue;
            }

            /* Flèches Haut (\033[A) / Bas (\033[B) */
            if (si == 3 && seq[1] == '[') {
                if (seq[2] == 'A') {
                    if (top_line > 0) top_line--;
                } else if (seq[2] == 'B') {
                    if (top_line < max_top) top_line++;
                }
                continue;
            }
            continue;
        }

        if (c == 'j') {
            if (top_line < max_top) top_line++;
        } else if (c == 'k') {
            if (top_line > 0) top_line--;
        } else if (c == ' ') {
            if (top_line + (uint32_t)view_height < max_top) top_line += (uint32_t)view_height;
            else top_line = max_top;
        } else if (c == 'b') {
            if (top_line > (uint32_t)view_height) top_line -= (uint32_t)view_height;
            else top_line = 0;
        }
    }

    /* Nettoyage propre de l'écran avant retour à l'éditeur */
    write(STDOUT_FILENO, "\033[H\033[2J", 7);
}
