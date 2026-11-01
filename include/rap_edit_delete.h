/* ==============================================================================
 * SCRIPT      : include/rap_edit_delete.h
 * RÉVISION    : Rev 2.1 (Prototype Signature Synchronization)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Alignement strict du prototype de rap_delete_lines avec l'implémentation
 *               réelle de src/plugins/mod_delete.c (signature 1-based à 3 paramètres).
 *               Élimination du conflit de types GCC sans allocation dynamique.
 * FONCTIONS   :
 *   1. rap_delete_lines (Suppression effective d'une plage de lignes 1-based)
 *   2. rap_delete_parse_cmd (Parseur CLI discriminant :d, :dd, :<N>dd, :<num>d, :<deb>:<fin>d)
 *   3. rap_plugin_mod_delete_init (Enregistrement du greffon dans la table BSS)
 * ============================================================================== */

#ifndef RAP_EDIT_DELETE_H
#define RAP_EDIT_DELETE_H

#include <stdint.h>
#include <stdbool.h>
#include "rap_edit_types.h"

/* 1. Suppression effective d'une plage de lignes (numérotation 1-based) */
int rap_delete_lines(rap_context_t *ctx, uint32_t start_line, uint32_t end_line);

/* 2. Parseur CLI discriminant les syntaxes de suppression */
int rap_delete_parse_cmd(rap_context_t *ctx, const char *cmd);

/* 3. Enregistrement dans la table des greffons BSS */
int rap_plugin_mod_delete_init(void);

#endif /* RAP_EDIT_DELETE_H */
