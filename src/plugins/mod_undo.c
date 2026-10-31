/* ==============================================================================
 * SCRIPT      : src/plugins/mod_undo.c
 * RÉVISION    : Rev 1.1 (Zero-Sleep Undo Engine)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Suppression du délai sleep(1). Communication instantanée via
 *               ctx->status_msg lors de l'application ou en cas d'historique vide.
 * FONCTIONS   :
 *   1. rap_undo_reset_burst (Clôture d'une séquence de frappe consécutive)
 *   2. rap_undo_snapshot (Sauvegarde intégrale du tampon et curseur en BSS)
 *   3. rap_undo_snapshot_burst (Sauvegarde amorcée au premier caractère tapé)
 *   4. rap_undo_apply (Permutation bidirectionnelle Undo / Redo instantanée)
 *   5. rap_cmd_undo (Point d'entrée CLI pour la commande :u et :undo)
 *   6. rap_plugin_mod_undo_init (Enregistrement du greffon dans la table BSS)
 * ============================================================================== */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "rap_edit_undo.h"
#include "rap_edit_config.h"
#include "rap_edit_plugin.h"
#include "rap_edit_ops.h"

extern int rap_core_index_lines(rap_context_t *ctx);

static uint8_t  s_undo_buffer[RAP_STATIC_BUFFER_SIZE];
static uint32_t s_undo_len = 0;
static uint32_t s_undo_cursor_row = 0;
static uint32_t s_undo_cursor_col = 0;
static bool     s_undo_is_dirty = false;
static bool     s_undo_available = false;
static bool     s_in_burst = false;

static uint8_t  s_swap_chunk[4096];

void rap_undo_reset_burst(void) {
    s_in_burst = false;
}

void rap_undo_snapshot(const rap_context_t *ctx) {
    if (!ctx) return;

    memcpy(s_undo_buffer, ctx->raw_buffer, ctx->buffer_len);
    s_undo_len = ctx->buffer_len;
    s_undo_cursor_row = ctx->cursor_row;
    s_undo_cursor_col = ctx->cursor_col;
    s_undo_is_dirty = ctx->is_dirty;
    s_undo_available = true;
    s_in_burst = false;
}

void rap_undo_snapshot_burst(const rap_context_t *ctx) {
    if (!ctx) return;
    if (!s_in_burst) {
        rap_undo_snapshot(ctx);
        s_in_burst = true;
    }
}

int rap_undo_apply(rap_context_t *ctx) {
    if (!ctx || !s_undo_available) {
        if (ctx) {
            snprintf(ctx->status_msg, sizeof(ctx->status_msg),
                     RAP_ANSI_YELLOW "[!] Aucun état précédent à restaurer" RAP_ANSI_RESET);
        }
        return -1;
    }

    s_in_burst = false;

    uint32_t max_bytes = (ctx->buffer_len > s_undo_len) ? ctx->buffer_len : s_undo_len;
    for (uint32_t offset = 0; offset < max_bytes; offset += sizeof(s_swap_chunk)) {
        uint32_t chunk = sizeof(s_swap_chunk);
        if (offset + chunk > RAP_STATIC_BUFFER_SIZE) {
            chunk = RAP_STATIC_BUFFER_SIZE - offset;
        }

        memcpy(s_swap_chunk, &ctx->raw_buffer[offset], chunk);
        memcpy(&ctx->raw_buffer[offset], &s_undo_buffer[offset], chunk);
        memcpy(&s_undo_buffer[offset], s_swap_chunk, chunk);
    }

    uint32_t tmp_len = ctx->buffer_len;
    ctx->buffer_len = s_undo_len;
    s_undo_len = tmp_len;

    uint32_t tmp_row = ctx->cursor_row;
    ctx->cursor_row = s_undo_cursor_row;
    s_undo_cursor_row = tmp_row;

    uint32_t tmp_col = ctx->cursor_col;
    ctx->cursor_col = s_undo_cursor_col;
    s_undo_cursor_col = tmp_col;

    bool tmp_dirty = ctx->is_dirty;
    ctx->is_dirty = s_undo_is_dirty;
    s_undo_is_dirty = tmp_dirty;

    rap_core_index_lines(ctx);
    rap_ops_clamp_cursor(ctx);

    snprintf(ctx->status_msg, sizeof(ctx->status_msg),
             RAP_ANSI_BOLD "[✓] Annulation / Rétablissement effectué (Undo/Redo)" RAP_ANSI_RESET);

    return 0;
}

static int rap_cmd_undo(rap_context_t *ctx, const char *args) {
    (void)args;
    return rap_undo_apply(ctx);
}

int rap_plugin_mod_undo_init(void) {
    rap_plugin_t plug_u = {
        .cmd_trigger = "u",
        .description = "Annule la dernière opération (:u)",
        .execute     = rap_cmd_undo
    };
    rap_plugin_register(&plug_u);

    rap_plugin_t plug_undo = {
        .cmd_trigger = "undo",
        .description = "Annule la dernière opération (:undo)",
        .execute     = rap_cmd_undo
    };
    rap_plugin_register(&plug_undo);

    return 0;
}
