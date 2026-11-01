/* ==============================================================================
 * SCRIPT      : src/plugins/mod_panic.c
 * RÉVISION    : Rev 1.1 (Format Truncation Fix & Safe Buffer Dimensioning)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Élimination des avertissements GCC -Wformat-truncation en calibrant
 *               les tampons locaux de chemins à (RAP_MAX_PATH_LEN + 32). Maintien
 *               strict de l'architecture 0-heap BSS, écriture synchrone avec barrière
 *               physique fsync() et gestion des commandes :bak et :panic.
 * FONCTIONS   :
 *   1. rap_panic_resolve_mode (Récupération des permissions Unix du fichier cible)
 *   2. rap_panic_write_buffer_synced (Écriture atomique et synchronisation fsync)
 *   3. rap_panic_copy_file_on_disk (Duplication bloc à bloc 0-heap de l'original)
 *   4. rap_panic_backup_buffer (Point d'entrée pour sauvegarder le tampon courant)
 *   5. rap_panic_backup_disk_original (Point d'entrée pour sauvegarder l'état disque)
 *   6. rap_panic_execute (Parseur universel CLI de la commande :bak)
 *   7. rap_cmd_panic_entry (Procédure d'évacuation d'urgence :panic)
 *   8. rap_cmd_bak_plugin_entry (Point d'entrée greffon pour la commande :bak)
 *   9. rap_plugin_mod_panic_init (Enregistrement persistant dans la table BSS)
 * ============================================================================== */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "rap_edit_panic.h"
#include "rap_edit_config.h"
#include "rap_edit_plugin.h"

/* Tampon statique BSS pour les copies disque à disque sans toucher au tas */
#define RAP_PANIC_CHUNK_SIZE 4096
static uint8_t s_panic_io_chunk[RAP_PANIC_CHUNK_SIZE];

/* 1. Récupération des permissions d'origine ou repli 0644 */
static mode_t rap_panic_resolve_mode(const char *path) {
    struct stat st;
    if (path && path[0] != '\0' && stat(path, &st) == 0) {
        return (st.st_mode & 0777);
    }
    return 0644;
}

/* 2. Écriture atomique et engagement matériel physique fsync() */
static int rap_panic_write_buffer_synced(const char *dest_path, const uint8_t *buf, uint32_t len, mode_t mode) {
    if (!dest_path || !buf) return -1;

    int fd = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0) {
        return -1;
    }

    uint32_t total_written = 0;
    while (total_written < len) {
        ssize_t n = write(fd, buf + total_written, len - total_written);
        if (n < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return -2;
        }
        total_written += (uint32_t)n;
    }

    /* Barrière matérielle : forcer l'écriture physique sur le support */
    if (fsync(fd) != 0) {
        close(fd);
        return -3;
    }

    close(fd);
    return 0;
}

/* 3. Duplication bloc à bloc de l'original sur disque sans allocation dynamique */
static int rap_panic_copy_file_on_disk(const char *src_path, const char *dest_path) {
    if (!src_path || !dest_path) return -1;

    int in_fd = open(src_path, O_RDONLY);
    if (in_fd < 0) return -1;

    mode_t mode = rap_panic_resolve_mode(src_path);
    int out_fd = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (out_fd < 0) {
        close(in_fd);
        return -2;
    }

    while (1) {
        ssize_t bytes_in = read(in_fd, s_panic_io_chunk, sizeof(s_panic_io_chunk));
        if (bytes_in < 0) {
            if (errno == EINTR) continue;
            close(in_fd);
            close(out_fd);
            return -3;
        }
        if (bytes_in == 0) break;

        ssize_t written = 0;
        while (written < bytes_in) {
            ssize_t w = write(out_fd, s_panic_io_chunk + written, bytes_in - written);
            if (w < 0) {
                if (errno == EINTR) continue;
                close(in_fd);
                close(out_fd);
                return -4;
            }
            written += w;
        }
    }

    fsync(out_fd);
    close(in_fd);
    close(out_fd);
    return 0;
}

/* 4. Sauvegarde synchrone du tampon courant */
int rap_panic_backup_buffer(rap_context_t *ctx, const char *dest_path) {
    if (!ctx) return -1;

    /* Dimensionné à RAP_MAX_PATH_LEN + 32 pour éliminer tout risque de troncature */
    char target[RAP_MAX_PATH_LEN + 32];
    if (dest_path && dest_path[0] != '\0') {
        strncpy(target, dest_path, sizeof(target) - 1);
        target[sizeof(target) - 1] = '\0';
    } else {
        if (ctx->file_path[0] != '\0') {
            snprintf(target, sizeof(target), "%s.bak", ctx->file_path);
        } else {
            snprintf(target, sizeof(target), "rap_buffer_dump.bak");
        }
    }

    mode_t mode = rap_panic_resolve_mode(ctx->file_path);
    int res = rap_panic_write_buffer_synced(target, ctx->raw_buffer, ctx->buffer_len, mode);

    if (res == 0) {
        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                 RAP_ANSI_BOLD "[✓] Sauvegarde synchro : '%.48s' (%u octets, fsync)" RAP_ANSI_RESET,
                 target, ctx->buffer_len);
        return 0;
    } else {
        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                 RAP_ANSI_RED "[!] Échec sauvegarde synchro vers '%.48s' (err: %d)" RAP_ANSI_RESET,
                 target, res);
        return res;
    }
}

/* 5. Sauvegarde de l'état disque d'origine */
int rap_panic_backup_disk_original(rap_context_t *ctx, const char *dest_path) {
    if (!ctx || ctx->file_path[0] == '\0') {
        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                 RAP_ANSI_YELLOW "[!] Aucun fichier associé au tampon." RAP_ANSI_RESET);
        return -1;
    }

    /* Dimensionné à RAP_MAX_PATH_LEN + 32 pour éliminer tout risque de troncature */
    char target[RAP_MAX_PATH_LEN + 32];
    if (dest_path && dest_path[0] != '\0') {
        strncpy(target, dest_path, sizeof(target) - 1);
        target[sizeof(target) - 1] = '\0';
    } else {
        snprintf(target, sizeof(target), "%s.orig.bak", ctx->file_path);
    }

    int res = rap_panic_copy_file_on_disk(ctx->file_path, target);
    if (res == 0) {
        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                 RAP_ANSI_BOLD "[✓] Clone original figé : '%.48s' (fsync)" RAP_ANSI_RESET, target);
        return 0;
    } else {
        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                 RAP_ANSI_RED "[!] Échec du clonage disque vers '%.48s'" RAP_ANSI_RESET, target);
        return res;
    }
}

/* 6. Parseur CLI pour la commande :bak */
int rap_panic_execute(rap_context_t *ctx, const char *args) {
    if (!ctx) return -1;

    while (args && *args == ' ') args++;

    if (!args || *args == '\0') {
        return rap_panic_backup_buffer(ctx, NULL);
    }

    if (strcmp(args, "orig") == 0 || strcmp(args, "disk") == 0) {
        return rap_panic_backup_disk_original(ctx, NULL);
    }

    return rap_panic_backup_buffer(ctx, args);
}

/* 7. Évacuation d'urgence :panic */
static int rap_cmd_panic_entry(rap_context_t *ctx, const char *args) {
    (void)args;
    if (!ctx) return -1;

    char panic_path[RAP_MAX_PATH_LEN + 64];
    snprintf(panic_path, sizeof(panic_path), "rap_panic_%d.dump", (int)getpid());

    int res = rap_panic_write_buffer_synced(panic_path, ctx->raw_buffer, ctx->buffer_len, 0600);
    if (res != 0) {
        snprintf(panic_path, sizeof(panic_path), "/tmp/rap_panic_%d.dump", (int)getpid());
        res = rap_panic_write_buffer_synced(panic_path, ctx->raw_buffer, ctx->buffer_len, 0600);
    }

    if (res == 0) {
        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                 RAP_ANSI_REVERSE " [PANIC] Tampon évacué d'urgence : '%.48s' " RAP_ANSI_RESET, panic_path);
    } else {
        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                 RAP_ANSI_RED "[CRITIQUE] Échec total de la vidange d'urgence !" RAP_ANSI_RESET);
    }
    return res;
}

/* 8. Point d'entrée pour :bak via le dispatcheur de greffons */
static int rap_cmd_bak_plugin_entry(rap_context_t *ctx, const char *args) {
    return rap_panic_execute(ctx, args);
}

/* 9. Enregistrement en table BSS */
int rap_plugin_mod_panic_init(void) {
    static rap_plugin_t plug_bak = {
        .cmd_trigger = "bak",
        .description = "Sauvegarde atomique synchro (:bak, :bak <nom>, :bak orig)",
        .execute     = rap_cmd_bak_plugin_entry
    };
    static rap_plugin_t plug_panic = {
        .cmd_trigger = "panic",
        .description = "Évacuation d'urgence du tampon brut (:panic)",
        .execute     = rap_cmd_panic_entry
    };

    rap_plugin_register(&plug_bak);
    rap_plugin_register(&plug_panic);
    return 0;
}
