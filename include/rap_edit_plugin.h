/* ==============================================================================
 * SCRIPT      : include/rap_edit_plugin.h
 * RÉVISION    : Rev 1.0
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Contrat d'interface unifié pour l'architecture en greffons.
 *               Définit la signature des commandes isolées et le contexte d'exécution.
 * FONCTIONS   :
 *   1. rap_plugin_handler_t (Pointeur de fonction d'exécution de commande)
 * ============================================================================== */

#ifndef RAP_EDIT_PLUGIN_H
#define RAP_EDIT_PLUGIN_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "rap_edit_types.h"

/* 1. Signature normalisée de la routine d'un greffon */
typedef int (*rap_plugin_handler_t)(rap_context_t *ctx, const char *args);

/* Structure d'enregistrement d'un greffon */
typedef struct {
    const char           *cmd_trigger; /* Préfixe déclencheur (ex: "p", "c", "x") */
    const char           *description; /* Description affichée par l'aide intégrée */
    rap_plugin_handler_t  execute;     /* Routine de traitement isolée */
} rap_plugin_t;

/* Primitives de registre */
int  rap_plugin_register(const rap_plugin_t *plugin);
int  rap_plugin_dispatch(rap_context_t *ctx, const char *line_input);
void rap_plugin_list_help(void);

#endif /* RAP_EDIT_PLUGIN_H */
