/* ==============================================================================
 * SCRIPT      : include/rap_edit_protect.h
 * RÉVISION    : Rev 1.0 (Zero-Heap Buffer Write-Protection Interface)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Déclarations pour le verrouillage/déverrouillage d'écriture 0-heap :
 *               bascule manuelle en lecture seule (:ro) et lecture-écriture (:rw),
 *               vérification des droits disque et synchronisation avec le HUD.
 * FONCTIONS   :
 *   1. rap_protect_set_mode (Mutation contrôlée du statut is_readonly et message HUD)
 *   2. rap_protect_execute (Parseur CLI pour les commandes :ro et :rw)
 *   3. rap_plugin_mod_protect_init (Enregistrement du greffon dans la table BSS)
 * ============================================================================== */

#ifndef RAP_EDIT_PROTECT_H
#define RAP_EDIT_PROTECT_H

#include <stdint.h>
#include <stdbool.h>
#include "rap_edit_types.h"

int rap_protect_set_mode(rap_context_t *ctx, bool readonly);
int rap_protect_execute(rap_context_t *ctx, const char *cmd);
int rap_plugin_mod_protect_init(void);

#endif /* RAP_EDIT_PROTECT_H */
