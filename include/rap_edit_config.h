/* ==============================================================================
 * SCRIPT      : include/rap_edit_config.h
 * RÉVISION    : Rev 1.6 (Plugin Capacity & Color Constants Extension)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Extension de RAP_MAX_PLUGINS à 64 entrées statiques BSS pour
 *               interdire toute saturation lors de l'enregistrement de greffons.
 * FONCTIONS   : (Constantes architecturales et codes VT100 ANSI)
 * ============================================================================== */

#ifndef RAP_EDIT_CONFIG_H
#define RAP_EDIT_CONFIG_H

#define RAP_MAX_PATH_LEN        1024
#define RAP_STATIC_BUFFER_SIZE  (64 * 1024)
#define RAP_MAX_LINES           4096
#define RAP_MAX_PLUGINS         64
#define RAP_MAX_LINE_INPUT      256
#define RAP_MAX_BUFFERS         4

/* Séquences d'échappement ANSI VT100 */
#define RAP_ANSI_RESET          "\033[0m"
#define RAP_ANSI_BOLD           "\033[1m"
#define RAP_ANSI_REVERSE        "\033[7m"
#define RAP_ANSI_CYAN           "\033[36m"
#define RAP_ANSI_YELLOW         "\033[1;33m"
#define RAP_ANSI_RED            "\033[1;31m"
#define RAP_ANSI_GREEN          "\033[32m"
#define RAP_ANSI_MAGENTA        "\033[1;35m"
#define RAP_ANSI_COMMENT        "\033[90m"

#endif /* RAP_EDIT_CONFIG_H */
