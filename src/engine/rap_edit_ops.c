/* ==============================================================================
 * SCRIPT      : src/engine/rap_edit_ops.c
 * RÉVISION    : Rev 1.3 (Atomic UTF-8 Cursor Traversal and Deletion)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Prise en charge des glyphes multi-octets UTF-8. Franchissement
 *               complet lors des déplacements fléchés et suppression du caractère
 *               entier (octet de tête + continuations) lors d'un Backspace.
 * FONCTIONS   :
 *   1. rap_utf8_char_len (Détermination déterministe de la taille d'un point de code)
 *   2. rap_ops_get_cursor_offset (Conversion coordonnées curseur -> offset brut)
 *   3. rap_ops_get_anchor_offset (Conversion coordonnées ancre -> offset brut)
 *   4. rap_ops_set_cursor_from_offset (Repositionnement exact de row/col)
 *   5. rap_ops_clamp_cursor (Bridage absolu de la coordonnée curseur)
 *   6. rap_ops_insert_char (Insertion d'octets avec snapshot de rafale)
 *   7. rap_ops_delete_char (Suppression atomique d'un glyphe UTF-8 complet)
 *   8. rap_ops_move_cursor (Déplacement fléché avec franchissement UTF-8)
 * ============================================================================== */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <string.h>
#include "rap_edit_ops.h"
#include "rap_edit_config.h"
#include "rap_edit_undo.h"

extern int rap_core_index_lines(rap_context_t *ctx);

/* 1. Longueur en octets d'une séquence UTF-8 selon son octet de tête */
uint32_t rap_utf8_char_len(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1;
}

/* 2. Offset mémoire pointé par le curseur */
uint32_t rap_ops_get_cursor_offset(const rap_context_t *ctx) {
    if (!ctx || ctx->total_lines == 0) return 0;
    uint32_t r = ctx->cursor_row;
    if (r >= ctx->total_lines) r = ctx->total_lines - 1;
    uint32_t off = ctx->lines[r].offset + ctx->cursor_col;
    return (off > ctx->buffer_len) ? ctx->buffer_len : off;
}

/* 3. Offset mémoire pointé par l'ancre */
uint32_t rap_ops_get_anchor_offset(const rap_context_t *ctx) {
    if (!ctx || ctx->total_lines == 0) return 0;
    uint32_t r = ctx->sel_start_row;
    if (r >= ctx->total_lines) r = ctx->total_lines - 1;
    uint32_t off = ctx->lines[r].offset + ctx->sel_start_col;
    return (off > ctx->buffer_len) ? ctx->buffer_len : off;
}

/* 4. Repositionnement exact de (row, col) */
void rap_ops_set_cursor_from_offset(rap_context_t *ctx, uint32_t target_off) {
    if (!ctx || ctx->total_lines == 0) {
        ctx->cursor_row = 0;
        ctx->cursor_col = 0;
        return;
    }

    if (target_off > ctx->buffer_len) {
        target_off = ctx->buffer_len;
    }

    for (uint32_t i = 0; i < ctx->total_lines; ++i) {
        uint32_t line_start = ctx->lines[i].offset;
        uint32_t line_end = line_start + ctx->lines[i].length;

        if (target_off >= line_start && (target_off <= line_end || i == ctx->total_lines - 1)) {
            ctx->cursor_row = i;
            ctx->cursor_col = target_off - line_start;
            return;
        }
    }

    ctx->cursor_row = ctx->total_lines - 1;
    ctx->cursor_col = ctx->lines[ctx->cursor_row].length;
}

/* 5. Bridage de sécurité absolu */
void rap_ops_clamp_cursor(rap_context_t *ctx) {
    if (!ctx) return;
    if (ctx->total_lines == 0) {
        ctx->cursor_row = 0;
        ctx->cursor_col = 0;
        return;
    }
    if (ctx->cursor_row >= ctx->total_lines) {
        ctx->cursor_row = ctx->total_lines - 1;
    }
    if (ctx->cursor_col > ctx->lines[ctx->cursor_row].length) {
        ctx->cursor_col = ctx->lines[ctx->cursor_row].length;
    }
}

/* 6. Insertion avec snapshot de rafale */
void rap_ops_insert_char(rap_context_t *ctx, char ch) {
    if (!ctx || ctx->is_readonly || ctx->buffer_len + 1 >= RAP_STATIC_BUFFER_SIZE) return;

    rap_undo_snapshot_burst(ctx);

    uint32_t insert_pos = rap_ops_get_cursor_offset(ctx);
    size_t bytes_to_move = ctx->buffer_len - insert_pos;

    if (bytes_to_move > 0) {
        memmove(&ctx->raw_buffer[insert_pos + 1],
                &ctx->raw_buffer[insert_pos],
                bytes_to_move);
    }

    ctx->raw_buffer[insert_pos] = (uint8_t)ch;
    ctx->buffer_len++;
    ctx->is_dirty = true;

    if (ch == '\n') {
        ctx->cursor_row++;
        ctx->cursor_col = 0;
    } else {
        ctx->cursor_col++;
    }

    rap_core_index_lines(ctx);
    rap_ops_clamp_cursor(ctx);
}

/* 7. Suppression atomique d'un glyphe UTF-8 complet (Backspace) */
void rap_ops_delete_char(rap_context_t *ctx) {
    if (!ctx || ctx->is_readonly || ctx->buffer_len == 0) return;

    rap_undo_snapshot_burst(ctx);
    rap_ops_clamp_cursor(ctx);
    uint32_t line_idx = ctx->cursor_row;

    if (ctx->cursor_col > 0) {
        uint32_t line_start = ctx->lines[line_idx].offset;
        uint32_t del_end    = line_start + ctx->cursor_col;
        uint32_t del_start  = del_end - 1;

        /* Remonte jusqu'à l'octet de tête si l'octet précédent est une continuation UTF-8 */
        while (del_start > line_start && (ctx->raw_buffer[del_start] & 0xC0) == 0x80) {
            del_start--;
        }

        uint32_t bytes_to_delete = del_end - del_start;
        if (del_end <= ctx->buffer_len) {
            size_t bytes_to_move = ctx->buffer_len - del_end;
            if (bytes_to_move > 0) {
                memmove(&ctx->raw_buffer[del_start],
                        &ctx->raw_buffer[del_end],
                        bytes_to_move);
            }
            ctx->buffer_len -= bytes_to_delete;
            ctx->is_dirty = true;
            ctx->cursor_col -= bytes_to_delete;
            rap_core_index_lines(ctx);
        }
    } else if (line_idx > 0) {
        /* Jonction avec la ligne précédente (suppression du saut de ligne \n) */
        uint32_t prev_line = line_idx - 1;
        uint32_t delete_pos = ctx->lines[line_idx].offset - 1;
        if (delete_pos < ctx->buffer_len && ctx->raw_buffer[delete_pos] == '\n') {
            size_t bytes_to_move = ctx->buffer_len - (delete_pos + 1);
            if (bytes_to_move > 0) {
                memmove(&ctx->raw_buffer[delete_pos],
                        &ctx->raw_buffer[delete_pos + 1],
                        bytes_to_move);
            }
            ctx->buffer_len--;
            ctx->is_dirty = true;
            ctx->cursor_row = prev_line;
            ctx->cursor_col = ctx->lines[prev_line].length;
            rap_core_index_lines(ctx);
        }
    }
    rap_ops_clamp_cursor(ctx);
}

/* 8. Déplacement fléché avec franchissement UTF-8 */
void rap_ops_move_cursor(rap_context_t *ctx, char direction) {
    if (!ctx) return;

    rap_undo_reset_burst();

    switch (direction) {
        case 'A': /* HAUT */
            if (ctx->cursor_row > 0) ctx->cursor_row--;
            break;
        case 'B': /* BAS */
            if (ctx->cursor_row + 1 < ctx->total_lines) ctx->cursor_row++;
            break;
        case 'C': /* DROITE : avance du nombre exact d'octets du glyphe UTF-8 */
            if (ctx->cursor_row < ctx->total_lines) {
                uint16_t line_len = ctx->lines[ctx->cursor_row].length;
                if (ctx->cursor_col < line_len) {
                    uint32_t off = ctx->lines[ctx->cursor_row].offset + ctx->cursor_col;
                    uint32_t step = rap_utf8_char_len(ctx->raw_buffer[off]);
                    if (ctx->cursor_col + step <= line_len) {
                        ctx->cursor_col += step;
                    } else {
                        ctx->cursor_col++;
                    }
                }
            }
            break;
        case 'D': /* GAUCHE : recule jusqu'à l'octet de tête UTF-8 */
            if (ctx->cursor_col > 0) {
                uint32_t line_start = ctx->lines[ctx->cursor_row].offset;
                ctx->cursor_col--;
                while (ctx->cursor_col > 0 &&
                       (ctx->raw_buffer[line_start + ctx->cursor_col] & 0xC0) == 0x80) {
                    ctx->cursor_col--;
                }
            }
            break;
    }
    rap_ops_clamp_cursor(ctx);
}
