/* ==============================================================================
 * SCRIPT      : include/rap_edit_sanitize.h
 * RÉVISION    : Rev 1.0 (Sanitization Plugin Interface)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Déclarations pour l'assainissement 0-heap en mémoire BSS :
 *               suppression des retours chariot Windows (\r), élimination des
 *               octets nuls (\0) et nettoyage global (:dos2unix, :strip-null, :clean).
 * FONCTIONS   :
 *   1. rap_sanitize_dos2unix (Suppression in-situ de tous les octets '\r')
 *   2. rap_sanitize_strip_null (Suppression in-situ de tous les octets '\0')
 *   3. rap_sanitize_clean_all (Élimination combinée '\r' et '\0' en une seule passe)
 *   4. rap_plugin_mod_sanitize_init (Enregistrement des greffons dans la table BSS)
 * ============================================================================== */

#ifndef RAP_EDIT_SANITIZE_H
#define RAP_EDIT_SANITIZE_H

#include <stdint.h>
#include <stdbool.h>
#include "rap_edit_types.h"

int rap_sanitize_dos2unix(rap_context_t *ctx);
int rap_sanitize_strip_null(rap_context_t *ctx);
int rap_sanitize_clean_all(rap_context_t *ctx);
int rap_plugin_mod_sanitize_init(void);

#endif /* RAP_EDIT_SANITIZE_H */
