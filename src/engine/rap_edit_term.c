/* ==============================================================================
 * SCRIPT      : src/engine/rap_edit_term.c
 * RÉVISION    : Rev 1.1 (Async SIGWINCH Signal Handler)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Capture du signal SIGWINCH via sigaction sans SA_RESTART pour
 *               interrompre read()/poll() avec EINTR dès que la taille change.
 * FONCTIONS   :
 *   1. rap_term_sigwinch_handler (Interruption matérielle atomique ultra-légère)
 *   2. rap_term_check_resize (Lecture et acquittement atomique du drapeau)
 *   3. rap_term_has_input (Sondage temporel non-bloquant sur STDIN via poll)
 *   4. rap_term_enable_raw (Configuration TTY brute et routage de SIGWINCH)
 *   5. rap_term_disable_raw (Restauration stricte de la configuration et signaux)
 * ============================================================================== */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <unistd.h>
#include <termios.h>
#include <poll.h>
#include <signal.h>
#include <string.h>

#include "rap_edit_term.h"

static struct termios    s_orig_termios;
static struct sigaction  s_orig_sigwinch;
static bool              s_is_raw = false;
static volatile sig_atomic_t s_winch_flag = 0;

/* 1. Gestionnaire de signal atomique (zéro I/O, zéro allocation) */
static void rap_term_sigwinch_handler(int sig) {
    (void)sig;
    s_winch_flag = 1;
}

/* 2. Lecture et acquittement du drapeau de redimensionnement */
bool rap_term_check_resize(void) {
    if (s_winch_flag) {
        s_winch_flag = 0;
        return true;
    }
    return false;
}

/* 3. Sondage non-bloquant sur STDIN */
bool rap_term_has_input(int timeout_ms) {
    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    pfd.revents = 0;
    return (poll(&pfd, 1, timeout_ms) > 0);
}

/* 4. Configuration TTY brute et routage SIGWINCH */
void rap_term_enable_raw(void) {
    if (s_is_raw) return;

    tcgetattr(STDIN_FILENO, &s_orig_termios);
    struct termios raw = s_orig_termios;

    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    write(STDOUT_FILENO, "\033[?1049h\033[?7l", 14);

    /* Enregistrement de SIGWINCH sans SA_RESTART pour réveiller read() */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = rap_term_sigwinch_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGWINCH, &sa, &s_orig_sigwinch);

    s_is_raw = true;
}

/* 5. Restauration de la configuration originelle et des signaux */
void rap_term_disable_raw(void) {
    if (!s_is_raw) return;

    sigaction(SIGWINCH, &s_orig_sigwinch, NULL);

    write(STDOUT_FILENO, "\033[?7h\033[?1049l\033[?25h", 20);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &s_orig_termios);
    s_is_raw = false;
}
