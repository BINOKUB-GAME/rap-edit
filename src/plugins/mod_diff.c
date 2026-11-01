/* ==============================================================================
 * SCRIPT      : src/plugins/mod_diff.c
 * RÉVISION    : Rev 1.1 (High-Contrast TTY Badges & Exact ANSI Byte-Length Fix)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Correction du contraste TTY avec badges explicites blanc-sur-rouge
 *               (\033[41;1;37m) et blanc-sur-vert (\033[42;1;37m). Élimination
 *               du bug de troncature d'écriture sur le symbole de suppression (-).
 * FONCTIONS   :
 *   1. rap_diff_load_disk_file (Lecture synchrone d'un fichier dans le tampon BSS)
 *   2. rap_diff_index_ref_lines (Indexation statique des lignes du tampon miroir)
 *   3. rap_diff_lines_are_equal (Comparaison stricte du contenu textuel de deux lignes)
 *   4. rap_diff_compute_deltas (Algorithme d'alignement différentiel avec lookahead)
 *   5. rap_diff_show_pager (Visionneur interactif VT100 haute lisibilité TTY)
 *   6. rap_diff_execute (Analyse des arguments CLI et aiguillage de la comparaison)
 *   7. rap_cmd_diff_plugin_entry (Point d'entrée du greffon pour le dispatcheur BSS)
 *   8. rap_plugin_mod_diff_init (Enregistrement persistant dans la table des greffons)
 * ============================================================================== */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include "rap_edit_diff.h"
#include "rap_edit_config.h"
#include "rap_edit_plugin.h"
#include "rap_edit_buffer.h"
#include "rap_edit_term.h"

#define RAP_DIFF_MAX_ROWS 16384
#define RAP_DIFF_LOOKAHEAD_WINDOW 64

/* Séquences haute lisibilité pour console TTY et terminaux graphiques */
#define RAP_DIFF_BADGE_RED    "\033[41;1;37m"  /* Fond rouge, texte blanc vif */
#define RAP_DIFF_BADGE_GREEN  "\033[42;1;37m"  /* Fond vert, texte blanc vif */
#define RAP_DIFF_LINE_ADD     "\033[1;32m+ \033[0m"
#define RAP_DIFF_LINE_DEL     "\033[1;31m- \033[0m"

typedef struct {
    char     type;       /* ' ' = identique, '+' = ajout, '-' = suppression */
    uint8_t  source;     /* 0 = référence/disque, 1 = RAM active */
    uint32_t line_num;   /* Numéro de ligne d'origine */
    uint32_t offset;     /* Décalage dans le tampon brut */
    uint16_t length;     /* Longueur de la ligne */
} rap_diff_row_t;

/* Tampons statiques BSS (0-heap) */
static uint8_t        s_diff_ref_buf[RAP_STATIC_BUFFER_SIZE];
static uint32_t       s_diff_ref_len = 0;
static rap_line_t     s_diff_ref_lines[RAP_MAX_LINES];
static uint32_t       s_diff_ref_total_lines = 0;

static rap_diff_row_t s_diff_rows[RAP_DIFF_MAX_ROWS];
static uint32_t       s_diff_total_rows = 0;
static uint32_t       s_diff_count_add = 0;
static uint32_t       s_diff_count_del = 0;

/* 1. Chargement synchrone d'un fichier externe en mémoire statique */
static int rap_diff_load_disk_file(const char *path) {
    if (!path || path[0] == '\0') return -1;

    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    s_diff_ref_len = 0;
    while (s_diff_ref_len < RAP_STATIC_BUFFER_SIZE) {
        ssize_t n = read(fd, &s_diff_ref_buf[s_diff_ref_len], RAP_STATIC_BUFFER_SIZE - s_diff_ref_len);
        if (n < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return -2;
        }
        if (n == 0) break;
        s_diff_ref_len += (uint32_t)n;
    }
    close(fd);
    return 0;
}

/* 2. Indexation statique des lignes du tampon miroir BSS */
static void rap_diff_index_ref_lines(void) {
    s_diff_ref_total_lines = 0;
    uint32_t off = 0;

    while (off < s_diff_ref_len && s_diff_ref_total_lines < RAP_MAX_LINES) {
        s_diff_ref_lines[s_diff_ref_total_lines].offset = off;
        uint32_t cur = off;
        while (cur < s_diff_ref_len && s_diff_ref_buf[cur] != '\n') {
            cur++;
        }

        uint32_t line_len = cur - off;
        if (line_len > 0 && s_diff_ref_buf[off + line_len - 1] == '\r') {
            line_len--;
        }

        s_diff_ref_lines[s_diff_ref_total_lines].length = (uint16_t)line_len;
        s_diff_ref_lines[s_diff_ref_total_lines].modified = false;
        s_diff_ref_total_lines++;

        if (cur < s_diff_ref_len && s_diff_ref_buf[cur] == '\n') {
            off = cur + 1;
        } else {
            off = cur;
        }
    }
}

/* 3. Comparaison stricte du contenu de deux lignes */
static bool rap_diff_lines_are_equal(const uint8_t *buf_a, const rap_line_t *la,
                                     const uint8_t *buf_b, const rap_line_t *lb) {
    uint16_t len_a = la->length;
    uint16_t len_b = lb->length;

    if (len_a > 0 && buf_a[la->offset + len_a - 1] == '\r') len_a--;
    if (len_b > 0 && buf_b[lb->offset + len_b - 1] == '\r') len_b--;

    if (len_a != len_b) return false;
    if (len_a == 0) return true;

    return (memcmp(&buf_a[la->offset], &buf_b[lb->offset], len_a) == 0);
}

/* 4. Alignement différentiel avec lookahead */
static void rap_diff_compute_deltas(const uint8_t *buf_a, const rap_line_t *lines_a, uint32_t count_a,
                                    const uint8_t *buf_b, const rap_line_t *lines_b, uint32_t count_b) {
    s_diff_total_rows = 0;
    s_diff_count_add = 0;
    s_diff_count_del = 0;

    uint32_t ia = 0, ib = 0;

    while ((ia < count_a || ib < count_b) && s_diff_total_rows + 4 < RAP_DIFF_MAX_ROWS) {
        if (ia < count_a && ib < count_b) {
            if (rap_diff_lines_are_equal(buf_a, &lines_a[ia], buf_b, &lines_b[ib])) {
                s_diff_rows[s_diff_total_rows].type = ' ';
                s_diff_rows[s_diff_total_rows].source = 1;
                s_diff_rows[s_diff_total_rows].line_num = ib + 1;
                s_diff_rows[s_diff_total_rows].offset = lines_b[ib].offset;
                s_diff_rows[s_diff_total_rows].length = lines_b[ib].length;
                s_diff_total_rows++;
                ia++;
                ib++;
                continue;
            }

            int match_b_at = -1;
            int match_a_at = -1;

            for (uint32_t w = 1; w <= RAP_DIFF_LOOKAHEAD_WINDOW; ++w) {
                if (match_b_at < 0 && ib + w < count_b) {
                    if (rap_diff_lines_are_equal(buf_a, &lines_a[ia], buf_b, &lines_b[ib + w])) {
                        match_b_at = (int)(ib + w);
                    }
                }
                if (match_a_at < 0 && ia + w < count_a) {
                    if (rap_diff_lines_are_equal(buf_a, &lines_a[ia + w], buf_b, &lines_b[ib])) {
                        match_a_at = (int)(ia + w);
                    }
                }
                if (match_b_at >= 0 || match_a_at >= 0) break;
            }

            if (match_b_at >= 0 && (match_a_at < 0 || (match_b_at - (int)ib) <= (match_a_at - (int)ia))) {
                while (ib < (uint32_t)match_b_at && s_diff_total_rows < RAP_DIFF_MAX_ROWS) {
                    s_diff_rows[s_diff_total_rows].type = '+';
                    s_diff_rows[s_diff_total_rows].source = 1;
                    s_diff_rows[s_diff_total_rows].line_num = ib + 1;
                    s_diff_rows[s_diff_total_rows].offset = lines_b[ib].offset;
                    s_diff_rows[s_diff_total_rows].length = lines_b[ib].length;
                    s_diff_total_rows++;
                    s_diff_count_add++;
                    ib++;
                }
            } else if (match_a_at >= 0) {
                while (ia < (uint32_t)match_a_at && s_diff_total_rows < RAP_DIFF_MAX_ROWS) {
                    s_diff_rows[s_diff_total_rows].type = '-';
                    s_diff_rows[s_diff_total_rows].source = 0;
                    s_diff_rows[s_diff_total_rows].line_num = ia + 1;
                    s_diff_rows[s_diff_total_rows].offset = lines_a[ia].offset;
                    s_diff_rows[s_diff_total_rows].length = lines_a[ia].length;
                    s_diff_total_rows++;
                    s_diff_count_del++;
                    ia++;
                }
            } else {
                s_diff_rows[s_diff_total_rows].type = '-';
                s_diff_rows[s_diff_total_rows].source = 0;
                s_diff_rows[s_diff_total_rows].line_num = ia + 1;
                s_diff_rows[s_diff_total_rows].offset = lines_a[ia].offset;
                s_diff_rows[s_diff_total_rows].length = lines_a[ia].length;
                s_diff_total_rows++;
                s_diff_count_del++;
                ia++;

                if (s_diff_total_rows < RAP_DIFF_MAX_ROWS) {
                    s_diff_rows[s_diff_total_rows].type = '+';
                    s_diff_rows[s_diff_total_rows].source = 1;
                    s_diff_rows[s_diff_total_rows].line_num = ib + 1;
                    s_diff_rows[s_diff_total_rows].offset = lines_b[ib].offset;
                    s_diff_rows[s_diff_total_rows].length = lines_b[ib].length;
                    s_diff_total_rows++;
                    s_diff_count_add++;
                    ib++;
                }
            }
        } else if (ia < count_a) {
            s_diff_rows[s_diff_total_rows].type = '-';
            s_diff_rows[s_diff_total_rows].source = 0;
            s_diff_rows[s_diff_total_rows].line_num = ia + 1;
            s_diff_rows[s_diff_total_rows].offset = lines_a[ia].offset;
            s_diff_rows[s_diff_total_rows].length = lines_a[ia].length;
            s_diff_total_rows++;
            s_diff_count_del++;
            ia++;
        } else if (ib < count_b) {
            s_diff_rows[s_diff_total_rows].type = '+';
            s_diff_rows[s_diff_total_rows].source = 1;
            s_diff_rows[s_diff_total_rows].line_num = ib + 1;
            s_diff_rows[s_diff_total_rows].offset = lines_b[ib].offset;
            s_diff_rows[s_diff_total_rows].length = lines_b[ib].length;
            s_diff_total_rows++;
            s_diff_count_add++;
            ib++;
        }
    }
}

/* 5. Visionneur interactif VT100 haute lisibilité TTY */
static void rap_diff_show_pager(const char *label_a, const char *label_b,
                                const uint8_t *buf_a, const uint8_t *buf_b) {
    write(STDOUT_FILENO, "\033[H\033[2J\033[?25l", 13);

    uint32_t top_line = 0;

    while (1) {
        int rows = 24, cols = 80;
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
            if (ws.ws_row > 5) rows = (int)ws.ws_row;
            if (ws.ws_col > 20) cols = (int)ws.ws_col;
        }

        int view_height = rows - 3;
        if (view_height < 1) view_height = 1;

        uint32_t max_top = 0;
        if (s_diff_total_rows > (uint32_t)view_height) {
            max_top = s_diff_total_rows - (uint32_t)view_height;
        }
        if (top_line > max_top) top_line = max_top;

        /* En-tête avec badges haute intensité (blanc sur rouge / blanc sur vert) */
        char hdr_buf[256];
        int hlen = snprintf(hdr_buf, sizeof(hdr_buf),
                            "\033[1;1H" RAP_ANSI_REVERSE " DIFF TACTIQUE : " RAP_ANSI_RESET
                            " " RAP_DIFF_BADGE_RED " (-) %-.32s " RAP_ANSI_RESET
                            RAP_ANSI_REVERSE " vs " RAP_ANSI_RESET
                            RAP_DIFF_BADGE_GREEN " (+) %-.32s " RAP_ANSI_RESET "\033[K",
                            label_a, label_b);
        write(STDOUT_FILENO, hdr_buf, (size_t)hlen);

        /* Rendu des lignes avec sécurité d'écriture */
        char row_buf[32];
        for (int r = 0; r < view_height; ++r) {
            int phys_row = r + 2;
            int plen = snprintf(row_buf, sizeof(row_buf), "\033[%d;1H\033[K", phys_row);
            write(STDOUT_FILENO, row_buf, (size_t)plen);

            uint32_t idx = top_line + (uint32_t)r;
            if (idx < s_diff_total_rows) {
                char t = s_diff_rows[idx].type;
                const uint8_t *src_buf = (s_diff_rows[idx].source == 0) ? buf_a : buf_b;
                uint32_t off = s_diff_rows[idx].offset;
                uint16_t len = s_diff_rows[idx].length;

                if (t == '+') {
                    write(STDOUT_FILENO, RAP_DIFF_LINE_ADD, strlen(RAP_DIFF_LINE_ADD));
                } else if (t == '-') {
                    write(STDOUT_FILENO, RAP_DIFF_LINE_DEL, strlen(RAP_DIFF_LINE_DEL));
                } else {
                    write(STDOUT_FILENO, "  ", 2);
                }

                char num_buf[16];
                int nlen = snprintf(num_buf, sizeof(num_buf), "%5u | ", s_diff_rows[idx].line_num);
                write(STDOUT_FILENO, num_buf, (size_t)nlen);

                int max_text = cols - 12;
                if (max_text < 1) max_text = 1;
                uint16_t print_len = (len > (uint16_t)max_text) ? (uint16_t)max_text : len;

                if (print_len > 0) {
                    write(STDOUT_FILENO, &src_buf[off], print_len);
                }
                write(STDOUT_FILENO, RAP_ANSI_RESET, strlen(RAP_ANSI_RESET));
            }
        }

        /* Séparateur d'état */
        if (rows > 2) {
            int sep_row = rows - 1;
            int plen = snprintf(row_buf, sizeof(row_buf), "\033[%d;1H\033[K", sep_row);
            write(STDOUT_FILENO, row_buf, (size_t)plen);
        }

        /* Barre d'état inférieure : badges nets blanc-sur-vert et blanc-sur-rouge */
        char bar_buf[256];
        uint32_t end_line = top_line + (uint32_t)view_height;
        if (end_line > s_diff_total_rows) end_line = s_diff_total_rows;

        int blen = snprintf(bar_buf, sizeof(bar_buf),
                            "\033[%d;1H" RAP_ANSI_REVERSE
                            " [DIFF] Lignes %u-%u/%u | " RAP_ANSI_RESET
                            RAP_DIFF_BADGE_GREEN " +%u " RAP_ANSI_RESET RAP_ANSI_REVERSE " " RAP_ANSI_RESET
                            RAP_DIFF_BADGE_RED " -%u " RAP_ANSI_RESET RAP_ANSI_REVERSE
                            " | Flèches/PgUp/PgDn: Défiler | q/Échap: Revenir " RAP_ANSI_RESET "\033[K",
                            rows, top_line + 1, end_line, s_diff_total_rows,
                            s_diff_count_add, s_diff_count_del);
        write(STDOUT_FILENO, bar_buf, (size_t)blen);

        /* Gestion des entrées clavier */
        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n <= 0) break;

        if (c == 'q' || c == 'Q' || c == '\r' || c == '\n') {
            break;
        }

        if (c == '\033') {
            if (!rap_term_has_input(35)) break;

            char seq[8];
            int si = 0;
            seq[si++] = '\033';
            while (si < 7 && rap_term_has_input(20)) {
                if (read(STDIN_FILENO, &seq[si], 1) <= 0) break;
                si++;
            }
            seq[si] = '\0';

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

    write(STDOUT_FILENO, "\033[H\033[2J", 7);
}

/* 6. Parseur CLI pour la commande :diff */
int rap_diff_execute(rap_context_t *ctx, const char *args) {
    if (!ctx) return -1;

    while (args && *args == ' ') args++;

    if (args && args[0] == '#' && args[1] >= '1' && args[1] <= '4' && args[2] == '\0') {
        uint32_t target_idx = (uint32_t)(args[1] - '1');
        if (target_idx >= rap_buffer_get_count()) {
            snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                     RAP_ANSI_YELLOW "[!] Onglet #%u inexistant." RAP_ANSI_RESET, target_idx + 1);
            return -1;
        }

        rap_context_t *target_ctx = rap_buffer_get(target_idx);
        if (!target_ctx) return -1;

        char label_a[64], label_b[64];
        snprintf(label_a, sizeof(label_a), "Onglet #%u (%.24s)", target_idx + 1, target_ctx->file_path);
        snprintf(label_b, sizeof(label_b), "Onglet #%u (RAM Active)", rap_buffer_get_active_idx() + 1);

        rap_diff_compute_deltas(target_ctx->raw_buffer, target_ctx->lines, target_ctx->total_lines,
                                ctx->raw_buffer, ctx->lines, ctx->total_lines);

        rap_diff_show_pager(label_a, label_b, target_ctx->raw_buffer, ctx->raw_buffer);
        return 0;
    }

    char ref_path[RAP_MAX_PATH_LEN + 32];
    bool is_orig_bak = false;

    if (!args || *args == '\0' || strcmp(args, "orig") == 0 || strcmp(args, "disk") == 0) {
        if (ctx->file_path[0] == '\0') {
            snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                     RAP_ANSI_YELLOW "[!] Aucun fichier associé au tampon." RAP_ANSI_RESET);
            return -1;
        }

        snprintf(ref_path, sizeof(ref_path), "%s.orig.bak", ctx->file_path);
        if (access(ref_path, R_OK) == 0) {
            is_orig_bak = true;
        } else {
            strncpy(ref_path, ctx->file_path, sizeof(ref_path) - 1);
            ref_path[sizeof(ref_path) - 1] = '\0';
        }
    } else {
        strncpy(ref_path, args, sizeof(ref_path) - 1);
        ref_path[sizeof(ref_path) - 1] = '\0';
    }

    if (access(ref_path, R_OK) != 0) {
        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                 RAP_ANSI_YELLOW "[!] Fichier de référence inaccessible : '%.32s'" RAP_ANSI_RESET, ref_path);
        return -1;
    }

    if (rap_diff_load_disk_file(ref_path) != 0) {
        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                 RAP_ANSI_RED "[!] Échec de lecture de '%.32s'" RAP_ANSI_RESET, ref_path);
        return -1;
    }

    rap_diff_index_ref_lines();

    char label_ref[64];
    if (is_orig_bak) {
        snprintf(label_ref, sizeof(label_ref), "Clone (.orig.bak)");
    } else {
        snprintf(label_ref, sizeof(label_ref), "Disque (%.24s)", ref_path);
    }

    rap_diff_compute_deltas(s_diff_ref_buf, s_diff_ref_lines, s_diff_ref_total_lines,
                            ctx->raw_buffer, ctx->lines, ctx->total_lines);

    rap_diff_show_pager(label_ref, "RAM Active", s_diff_ref_buf, ctx->raw_buffer);
    return 0;
}

/* 7. Point d'entrée greffon */
static int rap_cmd_diff_plugin_entry(rap_context_t *ctx, const char *args) {
    return rap_diff_execute(ctx, args);
}

/* 8. Enregistrement en table BSS */
int rap_plugin_mod_diff_init(void) {
    static rap_plugin_t plug_diff = {
        .cmd_trigger = "diff",
        .description = "Comparaison in-situ 0-heap (:diff orig, :diff #n, :diff <fich>)",
        .execute     = rap_cmd_diff_plugin_entry
    };

    rap_plugin_register(&plug_diff);
    return 0;
}
