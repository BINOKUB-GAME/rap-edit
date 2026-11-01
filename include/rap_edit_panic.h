/* ==============================================================================
 * SCRIPT      : include/rap_edit_panic.h
 * RÉVISION    : Rev 1.0 (Emergency Atomic Backup Interface)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Déclarations pour les sauvegardes de secours matérielles :
 *               synchronisation physique fsync, copie d'urgence 0-heap,
 *               duplication de l'original sain et commandes :bak / :panic.
 * FONCTIONS   :
 *   1. rap_panic_backup_buffer (Écriture synchrone du tampon en mémoire vers .bak)
 *   2. rap_panic_backup_disk_original (Copie physique bloc par bloc de l'original)
 *   3. rap_panic_execute (Aiguillage de la commande CLI :bak avec arguments)
 *   4. rap_plugin_mod_panic_init (Enregistrement du greffon dans la table BSS)
 * ============================================================================== */

#ifndef RAP_EDIT_PANIC_H
#define RAP_EDIT_PANIC_H

#include <stdint.h>
#include <stdbool.h>
#include "rap_edit_types.h"

int rap_panic_backup_buffer(rap_context_t *ctx, const char *dest_path);
int rap_panic_backup_disk_original(rap_context_t *ctx, const char *dest_path);
int rap_panic_execute(rap_context_t *ctx, const char *args);
int rap_plugin_mod_panic_init(void);

#endif /* RAP_EDIT_PANIC_H */
