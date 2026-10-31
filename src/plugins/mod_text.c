/* ==============================================================================
 * SCRIPT      : src/plugins/mod_text.c
 * RÉVISION    : Rev 1.1
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Activation explicite de _POSIX_C_SOURCE 200809L pour le prototype
 *               de pread() sous compilation C99 stricte.
 * FONCTIONS   :
 *   1. rap_cmd_print (Affichage paginé ou borné des lignes avec numérotation)
 *   2. rap_cmd_comment (Neutralisation chirurgicale d'une ligne via '# ')
 *   3. rap_cmd_diff (Comparatif différentiel entre le tampon RAM et le disque)
 *   4. rap_plugin_mod_text_init (Enregistrement des greffons dans la table BSS)
 * ============================================================================== */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "rap_edit_plugin.h"
#include "rap_edit_config.h"

/* Prototype externe du réindexeur de lignes défini dans rap_edit_core.c */
extern int rap_core_index_lines(rap_context_t *ctx);

/* 1. Affichage borné ou complet des lignes indexées */
int rap_cmd_print(rap_context_t *ctx, const char *args) {
    if (!ctx || ctx->total_lines == 0) {
        printf("[i] Tampon vide ou aucune ligne indexée.\n");
        return 0;
    }

    uint32_t start_line = 1;
    uint32_t end_line = ctx->total_lines;

    /* Analyse des arguments optionnels : "p [debut] [fin]" */
    if (args && *args != '\0') {
        int parsed_start = 0;
        int parsed_end = 0;
        int count = sscanf(args, "%d %d", &parsed_start, &parsed_end);

        if (count >= 1 && parsed_start > 0) {
            start_line = (uint32_t)parsed_start;
            end_line = start_line;
        }
        if (count == 2 && parsed_end >= (int)start_line) {
            end_line = (uint32_t)parsed_end;
        }
    }

    if (start_line > ctx->total_lines) start_line = ctx->total_lines;
    if (end_line > ctx->total_lines)   end_line = ctx->total_lines;

    printf("\n--- AFFICHAGE LIGNES %u À %u ---\n", start_line, end_line);
    for (uint32_t i = start_line - 1; i < end_line; ++i) {
        uint32_t offset = ctx->lines[i].offset;
        uint16_t len = ctx->lines[i].length;

        printf("%4u | ", i + 1);

        if (len > 0) {
            fwrite(&ctx->raw_buffer[offset], 1, len, stdout);
            if (ctx->raw_buffer[offset + len - 1] != '\n') {
                putchar('\n');
            }
        } else {
            putchar('\n');
        }
    }
    printf("--------------------------------\n\n");
    return 0;
}

/* 2. Neutralisation chirurgicale d'une ligne de configuration par insertion d'un '#' */
int rap_cmd_comment(rap_context_t *ctx, const char *args) {
    if (!ctx) return -1;
    if (ctx->is_readonly) {
        printf("[!] ERREUR : Fichier ouvert en lecture seule. Modification refusée.\n");
        return -2;
    }
    if (!args || *args == '\0') {
        printf("[!] Syntaxe : c <numero_ligne>\n");
        return -1;
    }

    int target = atoi(args);
    if (target < 1 || (uint32_t)target > ctx->total_lines) {
        printf("[!] ERREUR : Indice de ligne hors limites (1 à %u).\n", ctx->total_lines);
        return -3;
    }

    uint32_t line_idx = (uint32_t)(target - 1);
    uint32_t offset = ctx->lines[line_idx].offset;
    uint16_t len = ctx->lines[line_idx].length;

    if (len > 0 && ctx->raw_buffer[offset] == '#') {
        printf("[i] La ligne %d débute déjà par un dièse (#).\n", target);
        return 0;
    }

    const uint32_t shift = 2;
    if (ctx->buffer_len + shift > RAP_STATIC_BUFFER_SIZE) {
        printf("[!] ERREUR CRITIQUE : Capacité BSS saturée. Impossible d'insérer.\n");
        return -4;
    }

    memmove(&ctx->raw_buffer[offset + shift],
            &ctx->raw_buffer[offset],
            ctx->buffer_len - offset);

    ctx->raw_buffer[offset] = '#';
    ctx->raw_buffer[offset + 1] = ' ';

    ctx->buffer_len += shift;
    ctx->is_dirty = true;

    rap_core_index_lines(ctx);

    printf("[✓] Ligne %d neutralisée avec succès (+2 octets insérés).\n", target);
    return 0;
}

/* 3. Analyse différentielle entre la mémoire BSS et les données sur disque */
int rap_cmd_diff(rap_context_t *ctx, const char *args) {
    (void)args;
    if (!ctx) return -1;

    if (!ctx->is_dirty) {
        printf("[+] Aucune modification : le tampon BSS est identique au disque.\n");
        return 0;
    }

    if (ctx->fd < 0) {
        printf("[!] Descripteur de fichier non valide pour le comparatif.\n");
        return -2;
    }

    printf("\n=== DIFFÉRENTIEL TAMPON RAM <-> DISQUE ===\n");
    printf("--- DISQUE : %s (%lu octets)\n", ctx->file_path, (unsigned long)ctx->file_size_on_disk);
    printf("+++ BSS RAM: %s (%u octets)\n", ctx->file_path, ctx->buffer_len);

    uint8_t disk_chunk[512];
    uint32_t disk_read_offset = 0;
    bool difference_found = false;

    for (uint32_t i = 0; i < ctx->total_lines; ++i) {
        uint32_t offset = ctx->lines[i].offset;
        uint16_t len = ctx->lines[i].length;

        if (len > sizeof(disk_chunk)) {
            continue;
        }

        ssize_t bytes_read = pread(ctx->fd, disk_chunk, len, disk_read_offset);
        if (bytes_read > 0) {
            if ((uint16_t)bytes_read != len || memcmp(disk_chunk, &ctx->raw_buffer[offset], len) != 0) {
                difference_found = true;
                printf("\n[Ligne %u altérée]\n", i + 1);
                
                printf(RAP_ANSI_RED "- DISQUE : ");
                fwrite(disk_chunk, 1, bytes_read, stdout);
                if (disk_chunk[bytes_read - 1] != '\n') putchar('\n');
                printf(RAP_ANSI_RESET);

                printf(RAP_ANSI_GREEN "+ RAM    : ");
                fwrite(&ctx->raw_buffer[offset], 1, len, stdout);
                if (ctx->raw_buffer[offset + len - 1] != '\n') putchar('\n');
                printf(RAP_ANSI_RESET);
            }
            disk_read_offset += bytes_read;
        }
    }

    if (!difference_found) {
        printf("[i] Décalage de taille globale détecté sans altération des lignes antérieures.\n");
    }
    printf("==========================================\n\n");
    return 0;
}

/* 4. Enregistrement des commandes texte dans le registre statique */
int rap_plugin_mod_text_init(void) {
    rap_plugin_t plug_print = {
        .cmd_trigger = "p",
        .description = "Affiche les lignes indexées (syntaxe: p [debut] [fin])",
        .execute     = rap_cmd_print
    };
    rap_plugin_register(&plug_print);

    rap_plugin_t plug_comment = {
        .cmd_trigger = "c",
        .description = "Commente d'urgence une ligne de configuration (syntaxe: c <ligne>)",
        .execute     = rap_cmd_comment
    };
    rap_plugin_register(&plug_comment);

    rap_plugin_t plug_diff = {
        .cmd_trigger = "diff",
        .description = "Affiche les différences entre le tampon BSS et le disque",
        .execute     = rap_cmd_diff
    };
    rap_plugin_register(&plug_diff);

    return 0;
}
