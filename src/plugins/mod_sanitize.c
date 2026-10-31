/* ==============================================================================
 * SCRIPT      : src/plugins/mod_sanitize.c
 * RÉVISION    : Rev 1.0 (Raw Sanitization & In-Place 0-Heap Stream Filtering)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Greffon de nettoyage brut pour fichiers système endommagés.
 *               Filtrage in-situ sans copie intermédiaire : :dos2unix, :d2u,
 *               :strip-null, :null, :clean. Sauvegarde préalable de l'état via Undo.
 * FONCTIONS   :
 *   1. rap_sanitize_dos2unix (Tassement mémoire éliminant les caractères '\r')
 *   2. rap_sanitize_strip_null (Tassement mémoire éliminant les octets 0x00)
 *   3. rap_sanitize_clean_all (Filtrage combiné immédiat '\r' + '\0')
 *   4. rap_cmd_dos2unix_entry (Point d'entrée CLI pour :dos2unix et :d2u)
 *   5. rap_cmd_strip_null_entry (Point d'entrée CLI pour :strip-null et :null)
 *   6. rap_cmd_clean_entry (Point d'entrée CLI pour la macro globale :clean)
 *   7. rap_plugin_mod_sanitize_init (Enregistrement dans la table des greffons)
 * ============================================================================== */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>

#include "rap_edit_sanitize.h"
#include "rap_edit_config.h"
#include "rap_edit_plugin.h"
#include "rap_edit_undo.h"
#include "rap_edit_ops.h"

extern int rap_core_index_lines(rap_context_t *ctx);

/* 1. Élimination des retours chariot Windows (\r / 0x0D) */
int rap_sanitize_dos2unix(rap_context_t *ctx) {
    if (!ctx || ctx->is_readonly) return -1;
    if (ctx->buffer_len == 0) return 0;

    /* Détection préalable pour éviter un snapshot Undo inutile */
    uint32_t cr_count = 0;
    for (uint32_t i = 0; i < ctx->buffer_len; ++i) {
        if (ctx->raw_buffer[i] == '\r') {
            cr_count++;
        }
    }

    if (cr_count == 0) {
        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                 RAP_ANSI_CYAN "[INFO] Aucun octet CR (\\r) détecté." RAP_ANSI_RESET);
        return 0;
    }

    rap_undo_snapshot(ctx);

    uint32_t w = 0;
    for (uint32_t r = 0; r < ctx->buffer_len; ++r) {
        if (ctx->raw_buffer[r] != '\r') {
            ctx->raw_buffer[w++] = ctx->raw_buffer[r];
        }
    }

    ctx->buffer_len = w;
    ctx->is_dirty = true;

    rap_core_index_lines(ctx);
    rap_ops_clamp_cursor(ctx);

    snprintf(ctx->status_msg, sizeof(ctx->status_msg),
             RAP_ANSI_BOLD "[✓] dos2unix : %u octet(s) CR supprimé(s)" RAP_ANSI_RESET,
             cr_count);
    return 0;
}

/* 2. Élimination des octets nuls (0x00 / \0) */
int rap_sanitize_strip_null(rap_context_t *ctx) {
    if (!ctx || ctx->is_readonly) return -1;
    if (ctx->buffer_len == 0) return 0;

    uint32_t null_count = 0;
    for (uint32_t i = 0; i < ctx->buffer_len; ++i) {
        if (ctx->raw_buffer[i] == '\0') {
            null_count++;
        }
    }

    if (null_count == 0) {
        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                 RAP_ANSI_CYAN "[INFO] Aucun octet nul (0x00) détecté." RAP_ANSI_RESET);
        return 0;
    }

    rap_undo_snapshot(ctx);

    uint32_t w = 0;
    for (uint32_t r = 0; r < ctx->buffer_len; ++r) {
        if (ctx->raw_buffer[r] != '\0') {
            ctx->raw_buffer[w++] = ctx->raw_buffer[r];
        }
    }

    ctx->buffer_len = w;
    ctx->is_dirty = true;

    rap_core_index_lines(ctx);
    rap_ops_clamp_cursor(ctx);

    snprintf(ctx->status_msg, sizeof(ctx->status_msg),
             RAP_ANSI_BOLD "[✓] strip-null : %u octet(s) NUL supprimé(s)" RAP_ANSI_RESET,
             null_count);
    return 0;
}

/* 3. Assainissement combiné '\r' et '\0' en une seule passe */
int rap_sanitize_clean_all(rap_context_t *ctx) {
    if (!ctx || ctx->is_readonly) return -1;
    if (ctx->buffer_len == 0) return 0;

    uint32_t bad_bytes = 0;
    uint32_t cr_count = 0;
    uint32_t null_count = 0;

    for (uint32_t i = 0; i < ctx->buffer_len; ++i) {
        unsigned char c = ctx->raw_buffer[i];
        if (c == '\r') {
            bad_bytes++;
            cr_count++;
        } else if (c == '\0') {
            bad_bytes++;
            null_count++;
        }
    }

    if (bad_bytes == 0) {
        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                 RAP_ANSI_CYAN "[INFO] Tampon sain : aucun octet corrompu (CR / NUL)." RAP_ANSI_RESET);
        return 0;
    }

    rap_undo_snapshot(ctx);

    uint32_t w = 0;
    for (uint32_t r = 0; r < ctx->buffer_len; ++r) {
        unsigned char c = ctx->raw_buffer[r];
        if (c != '\r' && c != '\0') {
            ctx->raw_buffer[w++] = ctx->raw_buffer[r];
        }
    }

    ctx->buffer_len = w;
    ctx->is_dirty = true;

    rap_core_index_lines(ctx);
    rap_ops_clamp_cursor(ctx);

    snprintf(ctx->status_msg, sizeof(ctx->status_msg),
             RAP_ANSI_BOLD "[✓] clean : %u octet(s) purgé(s) (CR: %u, NUL: %u)" RAP_ANSI_RESET,
             bad_bytes, cr_count, null_count);
    return 0;
}

/* 4. Point d'entrée CLI pour :dos2unix et :d2u */
static int rap_cmd_dos2unix_entry(rap_context_t *ctx, const char *args) {
    (void)args;
    return rap_sanitize_dos2unix(ctx);
}

/* 5. Point d'entrée CLI pour :strip-null et :null */
static int rap_cmd_strip_null_entry(rap_context_t *ctx, const char *args) {
    (void)args;
    return rap_sanitize_strip_null(ctx);
}

/* 6. Point d'entrée CLI pour :clean */
static int rap_cmd_clean_entry(rap_context_t *ctx, const char *args) {
    (void)args;
    return rap_sanitize_clean_all(ctx);
}

/* 7. Enregistrement dans la table des greffons */
int rap_plugin_mod_sanitize_init(void) {
    static rap_plugin_t plug_d2u = {
        .cmd_trigger = "dos2unix",
        .description = "Convertit les fins de ligne CRLF en LF (supprime \\r)",
        .execute     = rap_cmd_dos2unix_entry
    };
    static rap_plugin_t plug_d2u_short = {
        .cmd_trigger = "d2u",
        .description = "Raccourci pour :dos2unix",
        .execute     = rap_cmd_dos2unix_entry
    };
    static rap_plugin_t plug_null = {
        .cmd_trigger = "strip-null",
        .description = "Supprime tous les octets nuls (0x00)",
        .execute     = rap_cmd_strip_null_entry
    };
    static rap_plugin_t plug_null_short = {
        .cmd_trigger = "null",
        .description = "Raccourci pour :strip-null",
        .execute     = rap_cmd_strip_null_entry
    };
    static rap_plugin_t plug_clean = {
        .cmd_trigger = "clean",
        .description = "Purger tous les octets parasites (\\r et \\0)",
        .execute     = rap_cmd_clean_entry
    };

    rap_plugin_register(&plug_d2u);
    rap_plugin_register(&plug_d2u_short);
    rap_plugin_register(&plug_null);
    rap_plugin_register(&plug_null_short);
    rap_plugin_register(&plug_clean);

    return 0;
}
