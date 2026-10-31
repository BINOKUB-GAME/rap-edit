/* ==============================================================================
 * SCRIPT      : src/plugins/mod_nav.c
 * RÉVISION    : Rev 1.0
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Greffon modulaire de navigation rapide 0-heap.
 *               Fournit les primitives de saut en début de ligne (Ctrl+A / BOL)
 *               et en fin de ligne (Ctrl+E / EOL), avec compatibilité sélection.
 * FONCTIONS   :
 *   1. rap_nav_bol (Positionne le curseur en colonne 0 sur la ligne courante)
 *   2. rap_nav_eol (Positionne le curseur au dernier caractère de la ligne courante)
 *   3. rap_cmd_bol (Commande console :bol pour retour au début de ligne)
 *   4. rap_cmd_eol (Commande console :eol pour saut en fin de ligne)
 *   5. rap_plugin_mod_nav_init (Enregistrement des greffons dans la table BSS)
 * ============================================================================== */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>

#include "rap_edit_types.h"
#include "rap_edit_plugin.h"

/* 1. Saut immédiat en début de ligne (Colonne 0) */
void rap_nav_bol(rap_context_t *ctx) {
    if (!ctx) return;
    ctx->cursor_col = 0;
}

/* 2. Saut immédiat en fin de ligne (Dernier caractère indexé) */
void rap_nav_eol(rap_context_t *ctx) {
    if (!ctx) return;
    if (ctx->cursor_row < ctx->total_lines) {
        ctx->cursor_col = ctx->lines[ctx->cursor_row].length;
    } else {
        ctx->cursor_col = 0;
    }
}

/* 3. Commande CLI :bol */
static int rap_cmd_bol(rap_context_t *ctx, const char *args) {
    (void)args;
    rap_nav_bol(ctx);
    return 0;
}

/* 4. Commande CLI :eol */
static int rap_cmd_eol(rap_context_t *ctx, const char *args) {
    (void)args;
    rap_nav_eol(ctx);
    return 0;
}

/* 5. Initialisation et enregistrement du module */
int rap_plugin_mod_nav_init(void) {
    rap_plugin_t plug_bol = {
        .cmd_trigger = "bol",
        .description = "Déplace le curseur au début de la ligne active",
        .execute     = rap_cmd_bol
    };
    rap_plugin_register(&plug_bol);

    rap_plugin_t plug_eol = {
        .cmd_trigger = "eol",
        .description = "Déplace le curseur à la fin de la ligne active",
        .execute     = rap_cmd_eol
    };
    rap_plugin_register(&plug_eol);

    return 0;
}
