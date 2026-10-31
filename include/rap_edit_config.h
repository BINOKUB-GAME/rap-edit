/* ==============================================================================
 * SCRIPT      : include/rap_edit_config.h
 * RÉVISION    : Rev 1.5 (Multi-Buffer Limit Configuration)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Définition de RAP_MAX_BUFFERS (4 documents simultanés en BSS).
 * FONCTIONS   : (Constantes et limites architecturales 0-heap)
 * ============================================================================== */

#ifndef RAP_EDIT_CONFIG_H
#define RAP_EDIT_CONFIG_H

#define RAP_MAX_PATH_LEN        1024
#define RAP_STATIC_BUFFER_SIZE  (64 * 1024)
#define RAP_MAX_LINES           4096
#define RAP_MAX_PLUGINS         32
#define RAP_MAX_LINE_INPUT      256
#define RAP_MAX_BUFFERS         4

/* Séquences d'échappement ANSI VT100 */
#define RAP_ANSI_RESET          "\033[0m"
#define RAP_ANSI_BOLD           "\033[1m"
#define RAP_ANSI_REVERSE        "\033[7m"
#define RAP_ANSI_CYAN           "\033[36m"
#define RAP_ANSI_YELLOW         "\033[1;33m"
#define RAP_ANSI_RED            "\033[31m"
#define RAP_ANSI_GREEN          "\033[32m"
#define RAP_ANSI_COMMENT        "\033[90m"

#endif /* RAP_EDIT_CONFIG_H */
