/* ==============================================================================
 * SCRIPT      : src/engine/rap_edit_hud.c
 * RÉVISION    : Rev 1.4 (Encoding Radar & Corruption HUD Badges)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Analyse temps réel du tampon pour afficher l'indicateur de fin
 *               de ligne [CRLF] (alerte Windows) / [LF] (Unix sain) et un badge
 *               rouge d'alarme [!NUL] si des octets corrompus sont présents.
 * FONCTIONS   :
 *   1. rap_hud_append_str (Injection sécurisée de chaîne dans le tampon BSS)
 *   2. rap_hud_get_basename (Extraction du nom de fichier pur sans son arborescence)
 *   3. rap_hud_render_to_buf (Formatage et écriture atomique du HUD enrichi)
 * ============================================================================== */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include "rap_edit_hud.h"
#include "rap_edit_config.h"
#include "rap_edit_buffer.h"

#define RAP_HUD_ROW_BUF_SIZE (RAP_MAX_PATH_LEN + 256)

/* 1. Concaténation bornée dans le tampon d'affichage BSS */
static void rap_hud_append_str(char *buf, uint32_t *len, uint32_t max_size, const char *str) {
    size_t slen = strlen(str);
    if (*len + slen >= max_size) return;
    memcpy(&buf[*len], str, slen);
    *len += (uint32_t)slen;
}

/* 2. Extraction du nom de fichier pur sans son chemin */
static const char* rap_hud_get_basename(const char *path) {
    if (!path || path[0] == '\0') return "<VIDE>";
    const char *p = strrchr(path, '/');
    return p ? (p + 1) : path;
}

/* 3. Formatage et écriture des deux lignes fixes du cockpit HUD */
void rap_hud_render_to_buf(const rap_context_t *ctx, char *frame_buf, uint32_t *frame_len, uint32_t max_buf_size) {
    if (!ctx || !frame_buf || !frame_len) return;

    char cwd_buf[RAP_MAX_PATH_LEN];
    if (getcwd(cwd_buf, sizeof(cwd_buf)) == NULL) {
        snprintf(cwd_buf, sizeof(cwd_buf), "[INCONNU]");
    }

    uid_t current_uid = geteuid();
    const char *priv_label = (current_uid == 0) ? "ROOT (#)" : "USER ($)";
    const char *state_label = ctx->is_dirty ? "[MODIFIÉ]" : "[SYNCHRONISÉ]";
    const char *mode_label = ctx->is_readonly ? "[RO]" : "[RW]";

    /* Analyse d'intégrité du tampon actif (CRLF / NUL) */
    bool has_crlf = false;
    bool has_null = false;
    for (uint32_t k = 0; k < ctx->buffer_len; ++k) {
        if (ctx->raw_buffer[k] == '\r') has_crlf = true;
        if (ctx->raw_buffer[k] == '\0') has_null = true;
        if (has_crlf && has_null) break;
    }

    /* Construction de la barre d'onglets dynamique */
    char tabs_buf[128];
    uint32_t t_off = 0;
    tabs_buf[0] = '\0';

    uint32_t count = rap_buffer_get_count();
    uint32_t active = rap_buffer_get_active_idx();

    for (uint32_t i = 0; i < count; ++i) {
        rap_context_t *b = rap_buffer_get(i);
        const char *name = b ? rap_hud_get_basename(b->file_path) : "vide";
        const char *dirty = (b && b->is_dirty) ? "*" : "";

        int w = 0;
        if (i == active) {
            w = snprintf(tabs_buf + t_off, sizeof(tabs_buf) - t_off,
                         "[%u: %.12s%s] ", i + 1, name, dirty);
        } else {
            w = snprintf(tabs_buf + t_off, sizeof(tabs_buf) - t_off,
                         "#%u: %.12s%s ", i + 1, name, dirty);
        }
        if (w > 0 && t_off + (uint32_t)w < sizeof(tabs_buf)) {
            t_off += (uint32_t)w;
        }
    }

    char row_buf[RAP_HUD_ROW_BUF_SIZE];

    /* --- LIGNE 1 : Onglets, Privilèges et Répertoire --- */
    rap_hud_append_str(frame_buf, frame_len, max_buf_size, "\033[1;1H" RAP_ANSI_REVERSE);
    snprintf(row_buf, sizeof(row_buf), " RAP-EDIT | %s| AUTH: %s | PWD: %s",
             tabs_buf[0] ? tabs_buf : "",
             priv_label,
             cwd_buf);
    if ((int)strlen(row_buf) >= ctx->term_cols) {
        row_buf[ctx->term_cols - 1] = '\0';
    }
    rap_hud_append_str(frame_buf, frame_len, max_buf_size, row_buf);
    rap_hud_append_str(frame_buf, frame_len, max_buf_size, "\033[K" RAP_ANSI_RESET);

    /* --- LIGNE 2 : Détails techniques, Format LF/CRLF et Alarme NUL --- */
    rap_hud_append_str(frame_buf, frame_len, max_buf_size, "\033[2;1H" RAP_ANSI_REVERSE);
    snprintf(row_buf, sizeof(row_buf), " CIBLE: %-16.16s | L: %u/%u | %u o | %s | %s%s | %s",
             rap_hud_get_basename(ctx->file_path),
             ctx->cursor_row + 1,
             ctx->total_lines,
             ctx->buffer_len,
             mode_label,
             has_crlf ? "[CRLF/DOS]" : "[LF/UNIX]",
             has_null ? " [!NUL]" : "",
             state_label);
    if ((int)strlen(row_buf) >= ctx->term_cols) {
        row_buf[ctx->term_cols - 1] = '\0';
    }
    rap_hud_append_str(frame_buf, frame_len, max_buf_size, row_buf);
    rap_hud_append_str(frame_buf, frame_len, max_buf_size, "\033[K" RAP_ANSI_RESET);
}
