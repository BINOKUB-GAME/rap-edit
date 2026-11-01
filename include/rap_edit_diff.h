/* ==============================================================================
 * SCRIPT      : include/rap_edit_diff.h
 * RÉVISION    : Rev 1.0 (Tactical In-Situ Diff Engine Interface)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Déclarations de l'outil de comparaison textuelle in-situ 0-heap :
 *               comparaison disque vs RAM (:diff orig), multi-onglets (:diff #n)
 *               et affichage avec pager couleur interactif VT100.
 * FONCTIONS   :
 *   1. rap_diff_execute (Point d'entrée CLI pour la commande :diff)
 *   2. rap_plugin_mod_diff_init (Enregistrement du greffon dans la table BSS)
 * ============================================================================== */

#ifndef RAP_EDIT_DIFF_H
#define RAP_EDIT_DIFF_H

#include <stdint.h>
#include <stdbool.h>
#include "rap_edit_types.h"

int rap_diff_execute(rap_context_t *ctx, const char *args);
int rap_plugin_mod_diff_init(void);

#endif /* RAP_EDIT_DIFF_H */
