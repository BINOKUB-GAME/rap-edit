/* ==============================================================================
 * SCRIPT      : include/rap_edit_buffer.h
 * RÉVISION    : Rev 1.0 (Multi-Buffer Interface)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Déclarations pour la gestion des tampons multiples en BSS,
 *               du presse-papiers partagé et de la bascule d'onglets (#1 à #4).
 * FONCTIONS   :
 *   1. rap_buffer_init (Initialisation des descripteurs de tampons en BSS)
 *   2. rap_buffer_load_file (Chargement d'un fichier dans un emplacement donné)
 *   3. rap_buffer_get (Accès direct à un contexte par son indice)
 *   4. rap_buffer_get_active (Pointeur vers le contexte du document actif)
 *   5. rap_buffer_get_active_idx (Indice du document actuellement affiché)
 *   6. rap_buffer_set_active_idx (Bascule vers un onglet avec transfert clipboard)
 *   7. rap_buffer_cycle (Bascule circulaire vers l'onglet suivant ou précédent)
 *   8. rap_buffer_get_count (Nombre total de documents chargés)
 *   9. rap_plugin_mod_buffer_init (Enregistrement du greffon dans la table BSS)
 * ============================================================================== */

#ifndef RAP_EDIT_BUFFER_H
#define RAP_EDIT_BUFFER_H

#include <stdint.h>
#include "rap_edit_types.h"

void           rap_buffer_init(void);
int            rap_buffer_load_file(uint32_t idx, const char *filepath);
rap_context_t* rap_buffer_get(uint32_t idx);
rap_context_t* rap_buffer_get_active(void);
uint32_t       rap_buffer_get_active_idx(void);
int            rap_buffer_set_active_idx(uint32_t idx);
void           rap_buffer_cycle(int direction);
uint32_t       rap_buffer_get_count(void);
int            rap_plugin_mod_buffer_init(void);

#endif /* RAP_EDIT_BUFFER_H */
