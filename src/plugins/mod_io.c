/* ==============================================================================
 * SCRIPT      : src/plugins/mod_io.c
 * RÉVISION    : Rev 1.3 (Truncation-Free Status Formatting)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Utilisation de %.48s sur ctx->file_path pour éliminer les warnings
 *               -Wformat-truncation sur la notification de sauvegarde atomique.
 * FONCTIONS   :
 *   1. rap_cmd_save (Écriture atomique, fsync et statut borné)
 *   2. rap_cmd_diff (Comparaison mémoire BSS / disque bornée)
 *   3. rap_plugin_mod_io_init (Enregistrement des greffons en BSS)
 * ============================================================================== */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "rap_edit_types.h"
#include "rap_edit_plugin.h"

/* 1. Écriture atomique et création sur disque si nouveau fichier */
static int rap_cmd_save(rap_context_t *ctx, const char *args) {
    (void)args;
    if (!ctx) return -1;

    if (ctx->is_readonly) {
        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                 RAP_ANSI_YELLOW "[!] Erreur : Fichier en lecture seule. Écriture refusée." RAP_ANSI_RESET);
        return -1;
    }

    int fd = open(ctx->file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                 RAP_ANSI_YELLOW "[!] Échec d'ouverture en écriture sur '%.48s'" RAP_ANSI_RESET, ctx->file_path);
        return -2;
    }

    ssize_t written = write(fd, ctx->raw_buffer, ctx->buffer_len);
    if (written < 0 || (uint32_t)written != ctx->buffer_len) {
        close(fd);
        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                 RAP_ANSI_YELLOW "[!] Écriture incomplète sur le support physique" RAP_ANSI_RESET);
        return -3;
    }

    fsync(fd);
    close(fd);

    struct stat st;
    if (stat(ctx->file_path, &st) == 0) {
        ctx->file_size_on_disk = (uint64_t)st.st_size;
        ctx->inode_num = (uint64_t)st.st_ino;
    }

    ctx->is_dirty = false;
    snprintf(ctx->status_msg, sizeof(ctx->status_msg),
             RAP_ANSI_BOLD "[✓] Fichier '%.48s' synchronisé sur disque (%u octets)" RAP_ANSI_RESET,
             ctx->file_path, ctx->buffer_len);

    return 0;
}

/* 2. Comparaison rapide mémoire BSS / disque */
static int rap_cmd_diff(rap_context_t *ctx, const char *args) {
    (void)args;
    if (!ctx) return -1;

    struct stat st;
    if (stat(ctx->file_path, &st) != 0) {
        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                 "[INFO] Nouveau fichier : n'existe pas encore sur disque.");
        return 0;
    }

    if ((uint64_t)st.st_size != ctx->buffer_len) {
        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                 "[DIFF] Mémoire: %u o | Disque: %ld o (Diff: %ld o)",
                 ctx->buffer_len, (long)st.st_size, (long)ctx->buffer_len - (long)st.st_size);
    } else {
        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                 "[DIFF] Mémoire et disque synchrones (%u octets).", ctx->buffer_len);
    }

    return 0;
}

/* 3. Initialisation du module */
int rap_plugin_mod_io_init(void) {
    rap_plugin_t plug_save = {
        .cmd_trigger = "w!",
        .description = "Écriture atomique avec création et synchronisation fsync",
        .execute     = rap_cmd_save
    };
    rap_plugin_register(&plug_save);

    rap_plugin_t plug_diff = {
        .cmd_trigger = "diff",
        .description = "Compare l'état du tampon BSS avec le disque",
        .execute     = rap_cmd_diff
    };
    rap_plugin_register(&plug_diff);

    return 0;
}
