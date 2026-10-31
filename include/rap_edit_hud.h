/* ==============================================================================
 * SCRIPT      : include/rap_edit_hud.h
 * RÉVISION    : Rev 1.1
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Interface normalisée du tableau de bord supérieur autonome (HUD).
 *               Fournit le prototype d'ancrage absolu sans retour à la ligne.
 * FONCTIONS   :
 *   1. rap_hud_render_to_buf (Rendu formaté du bandeau d'état sur 2 lignes fixes)
 * ============================================================================== */

#ifndef RAP_EDIT_HUD_H
#define RAP_EDIT_HUD_H

#include <stdint.h>
#include "rap_edit_types.h"

/* 1. Rendu du HUD dans le tampon de trame vidéo BSS */
void rap_hud_render_to_buf(const rap_context_t *ctx, char *frame_buf, uint32_t *frame_len, uint32_t max_buf_size);

#endif /* RAP_EDIT_HUD_H */
