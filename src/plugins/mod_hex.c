/* ==============================================================================
 * SCRIPT      : src/plugins/mod_hex.c
 * RÉVISION    : Rev 1.1 (Adaptive 16/32-Byte Wide Screen Layout & Full HUD Span)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Adaptation automatique de la largeur selon la taille du terminal.
 *               Mode large 32 octets/ligne dès cols >= 130, repli à 16 octets en TTY.
 *               Bascule interactive 16/32 octets via touche Tab. Extension complète
 *               des bandeaux HUD sur toute la largeur du terminal (\033[K avant reset).
 * FONCTIONS   :
 *   1. rap_hex_format_ascii_char (Filtrage et conversion sécurisée d'un octet en ASCII)
 *   2. rap_hex_render_line (Formatage d'une rangée 16 ou 32 octets avec alignement strict)
 *   3. rap_hex_show_pager (Visionneur interactif VT100 avec défilement et bascule Tab)
 *   4. rap_hex_execute (Parseur de commande :hex et aiguillage d'offset initial)
 *   5. rap_cmd_hex_plugin_entry (Point d'entrée du greffon pour le dispatcheur BSS)
 *   6. rap_plugin_mod_hex_init (Enregistrement persistant dans la table des greffons)
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
#include <ctype.h>
#include <sys/ioctl.h>

#include "rap_edit_hex.h"
#include "rap_edit_config.h"
#include "rap_edit_plugin.h"
#include "rap_edit_term.h"

/* 1. Filtrage et conversion sécurisée d'un octet en caractère ASCII imprimable */
static char rap_hex_format_ascii_char(uint8_t byte) {
    if (byte >= 32 && byte <= 126) {
        return (char)byte;
    }
    return '.';
}

/* 2. Formatage d'une rangée complète (16 ou 32 octets) dans le tampon de ligne */
static int rap_hex_render_line(char *dest, size_t max_size, uint32_t offset,
                              const uint8_t *buffer, uint32_t buffer_len, uint32_t bytes_per_row) {
    if (!dest || max_size < 120) return 0;

    int written = snprintf(dest, max_size, "\033[36m%08X:\033[0m  ", offset);
    if (written < 0 || (size_t)written >= max_size) return 0;

    /* A. Rendu de la matrice hexadécimale */
    for (uint32_t i = 0; i < bytes_per_row; ++i) {
        uint32_t byte_off = offset + i;

        if (byte_off < buffer_len) {
            uint8_t b = buffer[byte_off];
            int n = 0;
            if (b == 0x00) {
                n = snprintf(dest + written, max_size - written, "\033[2m00\033[0m ");
            } else if (b == 0x0D || b == 0x0A) {
                n = snprintf(dest + written, max_size - written, "\033[33m%02X\033[0m ", b);
            } else {
                n = snprintf(dest + written, max_size - written, "%02X ", b);
            }
            if (n > 0) written += n;
        } else {
            int n = snprintf(dest + written, max_size - written, "   ");
            if (n > 0) written += n;
        }

        /* Espace supplémentaire de séparation entre blocs de 4 octets */
        if ((i + 1) % 4 == 0 && i < bytes_per_row - 1) {
            if ((size_t)written + 1 < max_size) {
                dest[written++] = ' ';
                dest[written] = '\0';
            }
        }

        /* Séparation médiane accentuée à 16 octets en mode 32 octets */
        if (bytes_per_row == 32 && i == 15) {
            if ((size_t)written + 2 < max_size) {
                dest[written++] = ' ';
                dest[written] = '\0';
            }
        }
    }

    /* B. Rendu de la colonne ASCII */
    if ((size_t)written + 4 < max_size) {
        dest[written++] = ' ';
        dest[written++] = '|';
        dest[written] = '\0';
    }

    for (uint32_t i = 0; i < bytes_per_row; ++i) {
        if (bytes_per_row == 32 && i == 16) {
            if ((size_t)written + 1 < max_size) {
                dest[written++] = ' ';
            }
        }

        uint32_t byte_off = offset + i;
        if (byte_off < buffer_len) {
            uint8_t b = buffer[byte_off];
            char c = rap_hex_format_ascii_char(b);
            if ((size_t)written + 1 < max_size) {
                dest[written++] = c;
            }
        } else {
            if ((size_t)written + 1 < max_size) {
                dest[written++] = ' ';
            }
        }
    }

    if ((size_t)written + 7 < max_size) {
        dest[written++] = '|';
        dest[written++] = '\033';
        dest[written++] = '[';
        dest[written++] = 'K';
        dest[written] = '\0';
    }

    return written;
}

/* 3. Visionneur interactif VT100 avec support écran large et bascule Tab */
static void rap_hex_show_pager(rap_context_t *ctx, uint32_t start_offset) {
    if (!ctx) return;

    write(STDOUT_FILENO, "\033[H\033[2J\033[?25l", 13);

    /* Détection initiale de la géométrie du terminal */
    int term_rows = 24, term_cols = 80;
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        if (ws.ws_row > 5) term_rows = (int)ws.ws_row;
        if (ws.ws_col > 20) term_cols = (int)ws.ws_col;
    }

    /* 32 octets par rangée sur terminaux larges (>= 130 cols), sinon 16 */
    uint32_t bytes_per_row = (term_cols >= 130) ? 32 : 16;
    uint32_t top_row = start_offset / bytes_per_row;

    char line_buf[1024];

    while (1) {
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
            if (ws.ws_row > 5) term_rows = (int)ws.ws_row;
            if (ws.ws_col > 20) term_cols = (int)ws.ws_col;
        }

        uint32_t total_rows = (ctx->buffer_len + bytes_per_row - 1) / bytes_per_row;
        if (total_rows == 0) total_rows = 1;

        int view_height = term_rows - 3;
        if (view_height < 1) view_height = 1;

        uint32_t max_top = 0;
        if (total_rows > (uint32_t)view_height) {
            max_top = total_rows - (uint32_t)view_height;
        }
        if (top_row > max_top) top_row = max_top;

        /* --- Rangée 1 : En-tête Cockpit pleine largeur (\033[K avant reset) --- */
        char hdr_buf[512];
        int hlen = snprintf(hdr_buf, sizeof(hdr_buf),
                            "\033[1;1H" RAP_ANSI_REVERSE
                            " RAP-EDIT | INSPECTEUR HEXADÉCIMAL : %-.48s | TAILLE: %u octets | FORMAT: %u o/ligne "
                            "\033[K" RAP_ANSI_RESET,
                            (ctx->file_path[0] ? ctx->file_path : "<SANS NOM>"),
                            ctx->buffer_len,
                            bytes_per_row);
        write(STDOUT_FILENO, hdr_buf, (size_t)hlen);

        /* --- Rangées 2 à view_height + 1 : Données hexadécimales --- */
        for (int r = 0; r < view_height; ++r) {
            int phys_row = r + 2;
            char pos_buf[32];
            int plen = snprintf(pos_buf, sizeof(pos_buf), "\033[%d;1H\033[K", phys_row);
            write(STDOUT_FILENO, pos_buf, (size_t)plen);

            uint32_t cur_row = top_row + (uint32_t)r;
            if (cur_row < total_rows) {
                uint32_t off = cur_row * bytes_per_row;
                int llen = rap_hex_render_line(line_buf, sizeof(line_buf), off,
                                               ctx->raw_buffer, ctx->buffer_len, bytes_per_row);
                if (llen > 0) {
                    write(STDOUT_FILENO, line_buf, (size_t)llen);
                }
            }
        }

        /* Ligne de séparation */
        if (term_rows > 2) {
            char sep_buf[32];
            int slen = snprintf(sep_buf, sizeof(sep_buf), "\033[%d;1H\033[K", term_rows - 1);
            write(STDOUT_FILENO, sep_buf, (size_t)slen);
        }

        /* --- Rangée inférieure : Barre d'état pleine largeur (\033[K avant reset) --- */
        uint32_t cur_offset_start = top_row * bytes_per_row;
        uint32_t cur_offset_end = (top_row + (uint32_t)view_height) * bytes_per_row;
        if (cur_offset_end > ctx->buffer_len) cur_offset_end = ctx->buffer_len;

        char bar_buf[512];
        int blen = snprintf(bar_buf, sizeof(bar_buf),
                            "\033[%d;1H" RAP_ANSI_REVERSE
                            " [HEX] Offset 0x%08X-0x%08X (%u/%u o) | Ligne %u/%u | "
                            "Flèches/PgUp/PgDn: Défiler | Tab: %u o/l | q/Échap: Quitter "
                            "\033[K" RAP_ANSI_RESET,
                            term_rows, cur_offset_start, cur_offset_end,
                            cur_offset_end, ctx->buffer_len,
                            top_row + 1, total_rows,
                            (bytes_per_row == 16) ? 32 : 16);
        write(STDOUT_FILENO, bar_buf, (size_t)blen);

        /* Capture clavier */
        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n <= 0) break;

        if (c == 'q' || c == 'Q' || c == '\r' || c == '\n') {
            break;
        }

        /* Bascule interactive 16 octets <-> 32 octets via touche Tab */
        if (c == '\t') {
            uint32_t current_abs_offset = top_row * bytes_per_row;
            bytes_per_row = (bytes_per_row == 16) ? 32 : 16;
            top_row = current_abs_offset / bytes_per_row;
            continue;
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

            /* Page-Up / Page-Down */
            if (si == 4 && seq[1] == '[' && seq[3] == '~') {
                if (seq[2] == '5') {
                    if (top_row > (uint32_t)view_height) top_row -= (uint32_t)view_height;
                    else top_row = 0;
                } else if (seq[2] == '6') {
                    if (top_row + (uint32_t)view_height < max_top) top_row += (uint32_t)view_height;
                    else top_row = max_top;
                }
                continue;
            }

            /* Flèches Haut / Bas */
            if (si == 3 && seq[1] == '[') {
                if (seq[2] == 'A') {
                    if (top_row > 0) top_row--;
                } else if (seq[2] == 'B') {
                    if (top_row < max_top) top_row++;
                }
                continue;
            }
            continue;
        }

        if (c == 'j') {
            if (top_row < max_top) top_row++;
        } else if (c == 'k') {
            if (top_row > 0) top_row--;
        } else if (c == ' ') {
            if (top_row + (uint32_t)view_height < max_top) top_row += (uint32_t)view_height;
            else top_row = max_top;
        } else if (c == 'b') {
            if (top_row > (uint32_t)view_height) top_row -= (uint32_t)view_height;
            else top_row = 0;
        } else if (c == 'g') {
            top_row = 0;
        } else if (c == 'G') {
            top_row = max_top;
        }
    }

    write(STDOUT_FILENO, "\033[H\033[2J", 7);
}

/* 4. Parseur CLI pour la commande :hex */
int rap_hex_execute(rap_context_t *ctx, const char *args) {
    if (!ctx) return -1;

    while (args && *args == ' ') args++;

    uint32_t start_off = 0;

    if (args && *args != '\0') {
        if (args[0] == '0' && (args[1] == 'x' || args[1] == 'X')) {
            start_off = (uint32_t)strtoul(args + 2, NULL, 16);
        } else if (isdigit((unsigned char)args[0])) {
            start_off = (uint32_t)strtoul(args, NULL, 10);
        }
    } else {
        if (ctx->cursor_row < ctx->total_lines) {
            start_off = ctx->lines[ctx->cursor_row].offset;
        }
    }

    if (start_off >= ctx->buffer_len && ctx->buffer_len > 0) {
        start_off = ctx->buffer_len - 1;
    }

    rap_hex_show_pager(ctx, start_off);
    return 0;
}

/* 5. Point d'entrée greffon */
static int rap_cmd_hex_plugin_entry(rap_context_t *ctx, const char *args) {
    return rap_hex_execute(ctx, args);
}

/* 6. Enregistrement en table BSS */
int rap_plugin_mod_hex_init(void) {
    static rap_plugin_t plug_hex = {
        .cmd_trigger = "hex",
        .description = "Inspecteur hexadécimal brut 0-heap (:hex, :hex <offset>)",
        .execute     = rap_cmd_hex_plugin_entry
    };

    rap_plugin_register(&plug_hex);
    return 0;
}
