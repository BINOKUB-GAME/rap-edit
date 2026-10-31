/* ==============================================================================
 * SCRIPT      : src/engine/rap_edit_core.c
 * RÉVISION    : Rev 1.2 (New File On-The-Fly Creation)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Prise en charge native de la création de nouveaux fichiers.
 *               Interception de errno == ENOENT pour initialiser un document
 *               vierge en mémoire BSS (Ligne 1 vide) sans interrompre l'éditeur.
 * FONCTIONS   :
 *   1. rap_core_init_context (Remise à zéro déterministe de l'état statique)
 *   2. rap_core_index_lines (Indexation BSS avec garantie d'au moins 1 ligne active)
 *   3. rap_core_load_file (Ouverture de fichier existant ou création d'un nouveau)
 * ============================================================================== */

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include "rap_edit_types.h"
#include "rap_edit_config.h"

/* 1. Initialisation déterministe du contexte BSS */
void rap_core_init_context(rap_context_t *ctx) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(rap_context_t));
    ctx->fd = -1;
}

/* 2. Indexation des lignes avec garantie d'une ligne 1 active sur fichier vide */
int rap_core_index_lines(rap_context_t *ctx) {
    if (!ctx) return 0;

    ctx->total_lines = 0;

    /* Sur document vide (nouveau fichier), initialise 1 ligne vide éditable */
    if (ctx->buffer_len == 0) {
        ctx->lines[0].offset = 0;
        ctx->lines[0].length = 0;
        ctx->lines[0].modified = false;
        ctx->total_lines = 1;
        return 1;
    }

    uint32_t line_start = 0;

    for (uint32_t i = 0; i < ctx->buffer_len; ++i) {
        if (ctx->raw_buffer[i] == '\n' || i == ctx->buffer_len - 1) {
            if (ctx->total_lines >= RAP_MAX_LINES) {
                return -1;
            }

            uint32_t end_pos = (ctx->raw_buffer[i] == '\n') ? i : i + 1;
            ctx->lines[ctx->total_lines].offset = line_start;
            ctx->lines[ctx->total_lines].length = (uint16_t)(end_pos - line_start);
            ctx->lines[ctx->total_lines].modified = false;
            
            ctx->total_lines++;
            line_start = i + 1;
        }
    }
    return (int)ctx->total_lines;
}

/* 3. Ouverture ou création d'un nouveau document */
int rap_core_load_file(rap_context_t *ctx, const char *filepath) {
    if (!ctx || !filepath) return -1;

    rap_core_init_context(ctx);
    strncpy(ctx->file_path, filepath, RAP_MAX_PATH_LEN - 1);

    ctx->fd = open(filepath, O_RDWR);
    if (ctx->fd < 0) {
        /* CAS 1 : Le fichier n'existe pas encore sur disque -> Nouveau fichier */
        if (errno == ENOENT) {
            ctx->fd = -1;
            ctx->file_size_on_disk = 0;
            ctx->inode_num = 0;
            ctx->buffer_len = 0;
            ctx->is_readonly = false;
            ctx->is_dirty = false;
            rap_core_index_lines(ctx);
            return 0; /* Chargement réussi en mode nouveau document */
        }

        /* CAS 2 : Fichier présent mais sans droit d'écriture -> Lecture seule */
        ctx->fd = open(filepath, O_RDONLY);
        if (ctx->fd < 0) {
            return -2; /* Erreur d'accès ou permissions système insuffisantes */
        }
        ctx->is_readonly = true;
    } else {
        ctx->is_readonly = false;
    }

    struct stat st;
    if (fstat(ctx->fd, &st) < 0) {
        close(ctx->fd);
        ctx->fd = -1;
        return -3;
    }

    ctx->file_size_on_disk = (uint64_t)st.st_size;
    ctx->inode_num = (uint64_t)st.st_ino;

    if (ctx->file_size_on_disk > RAP_STATIC_BUFFER_SIZE) {
        close(ctx->fd);
        ctx->fd = -1;
        return -4;
    }

    ssize_t bytes_read = read(ctx->fd, ctx->raw_buffer, ctx->file_size_on_disk);
    if (bytes_read < 0) {
        close(ctx->fd);
        ctx->fd = -1;
        return -5;
    }

    ctx->buffer_len = (uint32_t)bytes_read;
    ctx->is_dirty = false;

    rap_core_index_lines(ctx);
    return 0;
}
