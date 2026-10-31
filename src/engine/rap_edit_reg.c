/* ==============================================================================
 * SCRIPT      : src/engine/rap_edit_reg.c
 * RÉVISION    : Rev 1.1
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Harmonisation du gestionnaire de greffons BSS. Tolérance aux
 *               espaces initiaux et deux-points résiduels, distribution sécurisée
 *               des arguments pour :rch, protection contre les pointeurs execute
 *               NULL et conservation de rap_plugin_list_help.
 * FONCTIONS   :
 *   1. rap_plugin_register (Enregistrement sécurisé d'un greffon dans la table BSS)
 *   2. rap_plugin_dispatch (Routage déterministe et transmission propre des arguments)
 *   3. rap_plugin_list_help (Affichage console de la liste des commandes actives)
 * ============================================================================== */

#include <stdio.h>
#include <string.h>
#include "rap_edit_plugin.h"

/* Table d'enregistrement résidant en mémoire BSS */
static rap_plugin_t s_plugin_table[RAP_MAX_PLUGINS];
static uint32_t     s_plugin_count = 0;

/* 1. Enregistrement d'un greffon dans la table BSS */
int rap_plugin_register(const rap_plugin_t *plugin) {
    if (!plugin || !plugin->cmd_trigger) {
        return -1;
    }
    if (s_plugin_count >= RAP_MAX_PLUGINS) {
        return -2; /* Table pleine */
    }
    s_plugin_table[s_plugin_count++] = *plugin;
    return 0;
}

/* 2. Routage déterministe et transmission propre des arguments */
int rap_plugin_dispatch(rap_context_t *ctx, const char *line_input) {
    if (!ctx || !line_input) {
        return -1;
    }

    /* Saut des espaces, tabulations ou deux-points initiaux résiduels */
    while (*line_input == ' ' || *line_input == '\t' || *line_input == ':') {
        line_input++;
    }
    if (*line_input == '\0') {
        return 0;
    }

    for (uint32_t i = 0; i < s_plugin_count; ++i) {
        size_t trig_len = strlen(s_plugin_table[i].cmd_trigger);
        if (strncmp(line_input, s_plugin_table[i].cmd_trigger, trig_len) == 0) {
            /* Vérification de la délimitation : fin de mot ou espace */
            if (line_input[trig_len] == '\0' || line_input[trig_len] == ' ') {
                const char *args = line_input + trig_len;
                while (*args == ' ' || *args == '\t') {
                    args++; /* Saut des espaces séparateurs */
                }
                if (s_plugin_table[i].execute) {
                    return s_plugin_table[i].execute(ctx, args);
                }
                return 0;
            }
        }
    }
    return -127; /* Commande inconnue */
}

/* 3. Affichage console de la liste des commandes actives */
void rap_plugin_list_help(void) {
    printf("\n=== COMMANDES ET GREFFONS DISPONIBLES ===\n");
    for (uint32_t i = 0; i < s_plugin_count; ++i) {
        printf("  %-10s : %s\n", s_plugin_table[i].cmd_trigger, s_plugin_table[i].description);
    }
    printf("=========================================\n\n");
}
