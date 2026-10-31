/* ==============================================================================
 * SCRIPT      : src/plugins/mod_search.c
 * RÉVISION    : Rev 1.2 (Bounded Precision Formatting)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Utilisation de %.48s sur s_last_pattern pour interdire tout
 *               avertissement GCC lors des confirmations de recherche.
 * FONCTIONS   :
 *   1. rap_search_find_pattern (Balayage linéaire circulaire 0-heap)
 *   2. rap_cmd_rch (Point d'entrée CLI pour la commande :rch [motif])
 *   3. rap_plugin_mod_search_init (Enregistrement du greffon en BSS)
 * ============================================================================== */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "rap_edit_types.h"
#include "rap_edit_config.h"
#include "rap_edit_plugin.h"
#include "rap_edit_ops.h"

static char s_last_pattern[RAP_MAX_LINE_INPUT];
static bool s_has_pattern = false;

/* 1. Balayage linéaire avec rebouclage circulaire */
static int rap_search_find_pattern(rap_context_t *ctx, const char *pattern) {
    if (!ctx || !pattern || ctx->buffer_len == 0) return -1;

    size_t pat_len = strlen(pattern);
    if (pat_len == 0 || pat_len > ctx->buffer_len) return -1;

    uint32_t start_off = rap_ops_get_cursor_offset(ctx) + 1;
    if (start_off >= ctx->buffer_len) {
        start_off = 0;
    }

    uint32_t buf_len = ctx->buffer_len;
    uint32_t match_off = UINT32_MAX;

    for (uint32_t i = start_off; i + pat_len <= buf_len; ++i) {
        if (memcmp(&ctx->raw_buffer[i], pattern, pat_len) == 0) {
            match_off = i;
            break;
        }
    }

    if (match_off == UINT32_MAX && start_off > 0) {
        uint32_t limit = (start_off + (uint32_t)pat_len > buf_len) ? buf_len : start_off;
        for (uint32_t i = 0; i + pat_len <= limit; ++i) {
            if (memcmp(&ctx->raw_buffer[i], pattern, pat_len) == 0) {
                match_off = i;
                break;
            }
        }
    }

    if (match_off != UINT32_MAX) {
        rap_ops_set_cursor_from_offset(ctx, match_off);
        rap_ops_clamp_cursor(ctx);
        return 0;
    }

    return -2;
}

/* 2. Point d'entrée de la commande :rch */
static int rap_cmd_rch(rap_context_t *ctx, const char *args) {
    if (!ctx) return -1;

    while (args && (*args == ' ' || *args == '\t')) {
        args++;
    }

    if (args && *args != '\0') {
        strncpy(s_last_pattern, args, sizeof(s_last_pattern) - 1);
        s_last_pattern[sizeof(s_last_pattern) - 1] = '\0';
        s_has_pattern = true;
    } else if (!s_has_pattern) {
        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                 RAP_ANSI_YELLOW "[!] Usage : :rch <chaine_a_chercher>" RAP_ANSI_RESET);
        return 0;
    }

    int res = rap_search_find_pattern(ctx, s_last_pattern);
    if (res == 0) {
        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                 RAP_ANSI_BOLD "[✓] Motif '%.48s' trouvé (Ligne %u)" RAP_ANSI_RESET,
                 s_last_pattern, ctx->cursor_row + 1);
    } else {
        snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                 RAP_ANSI_YELLOW "[!] Motif '%.48s' introuvable dans le document" RAP_ANSI_RESET,
                 s_last_pattern);
    }

    return 0;
}

/* 3. Enregistrement du greffon dans la table */
int rap_plugin_mod_search_init(void) {
    rap_plugin_t plug_rch = {
        .cmd_trigger = "rch",
        .description = "Recherche circulaire d'une chaîne (:rch <chaine>)",
        .execute     = rap_cmd_rch
    };
    rap_plugin_register(&plug_rch);

    return 0;
}
