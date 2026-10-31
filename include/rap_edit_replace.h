/* ==============================================================================
 * SCRIPT      : include/rap_edit_replace.h
 * RÉVISION    : Rev 1.0
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Interface du greffon modulaire de substitution (:s, :%s, :10:20s).
 * FONCTIONS   :
 *   1. rap_replace_parse_cmd (Parseur syntaxique des commandes de substitution)
 *   2. rap_plugin_mod_replace_init (Enregistrement du greffon dans la table BSS)
 * ============================================================================== */

#ifndef RAP_EDIT_REPLACE_H
#define RAP_EDIT_REPLACE_H

#include "rap_edit_types.h"

int rap_replace_parse_cmd(rap_context_t *ctx, const char *cmd);
int rap_plugin_mod_replace_init(void);

#endif /* RAP_EDIT_REPLACE_H */
