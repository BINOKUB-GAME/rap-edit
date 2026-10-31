/* ==============================================================================
 * SCRIPT      : include/rap_edit_undo.h
 * RÉVISION    : Rev 1.0
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Interface du sous-système d'annulation 0-heap BSS (Snapshot & Swap).
 * FONCTIONS   :
 *   1. rap_undo_reset_burst (Réinitialise le drapeau de frappe continue)
 *   2. rap_undo_snapshot (Capture immédiate d'un état complet avant opération)
 *   3. rap_undo_snapshot_burst (Capture conditionnelle sur début de rafale de frappe)
 *   4. rap_undo_apply (Restauration réversible de l'état précédent / Toggle Redo)
 *   5. rap_plugin_mod_undo_init (Enregistrement du greffon dans la table BSS)
 * ============================================================================== */

#ifndef RAP_EDIT_UNDO_H
#define RAP_EDIT_UNDO_H

#include "rap_edit_types.h"

void rap_undo_reset_burst(void);
void rap_undo_snapshot(const rap_context_t *ctx);
void rap_undo_snapshot_burst(const rap_context_t *ctx);
int  rap_undo_apply(rap_context_t *ctx);
int  rap_plugin_mod_undo_init(void);

#endif /* RAP_EDIT_UNDO_H */
