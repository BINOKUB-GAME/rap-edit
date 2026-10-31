/* ==============================================================================
 * SCRIPT      : include/rap_edit_ops.h
 * RÉVISION    : Rev 1.2 (UTF-8 Multibyte Geometry Support)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Ajout de la déclaration rap_utf8_char_len() pour la géométrie
 *               multi-octets UTF-8 et synchronisation du prototype avec mod_ops.
 * FONCTIONS   :
 *   1. rap_utf8_char_len (Calcul de la longueur en octets d'un glyphe UTF-8)
 *   2. rap_ops_get_cursor_offset (Calcul de l'offset mémoire pointé par le curseur)
 *   3. rap_ops_get_anchor_offset (Calcul de l'offset mémoire pointé par l'ancre)
 *   4. rap_ops_set_cursor_from_offset (Recalcul exact de row/col selon un offset)
 *   5. rap_ops_clamp_cursor (Bridage défensif contre toute dérive hors limites)
 *   6. rap_ops_insert_char (Insertion d'octet avec translation mémoire memmove)
 *   7. rap_ops_delete_char (Suppression atomique de glyphes UTF-8 / Backspace)
 *   8. rap_ops_move_cursor (Déplacement fléché avec franchissement UTF-8)
 * ============================================================================== */

#ifndef RAP_EDIT_OPS_H
#define RAP_EDIT_OPS_H

#include <stdint.h>
#include "rap_edit_types.h"

uint32_t rap_utf8_char_len(unsigned char lead);
uint32_t rap_ops_get_cursor_offset(const rap_context_t *ctx);
uint32_t rap_ops_get_anchor_offset(const rap_context_t *ctx);
void     rap_ops_set_cursor_from_offset(rap_context_t *ctx, uint32_t target_off);
void     rap_ops_clamp_cursor(rap_context_t *ctx);
void     rap_ops_insert_char(rap_context_t *ctx, char ch);
void     rap_ops_delete_char(rap_context_t *ctx);
void     rap_ops_move_cursor(rap_context_t *ctx, char direction);

#endif /* RAP_EDIT_OPS_H */
