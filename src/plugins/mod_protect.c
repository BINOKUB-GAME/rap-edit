/* ==============================================================================
 * SCRIPT      : src/plugins/mod_protect.c
 * RÉVISION    : Rev 1.0 (Write-Protection Toggle Engine)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Gestionnaire 0-heap de protection en écriture. Prise en charge des
 *               commandes :ro (Read-Only) et :rw (Read-Write), vérification des
 *               droits POSIX W_OK et retour visuel haute visibilité dans le HUD.
 * FONCTIONS   :
 *   1. rap_protect_check_disk_writable (Vérification non-bloquante des droits système)
 *   2. rap_protect_set_mode (Application du mode lecture seule ou lecture/écriture)
 *   3. rap_protect_execute (Aiguillage des commandes :ro et :rw)
 *   4. rap_cmd_protect_plugin_entry (Point d'entrée pour le dispatcheur BSS)
 *   5. rap_plugin_mod_protect_init (Enregistrement persistant dans la table des greffons)
 * ============================================================================== */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rap_edit_protect.h"
#include "rap_edit_config.h"
#include "rap_edit_plugin.h"

/* 1. Vérification non-bloquante des droits d'écriture physiques sur disque */
static bool rap_protect_check_disk_writable(const char *path) {
    if (!path || path[0] == '\0') return true;
    if (access(path, F_OK) != 0) {
        /* Nouveau fichier pas encore créé : écriture autorisée par défaut */
        return true;
    }
    return (access(path, W_OK) == 0);
}

/* 2. Application du mode et notification dynamique dans le Cockpit */
int rap_protect_set_mode(rap_context_t *ctx, bool readonly) {
    if (!ctx) return -1;

    ctx->is_readonly = readonly;

    if (readonly) {
        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                 RAP_ANSI_BOLD RAP_ANSI_RED "[✓] Tampon verrouillé en LECTURE SEULE ([RO])" RAP_ANSI_RESET);
    } else {
        bool disk_ok = rap_protect_check_disk_writable(ctx->file_path);
        if (disk_ok) {
            snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                     RAP_ANSI_BOLD RAP_ANSI_GREEN "[✓] Tampon déverrouillé en LECTURE-ÉCRITURE ([RW])" RAP_ANSI_RESET);
        } else {
            snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                     RAP_ANSI_BOLD RAP_ANSI_YELLOW "[!] Tampon [RW] en RAM mais fichier disque en LECTURE SEULE sur l'OS" RAP_ANSI_RESET);
        }
    }
    return 0;
}

/* 3. Aiguillage des commandes :ro et :rw */
int rap_protect_execute(rap_context_t *ctx, const char *cmd) {
    if (!ctx || !cmd) return -1;

    while (*cmd == ' ' || *cmd == ':') cmd++;

    if (strcmp(cmd, "ro") == 0) {
        return rap_protect_set_mode(ctx, true);
    }

    if (strcmp(cmd, "rw") == 0) {
        return rap_protect_set_mode(ctx, false);
    }

    return -127;
}

/* 4. Point d'entrée pour le dispatcheur de greffons */
static int rap_cmd_protect_plugin_entry(rap_context_t *ctx, const char *args) {
    return rap_protect_execute(ctx, args);
}

/* 5. Enregistrement en table BSS */
int rap_plugin_mod_protect_init(void) {
    static rap_plugin_t plug_ro = {
        .cmd_trigger = "ro",
        .description = "Bascule le tampon en lecture seule protégée (:ro)",
        .execute     = rap_cmd_protect_plugin_entry
    };

    static rap_plugin_t plug_rw = {
        .cmd_trigger = "rw",
        .description = "Bascule le tampon en mode lecture-écriture (:rw)",
        .execute     = rap_cmd_protect_plugin_entry
    };

    rap_plugin_register(&plug_ro);
    rap_plugin_register(&plug_rw);
    return 0;
}
