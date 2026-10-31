/* ==============================================================================
 * SCRIPT      : src/plugins/mod_buffer.c
 * RÉVISION    : Rev 1.4 (Wrestrict Elimination in Next/Prev Cycling)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Découplage du nom de fichier sur la pile locale dans rap_cmd_tab_next
 *               et rap_cmd_tab_prev pour supprimer formellement les avertissements
 *               GCC -Wrestrict. Maintien strict de la conformité 0-heap BSS.
 * FONCTIONS   :
 *   1. rap_buffer_init (Remise à zéro des contextes BSS)
 *   2. rap_buffer_load_file (Lecture et indexation d'un document dans un slot)
 *   3. rap_buffer_get (Récupération sécurisée d'un slot mémoire)
 *   4. rap_buffer_get_active (Pointeur direct sur l'onglet actif)
 *   5. rap_buffer_get_active_idx (Lecture de l'indice actif)
 *   6. rap_buffer_set_active_idx (Changement d'onglet avec copie du presse-papiers)
 *   7. rap_buffer_cycle (Bascule relative +1 / -1)
 *   8. rap_buffer_get_count (Lecture du nombre de fichiers ouverts)
 *   9. rap_buffer_close_active (Fermeture et compactage statique du tableau BSS)
 *  10. rap_buffer_switch_to (Bascule ciblée vers un indice d'onglet 1-based)
 *  11. rap_cmd_tab_1 (Point d'entrée direct onglet #1)
 *  12. rap_cmd_tab_2 (Point d'entrée direct onglet #2)
 *  13. rap_cmd_tab_3 (Point d'entrée direct onglet #3)
 *  14. rap_cmd_tab_4 (Point d'entrée direct onglet #4)
 *  15. rap_cmd_tab_switch (Point d'entrée paramétré :tab <num>)
 *  16. rap_cmd_tab_next (Point d'entrée :n et :next isolé de tout alias mémoire)
 *  17. rap_cmd_tab_prev (Point d'entrée :p et :prev isolé de tout alias mémoire)
 *  18. rap_cmd_tab_list (Point d'entrée pour :b / liste des tampons)
 *  19. rap_cmd_tab_close (Point d'entrée pour :bd et :close)
 *  20. rap_cmd_tab_close_force (Point d'entrée pour :bd! et :close!)
 *  21. rap_plugin_mod_buffer_init (Enregistrement des greffons dans la table BSS)
 * ============================================================================== */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
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

/* 2. Chargement d'un fichier */
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

/* 9. Fermeture atomique et compactage de la mémoire BSS */
int rap_buffer_close_active(bool force) {
    if (s_buffer_count == 0) return 0;

    rap_context_t *cur = &s_buffers[s_active_idx];
    if (cur->is_dirty && !force) {
        snprintf(cur->status_msg, sizeof(cur->status_msg),
                 RAP_ANSI_YELLOW "[!] Modifications en cours. Utilisez :w! ou :bd! pour forcer" RAP_ANSI_RESET);
        return -1;
    }

    if (s_buffer_count == 1) {
        exit(0);
    }

    rap_clipboard_t keep_clip = cur->clipboard;

    for (uint32_t i = s_active_idx; i + 1 < s_buffer_count; ++i) {
        s_buffers[i] = s_buffers[i + 1];
    }

    memset(&s_buffers[s_buffer_count - 1], 0, sizeof(rap_context_t));
    s_buffer_count--;

    if (s_active_idx >= s_buffer_count) {
        s_active_idx = s_buffer_count - 1;
    }

    s_buffers[s_active_idx].clipboard = keep_clip;
    write(STDOUT_FILENO, "\033[H\033[2J", 7);

    snprintf(s_buffers[s_active_idx].status_msg, sizeof(s_buffers[s_active_idx].status_msg),
             RAP_ANSI_BOLD "[✓] Onglet fermé avec succès" RAP_ANSI_RESET);
    return 0;
}

/* 10. Bascule ciblée vers un indice d'onglet (1-based) */
static int rap_buffer_switch_to(uint32_t target) {
    if (target >= 1 && target <= s_buffer_count) {
        rap_buffer_set_active_idx(target - 1);
        write(STDOUT_FILENO, "\033[H\033[2J", 7);

        char target_name[64];
        strncpy(target_name, s_buffers[s_active_idx].file_path, sizeof(target_name) - 1);
        target_name[sizeof(target_name) - 1] = '\0';

        snprintf(s_buffers[s_active_idx].status_msg, sizeof(s_buffers[s_active_idx].status_msg),
                 RAP_ANSI_BOLD "[✓] Bascule vers l'onglet #%u : '%.48s'" RAP_ANSI_RESET,
                 target, target_name);
        return 0;
    }

    snprintf(s_buffers[s_active_idx].status_msg, sizeof(s_buffers[s_active_idx].status_msg),
             RAP_ANSI_YELLOW "[!] Onglet inexistant (max: #%u)" RAP_ANSI_RESET, s_buffer_count);
    return -2;
}

/* 11. Point d'entrée direct onglet #1 */
static int rap_cmd_tab_1(rap_context_t *ctx, const char *args) {
    (void)ctx; (void)args;
    return rap_buffer_switch_to(1);
}

/* 12. Point d'entrée direct onglet #2 */
static int rap_cmd_tab_2(rap_context_t *ctx, const char *args) {
    (void)ctx; (void)args;
    return rap_buffer_switch_to(2);
}

/* 13. Point d'entrée direct onglet #3 */
static int rap_cmd_tab_3(rap_context_t *ctx, const char *args) {
    (void)ctx; (void)args;
    return rap_buffer_switch_to(3);
}

/* 14. Point d'entrée direct onglet #4 */
static int rap_cmd_tab_4(rap_context_t *ctx, const char *args) {
    (void)ctx; (void)args;
    return rap_buffer_switch_to(4);
}

/* 15. Point d'entrée paramétré :tab <num> */
static int rap_cmd_tab_switch(rap_context_t *ctx, const char *args) {
    (void)ctx;
    if (!args || *args == '\0') return -1;
    const char *p = args;
    while (*p == ' ' || *p == '#') p++;
    int target = *p - '0';
    return rap_buffer_switch_to((uint32_t)target);
}

/* 16. Commande :n et :next (Découplée pour éliminer -Wrestrict) */
static int rap_cmd_tab_next(rap_context_t *ctx, const char *args) {
    (void)ctx; (void)args;
    rap_buffer_cycle(1);
    write(STDOUT_FILENO, "\033[H\033[2J", 7);

    char target_name[64];
    strncpy(target_name, s_buffers[s_active_idx].file_path, sizeof(target_name) - 1);
    target_name[sizeof(target_name) - 1] = '\0';

    snprintf(s_buffers[s_active_idx].status_msg, sizeof(s_buffers[s_active_idx].status_msg),
             RAP_ANSI_BOLD "[✓] Onglet #%u : '%.48s'" RAP_ANSI_RESET,
             s_active_idx + 1, target_name);
    return 0;
}

/* 17. Commande :p et :prev (Découplée pour éliminer -Wrestrict) */
static int rap_cmd_tab_prev(rap_context_t *ctx, const char *args) {
    (void)ctx; (void)args;
    rap_buffer_cycle(-1);
    write(STDOUT_FILENO, "\033[H\033[2J", 7);

    char target_name[64];
    strncpy(target_name, s_buffers[s_active_idx].file_path, sizeof(target_name) - 1);
    target_name[sizeof(target_name) - 1] = '\0';

    snprintf(s_buffers[s_active_idx].status_msg, sizeof(s_buffers[s_active_idx].status_msg),
             RAP_ANSI_BOLD "[✓] Onglet #%u : '%.48s'" RAP_ANSI_RESET,
             s_active_idx + 1, target_name);
    return 0;
}

/* 18. Commande :b (liste des documents) */
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

/* 19. Commande :bd et :close */
static int rap_cmd_tab_close(rap_context_t *ctx, const char *args) {
    (void)ctx;
    bool force = false;
    if (args && (strcmp(args, "!") == 0)) force = true;
    return rap_buffer_close_active(force);
}

/* 20. Commande :bd! et :close! (Fermeture forcée garantie) */
static int rap_cmd_tab_close_force(rap_context_t *ctx, const char *args) {
    (void)ctx; (void)args;
    return rap_buffer_close_active(true);
}

/* 21. Enregistrement des greffons dans la table BSS */
int rap_plugin_mod_buffer_init(void) {
    static rap_plugin_t plug_t1   = { .cmd_trigger = "#1",     .description = "Bascule onglet #1",  .execute = rap_cmd_tab_1 };
    static rap_plugin_t plug_t2   = { .cmd_trigger = "#2",     .description = "Bascule onglet #2",  .execute = rap_cmd_tab_2 };
    static rap_plugin_t plug_t3   = { .cmd_trigger = "#3",     .description = "Bascule onglet #3",  .execute = rap_cmd_tab_3 };
    static rap_plugin_t plug_t4   = { .cmd_trigger = "#4",     .description = "Bascule onglet #4",  .execute = rap_cmd_tab_4 };
    static rap_plugin_t plug_tab  = { .cmd_trigger = "tab",    .description = "Bascule par index",   .execute = rap_cmd_tab_switch };
    static rap_plugin_t plug_n    = { .cmd_trigger = "n",      .description = "Onglet suivant",     .execute = rap_cmd_tab_next };
    static rap_plugin_t plug_nxt  = { .cmd_trigger = "next",   .description = "Onglet suivant",     .execute = rap_cmd_tab_next };
    static rap_plugin_t plug_p    = { .cmd_trigger = "p",      .description = "Onglet précédent",   .execute = rap_cmd_tab_prev };
    static rap_plugin_t plug_prv  = { .cmd_trigger = "prev",   .description = "Onglet précédent",   .execute = rap_cmd_tab_prev };
    static rap_plugin_t plug_lst  = { .cmd_trigger = "b",      .description = "Liste des onglets",  .execute = rap_cmd_tab_list };
    static rap_plugin_t plug_bd   = { .cmd_trigger = "bd",     .description = "Fermer onglet",      .execute = rap_cmd_tab_close };
    static rap_plugin_t plug_bdf  = { .cmd_trigger = "bd!",    .description = "Fermer (forcer)",    .execute = rap_cmd_tab_close_force };
    static rap_plugin_t plug_cls  = { .cmd_trigger = "close",  .description = "Fermer onglet",      .execute = rap_cmd_tab_close };
    static rap_plugin_t plug_clsf = { .cmd_trigger = "close!", .description = "Fermer (forcer)",    .execute = rap_cmd_tab_close_force };

    rap_plugin_register(&plug_t1);
    rap_plugin_register(&plug_t2);
    rap_plugin_register(&plug_t3);
    rap_plugin_register(&plug_t4);
    rap_plugin_register(&plug_tab);
    rap_plugin_register(&plug_n);
    rap_plugin_register(&plug_nxt);
    rap_plugin_register(&plug_p);
    rap_plugin_register(&plug_prv);
    rap_plugin_register(&plug_lst);
    rap_plugin_register(&plug_bd);
    rap_plugin_register(&plug_bdf);
    rap_plugin_register(&plug_cls);
    rap_plugin_register(&plug_clsf);

    return 0;
}
