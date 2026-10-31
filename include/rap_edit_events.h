/* ==============================================================================
 * SCRIPT      : include/rap_edit_events.h
 * RÉVISION    : Rev 1.0
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Interface du sous-système de traitement d'événements et commandes.
 * FONCTIONS   :
 *   1. rap_events_run_loop (Boucle principale d'interaction visuelle 0-heap)
 * ============================================================================== */

#ifndef RAP_EDIT_EVENTS_H
#define RAP_EDIT_EVENTS_H

#include "rap_edit_types.h"

void rap_events_run_loop(rap_context_t *ctx);

#endif /* RAP_EDIT_EVENTS_H */
