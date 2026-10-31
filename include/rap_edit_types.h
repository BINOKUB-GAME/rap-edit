/* ==============================================================================
 * SCRIPT      : include/rap_edit_types.h
 * RÉVISION    : Rev 1.6 (Horizontal Viewport Integration)
 * PROJET      : RAP-EDIT / RAPTOR DEFENSE COMPANION
 * INTÉGRATION : Ajout du décalage viewport_left pour la gestion du défilement
 *               horizontal fluide des lignes longues sans allocation dynamique.
 * FONCTIONS   :
 *   1. rap_line_t (Descripteur d'offset et longueur de ligne)
 *   2. rap_clipboard_t (Structure BSS du presse-papiers interne)
 *   3. rap_context_t (État global enrichi avec viewport_left et statut 256 o)
 * ============================================================================== */

#ifndef RAP_EDIT_TYPES_H
#define RAP_EDIT_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "rap_edit_config.h"

#define RAP_CLIPBOARD_CAPACITY (64 * 1024)

/* 1. Descripteur de ligne */
typedef struct {
    uint32_t offset;    /* Décalage en octets dans le tampon brut */
    uint16_t length;    /* Longueur de la ligne */
    bool     modified;  /* Drapeau de modification locale */
} rap_line_t;

/* 2. Presse-papiers interne résidant en BSS */
typedef struct {
    uint8_t  buffer[RAP_CLIPBOARD_CAPACITY];
    uint32_t len;
    bool     is_line_block; /* true = bloc de lignes entières, false = flux octets */
} rap_clipboard_t;

/* 3. Contexte global d'exécution */
typedef struct {
    int             fd;                                   /* Descripteur de fichier actif */
    char            file_path[RAP_MAX_PATH_LEN];         /* Chemin du fichier audité */
    uint64_t        file_size_on_disk;                   /* Taille constatée sur disque */
    uint64_t        inode_num;                           /* Inode système vérifié */
    uint8_t         raw_buffer[RAP_STATIC_BUFFER_SIZE];  /* Tampon statique BSS */
    uint32_t        buffer_len;                          /* Octets réels chargés */
    
    rap_line_t      lines[RAP_MAX_LINES];                /* Table des lignes */
    uint32_t        total_lines;                         /* Nombre de lignes indexées */
    
    rap_clipboard_t clipboard;                           /* Tampon de copie/coupe interne */

    /* Pilotage visuel et sélection continue */
    uint32_t        cursor_row;                          /* Ligne active */
    uint32_t        cursor_col;                          /* Colonne active */
    uint32_t        viewport_top;                        /* Fenêtre glissante verticale */
    uint32_t        viewport_left;                       /* Fenêtre glissante horizontale */
    int             term_rows;                           /* Hauteur de la grille console */
    int             term_cols;                           /* Largeur de la grille console */
    bool            in_command_mode;                     /* Indicateur de saisie invite */

    bool            is_selecting;                        /* Mode sélection visuelle actif */
    uint32_t        sel_start_row;                       /* Origine sélection : ligne */
    uint32_t        sel_start_col;                       /* Origine sélection : colonne */

    char            status_msg[256];                     /* Message d'état 0-heap sans troncature */

    bool            is_readonly;                         /* Statut protection lecture seule */
    bool            is_dirty;                            /* Modifications non synchronisées */
} rap_context_t;

#endif /* RAP_EDIT_TYPES_H */
