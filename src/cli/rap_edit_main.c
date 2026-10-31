/* ==============================================================================
 * SCRIPT      : src/cli/rap_edit_main.c
 * RÉVISION    : Rev 3.7 (Sanitize Plugin Integration)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Ajout de l'enregistrement de rap_plugin_mod_sanitize_init
 *               dans la séquence d'amorçage des greffons BSS.
 * FONCTIONS   :
 *   1. main (Point d'entrée du binaire)
 * ============================================================================== */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>

#include "rap_edit_types.h"
#include "rap_edit_term.h"
#include "rap_edit_events.h"
#include "rap_edit_buffer.h"

extern int rap_plugin_mod_buffer_init(void);
extern int rap_plugin_mod_text_init(void);
extern int rap_plugin_mod_io_init(void);
extern int rap_plugin_mod_clipboard_init(void);
extern int rap_plugin_mod_nav_init(void);
extern int rap_plugin_mod_search_init(void);
extern int rap_plugin_mod_undo_init(void);
extern int rap_plugin_mod_delete_init(void);
extern int rap_plugin_mod_replace_init(void);
extern int rap_plugin_mod_sanitize_init(void);

/* 1. Point d'entrée du binaire */
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <fichier1> [fichier2] [fichier3] [fichier4]\n", argv[0]);
        return 1;
    }

    rap_buffer_init();

    /* Priorité aux tampons, suivi des greffons de transformation */
    rap_plugin_mod_buffer_init();
    rap_plugin_mod_text_init();
    rap_plugin_mod_io_init();
    rap_plugin_mod_clipboard_init();
    rap_plugin_mod_nav_init();
    rap_plugin_mod_search_init();
    rap_plugin_mod_undo_init();
    rap_plugin_mod_delete_init();
    rap_plugin_mod_replace_init();
    rap_plugin_mod_sanitize_init();

    int loaded_count = 0;
    for (int i = 1; i < argc && loaded_count < RAP_MAX_BUFFERS; ++i) {
        int err = rap_buffer_load_file((uint32_t)loaded_count, argv[i]);
        if (err == 0) {
            loaded_count++;
        } else {
            fprintf(stderr, "[!] Attention : Échec de chargement de '%s'\n", argv[i]);
        }
    }

    if (loaded_count == 0) {
        fprintf(stderr, "[!] Aucun fichier valide à éditer.\n");
        return 1;
    }

    rap_buffer_set_active_idx(0);

    atexit(rap_term_disable_raw);
    rap_term_enable_raw();

    rap_events_run_loop(rap_buffer_get_active());

    return 0;
}
