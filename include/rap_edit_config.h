/* ==============================================================================
 * SCRIPT      : include/rap_edit_config.h
 * RÉVISION    : Rev 1.7 (256 KB Tactical Buffer & Line Capacity Expansion)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Extension du tampon statique de 64 Ko à 256 Ko (RAP_STATIC_BUFFER_SIZE)
 *               permettant d'éditer des configurations noyau (.config) et scripts
 *               volumineux. Extension corrélative de RAP_MAX_LINES à 8192 lignes.
 *               Maintien strict de l'architecture 0-heap BSS (zéro malloc).
 * FONCTIONS   : (Constantes architecturales, limites BSS et codes VT100 ANSI)
 * ============================================================================== */

#ifndef RAP_EDIT_CONFIG_H
#define RAP_EDIT_CONFIG_H

#define RAP_MAX_PATH_LEN        1024
#define RAP_STATIC_BUFFER_SIZE  (256 * 1024)   /* 262 144 octets par slot */
#define RAP_CLIPBOARD_CAPACITY  (256 * 1024)   /* Presse-papiers calé à 256 Ko */
#define RAP_MAX_LINES           8192           /* Indexation jusqu'à 8192 lignes */
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
