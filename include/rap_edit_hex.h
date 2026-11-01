/* ==============================================================================
 * SCRIPT      : include/rap_edit_hex.h
 * RÉVISION    : Rev 1.0 (Zero-Heap Raw Hexadecimal Inspection Engine)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Déclarations de l'inspecteur hexadécimal brut 0-heap BSS :
 *               grille 16 octets/ligne, décodage ASCII direct, navigation
 *               interactive VT100 et calcul instantané d'offsets mémoire O(1).
 * FONCTIONS   :
 *   1. rap_hex_execute (Point d'entrée CLI pour la commande :hex)
 *   2. rap_plugin_mod_hex_init (Enregistrement du greffon dans la table BSS)
 * ============================================================================== */

#ifndef RAP_EDIT_HEX_H
#define RAP_EDIT_HEX_H

#include <stdint.h>
#include <stdbool.h>
#include "rap_edit_types.h"

int rap_hex_execute(rap_context_t *ctx, const char *args);
int rap_plugin_mod_hex_init(void);

#endif /* RAP_EDIT_HEX_H */
