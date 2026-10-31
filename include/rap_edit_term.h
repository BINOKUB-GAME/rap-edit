/* ==============================================================================
 * SCRIPT      : include/rap_edit_term.h
 * RÉVISION    : Rev 1.1 (SIGWINCH Hardware Interface)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Ajout du drapeau atomique et de la primitive rap_term_check_resize()
 *               pour la détection non-bloquante du redimensionnement de fenêtre.
 * FONCTIONS   :
 *   1. rap_term_enable_raw (Bascule en mode brut et capture de SIGWINCH)
 *   2. rap_term_disable_raw (Restauration propre du mode canonique et signaux)
 *   3. rap_term_has_input (Sondage non-bloquant sur le descripteur STDIN via poll)
 *   4. rap_term_check_resize (Vérification et réinitialisation du drapeau SIGWINCH)
 * ============================================================================== */

#ifndef RAP_EDIT_TERM_H
#define RAP_EDIT_TERM_H

#include <stdbool.h>

void rap_term_enable_raw(void);
void rap_term_disable_raw(void);
bool rap_term_has_input(int timeout_ms);
bool rap_term_check_resize(void);

#endif /* RAP_EDIT_TERM_H */
