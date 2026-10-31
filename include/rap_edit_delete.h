/* ==============================================================================
 * SCRIPT      : include/rap_edit_delete.h
 * RÉVISION    : Rev 1.0
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Interface du greffon modulaire de suppression de lignes (:d, :2d, :3:90d).
 * FONCTIONS   :
 *   1. rap_delete_lines (Suppression sécurisée d'une plage de lignes avec Undo)
 *   2. rap_delete_parse_cmd (Parseur syntaxique des commandes de suppression)
 *   3. rap_plugin_mod_delete_init (Enregistrement du greffon dans la table BSS)
 * ============================================================================== */

#ifndef RAP_EDIT_DELETE_H
#define RAP_EDIT_DELETE_H

#include <stdint.h>
#include "rap_edit_types.h"

int rap_delete_lines(rap_context_t *ctx, uint32_t start_line, uint32_t end_line);
int rap_delete_parse_cmd(rap_context_t *ctx, const char *cmd);
int rap_plugin_mod_delete_init(void);

#endif /* RAP_EDIT_DELETE_H */
