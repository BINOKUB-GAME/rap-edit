/* ==============================================================================
 * SCRIPT      : src/plugins/mod_buffer.c
 * RÉVISION    : Rev 1.1 (Wrestrict Decoupling & Stack Isolation)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Élimination de l'avertissement GCC -Wrestrict dans rap_cmd_tab_switch
 *               par découplage du nom de fichier dans un tampon de pile indépendant.
 *               Maintien strict du 0-heap BSS et de la synchronisation clipboard.
 * FONCTIONS   :
 *   1. rap_buffer_init (Remise à zéro des contextes BSS)
 *   2. rap_buffer_load_file (Lecture et indexation d'un document dans un slot)
 *   3. rap_buffer_get (Récupération sécurisée d'un slot mémoire)
 *   4. rap_buffer_get_active (Pointeur direct sur l'onglet actif)
 *   5. rap_buffer_get_active_idx (Lecture de l'indice actif)
 *   6. rap_buffer_set_active_idx (Changement d'onglet avec copie du presse-papiers)
 *   7. rap_buffer_cycle (Bascule relative +1 / -1)
 *   8. rap_buffer_get_count (Lecture du nombre de fichiers ouverts)
 *   9. rap_cmd_tab_switch (Point d'entrée pour :1..:4 et :#1..:#4 sans -Wrestrict)
 *  10. rap_cmd_tab_next (Point d'entrée pour :n et :next)
 *  11. rap_cmd_tab_prev (Point d'entrée pour :p et :prev)
 *  12. rap_cmd_tab_list (Point d'entrée pour :b / liste des tampons)
 *  13. rap_plugin_mod_buffer_init (Enregistrement des greffons dans la table BSS)
 * ============================================================================== */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "rap_edit_buffer.h"
#include "rap_edit_config.h"
#include "rap_edit_plugin.h"

extern int rap_core_load_file(rap_context_t *ctx, const char *filepath);

static rap_context_t s_buffers[RAP_MAX_BUFFERS];
static uint32_t      s_active_idx = 0;
static uint32_t      s_buffer_count = 0;

/* 1. Initialisation */
void rap_buffer_init(void) {
    memset(s_buffers, 0, sizeof(s_buffers));
    s_active_idx = 0;
    s_buffer_count = 0;
}

/* 2. Chargement d'un fichier dans un slot */
int rap_buffer_load_file(uint32_t idx, const char *filepath) {
    if (idx >= RAP_MAX_BUFFERS || !filepath) return -1;
    int res = rap_core_load_file(&s_buffers[idx], filepath);
    if (res == 0) {
        if (idx >= s_buffer_count) {
            s_buffer_count = idx + 1;
        }
    }
    return res;
}

/* 3. Accès à un slot */
rap_context_t* rap_buffer_get(uint32_t idx) {
    if (idx >= s_buffer_count) return NULL;
    return &s_buffers[idx];
}

/* 4. Contexte actif */
rap_context_t* rap_buffer_get_active(void) {
    return &s_buffers[s_active_idx];
}

/* 5. Indice actif */
uint32_t rap_buffer_get_active_idx(void) {
    return s_active_idx;
}

/* 6. Bascule d'onglet avec synchronisation automatique du presse-papiers */
int rap_buffer_set_active_idx(uint32_t idx) {
    if (idx >= s_buffer_count) return -1;
    if (idx != s_active_idx) {
        /* Propagation immédiate du presse-papiers vers le document cible */
        s_buffers[idx].clipboard = s_buffers[s_active_idx].clipboard;
        s_active_idx = idx;
    }
    return 0;
}

/* 7. Bascule circulaire */
void rap_buffer_cycle(int direction) {
    if (s_buffer_count <= 1) return;
    if (direction > 0) {
        uint32_t next = (s_active_idx + 1) % s_buffer_count;
        rap_buffer_set_active_idx(next);
    } else {
        uint32_t prev = (s_active_idx == 0) ? (s_buffer_count - 1) : (s_active_idx - 1);
        rap_buffer_set_active_idx(prev);
    }
}

/* 8. Nombre de documents */
uint32_t rap_buffer_get_count(void) {
    return s_buffer_count;
}

/* 9. Commande :1 .. :4 ou :#1 .. :#4 (Découplée pour éliminer -Wrestrict) */
static int rap_cmd_tab_switch(rap_context_t *ctx, const char *args) {
    (void)ctx;
    if (!args || *args == '\0') return -1;

    const char *p = args;
    if (*p == '#') p++;

    int target = *p - '0';
    if (target >= 1 && (uint32_t)target <= s_buffer_count) {
        rap_buffer_set_active_idx((uint32_t)(target - 1));
        write(STDOUT_FILENO, "\033[H\033[2J", 7);

        /* Copie préalable sur la pile locale pour garantir la disjonction mémoire */
        char target_name[64];
        strncpy(target_name, s_buffers[s_active_idx].file_path, sizeof(target_name) - 1);
        target_name[sizeof(target_name) - 1] = '\0';

        snprintf(s_buffers[s_active_idx].status_msg, sizeof(s_buffers[s_active_idx].status_msg),
                 RAP_ANSI_BOLD "[✓] Bascule vers l'onglet #%d : '%.48s'" RAP_ANSI_RESET,
                 target, target_name);
        return 0;
    }

    snprintf(s_buffers[s_active_idx].status_msg, sizeof(s_buffers[s_active_idx].status_msg),
             RAP_ANSI_YELLOW "[!] Onglet inexistant (max: #%u)" RAP_ANSI_RESET, s_buffer_count);
    return -2;
}

/* 10. Commande :n / :next */
static int rap_cmd_tab_next(rap_context_t *ctx, const char *args) {
    (void)ctx; (void)args;
    rap_buffer_cycle(1);
    write(STDOUT_FILENO, "\033[H\033[2J", 7);
    return 0;
}

/* 11. Commande :p / :prev */
static int rap_cmd_tab_prev(rap_context_t *ctx, const char *args) {
    (void)ctx; (void)args;
    rap_buffer_cycle(-1);
    write(STDOUT_FILENO, "\033[H\033[2J", 7);
    return 0;
}

/* 12. Commande :b (liste des documents) */
static int rap_cmd_tab_list(rap_context_t *ctx, const char *args) {
    (void)args;
    char list_buf[192];
    uint32_t off = 0;
    list_buf[0] = '\0';

    for (uint32_t i = 0; i < s_buffer_count; ++i) {
        int w = snprintf(list_buf + off, sizeof(list_buf) - off,
                         "[%u: %.16s%s] ",
                         i + 1, s_buffers[i].file_path, s_buffers[i].is_dirty ? "*" : "");
        if (w > 0 && off + (uint32_t)w < sizeof(list_buf)) {
            off += (uint32_t)w;
        }
    }

    snprintf(ctx->status_msg, sizeof(ctx->status_msg),
             RAP_ANSI_REVERSE " Onglets : %s " RAP_ANSI_RESET, list_buf);
    return 0;
}

/* 13. Enregistrement des déclencheurs dans la table des greffons */
int rap_plugin_mod_buffer_init(void) {
    static rap_plugin_t plug_t1 = { .cmd_trigger = "#1", .description = "Bascule onglet #1", .execute = rap_cmd_tab_switch };
    static rap_plugin_t plug_t2 = { .cmd_trigger = "#2", .description = "Bascule onglet #2", .execute = rap_cmd_tab_switch };
    static rap_plugin_t plug_t3 = { .cmd_trigger = "#3", .description = "Bascule onglet #3", .execute = rap_cmd_tab_switch };
    static rap_plugin_t plug_t4 = { .cmd_trigger = "#4", .description = "Bascule onglet #4", .execute = rap_cmd_tab_switch };
    static rap_plugin_t plug_nxt = { .cmd_trigger = "n",  .description = "Onglet suivant",    .execute = rap_cmd_tab_next };
    static rap_plugin_t plug_prv = { .cmd_trigger = "p",  .description = "Onglet précédent",  .execute = rap_cmd_tab_prev };
    static rap_plugin_t plug_lst = { .cmd_trigger = "b",  .description = "Liste des onglets", .execute = rap_cmd_tab_list };

    rap_plugin_register(&plug_t1);
    rap_plugin_register(&plug_t2);
    rap_plugin_register(&plug_t3);
    rap_plugin_register(&plug_t4);
    rap_plugin_register(&plug_nxt);
    rap_plugin_register(&plug_prv);
    rap_plugin_register(&plug_lst);

    return 0;
}
