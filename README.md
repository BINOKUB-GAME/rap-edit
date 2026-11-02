# RAP-EDIT (RAPTOR DEFENSE COMPANION)

Éditeur de texte tactique en mode console, ultra-léger et autonome, conçu spécifiquement pour l'administration système critique, la reprise après sinistre (*disaster recovery*), les consoles de secours (*single-user mode*) et les environnements dégradés.

---

## 1. Philosophie & Architecture 0-Heap

RAP-EDIT a été conçu selon une règle absolue : **le déterminisme mémoire total**.

* **Zéro Allocation Dynamique (0-heap) :** Aucun appel à `malloc()`, `calloc()`, `realloc()` ou `free()`. L'intégralité des tampons de données, de la pile d'annulation et des descripteurs réside dans le segment de mémoire statique **BSS**.
* **Immunité aux pannes de tas :** Insensible à la fragmentation mémoire, aux fuites de mémoire (*memory leaks*) ou à la corruption du gestionnaire de tas de la libc.
* **Binaire Statique Autonome :** Compilé avec l'option `-static`, le binaire s'exécute directement sans dépendance dynamique externe, même sur des systèmes de fichiers corrompus où `ld-linux` ou les bibliothèques partagées sont inaccessibles.
* **Sécurité & Synchronisation Atomique :** Les écritures sur disque (`:w!`) s'appuient sur un appel explicite à `fsync()` garantissant l'écriture physique sur les secteurs disques.

---

## 2. Spécifications Techniques

| Paramètre | Valeur / Caractéristique |
| :--- | :--- |
| **Standard C** | C99 pur (`-std=c99 -pedantic`) |
| **Allocation dynamique** | **0 octet** (Strictement BSS / Statique) |
| **Capacité max par fichier** | 64 Ko (`RAP_STATIC_BUFFER_SIZE`) |
| **Nombre max de lignes** | 4 096 lignes indexées en table statique |
| **Documents simultanés** | Jusqu'à 4 onglets actifs (`#1` à `#4`) |
| **Presse-papiers** | Tampon BSS partagé unifié entre tous les onglets |
| **Compatibilité TTY** | Émulation VT100 / ANSI native, détection `SIGWINCH` via `ioctl` |
| **Encodage** | Décodage et affichage transparent des séquences UTF-8 |

---

## 3. Compilation et Installation

### Prérequis
* Un compilateur C compatible C99 (`gcc` ou `clang`).
* GNU Make.

### Compilation du binaire statique
```bash
make clean
make
```
Le binaire autonome optimisé et dépouillé (*stripped*) est produit dans `bin/rap-edit`.

### Installation sur le système (Optionnel)
```bash
sudo cp bin/rap-edit /usr/local/bin/
```

---

## 4. Démarrage Rapide

Ouvrir un ou plusieurs fichiers (jusqu'à 4 simultanément) :
```bash
# Édition simple
./bin/rap-edit /etc/fstab

# Édition multi-tampons avec bascule d'onglets
./bin/rap-edit /etc/network/interfaces /etc/hosts /etc/resolv.conf
```

---

## 5. Guide Opérationnel des Commandes

Toutes les commandes s'exécutent en saisissant `:` en mode navigation.

### A. Gestion des Onglets & Documents Multiples
| Commande / Touche | Action |
| :--- | :--- |
| <kbd>Ctrl</kbd> + <kbd>W</kbd> | Bascule circulaire immédiate vers l'onglet suivant (0 ms) |
| `:#1`, `:#2`, `:#3`, `:#4` | Saut direct vers l'onglet spécifié |
| `:n` ou `:next` | Document suivant |
| `:p` ou `:prev` | Document précédent |
| `:b` | Affiche l'état des tampons en mémoire et indicateurs de modification (`*`) |
| `:bd` ou `:close` | Ferme l'onglet actif (sécurisé : refuse si modifié non enregistré) |
| `:bd!` ou `:close!` | Force la fermeture de l'onglet actif sans sauvegarder |

### B. Presse-papiers & Déplacement de Texte
Le presse-papiers est **commun à tous les onglets**. Vous pouvez couper ou copier dans `#1` et coller dans `#3`.

#### 1. Mode Visuel Interactif
* <kbd>Shift</kbd> + <kbd>Flèches</kbd> : Étend la surbrillance de sélection à l'écran.
* <kbd>Ctrl</kbd> + <kbd>C</kbd> : Copie la sélection dans le presse-papiers.
* <kbd>Ctrl</kbd> + <kbd>X</kbd> : Coupe la sélection vers le presse-papiers.
* <kbd>Ctrl</kbd> + <kbd>V</kbd> : Colle le contenu à la position exacte du curseur.

#### 2. Mode CLI en 2 étapes
* **Étape 1 (Copie / Coupe) :**
  * `:c` / `:x` : Copie / Coupe la ligne courante.
  * `:<num>c` / `:<num>x` : Copie / Coupe une ligne spécifique (ex: `:10c`).
  * `:<deb>:<fin>c` / `:<deb>:<fin>x` : Copie / Coupe une plage complète (ex: `:1:10c`).
* **Étape 2 (Collage) :**
  * `:pp` : Colle le presse-papiers à la position courante du curseur.
  * `:<num>w` ou `:<num>pp` : Colle le contenu sous la ligne ciblée (ex: `:15w`, `:15pp`).

#### 3. Mode Macro Combinée (1 seule étape)
* `:<deb>:<fin>c:<dest>w` : Copie la plage et la colle à la destination (ex: `:1:10c:15w`).
* `:<deb>:<fin>x:<dest>w` : Déplace la plage (coupe + colle) vers la destination (ex: `:1:10x:15w`).
* `:<num>c:<dest>w` : Copie une ligne unique et la colle à la destination (ex: `:10c:15w`).

---

### C. Navigation & Défilement
| Raccourci | Fonction |
| :--- | :--- |
| <kbd>Flèches</kbd> | Déplacement unitaire du curseur |
| <kbd>Page-Up</kbd> / <kbd>Page-Down</kbd> | Défilement rapide d'une hauteur d'écran complète |
| <kbd>Ctrl</kbd> + <kbd>A</kbd> | Début de la ligne courante (colonne 0) |
| <kbd>Ctrl</kbd> + <kbd>E</kbd> | Fin de la ligne courante (EOL) |
| `:<num>` | Saut direct à la ligne indiquée (ex: `:42`) |
| `:$` | Saut direct à la dernière ligne du document |
| <kbd>Ctrl</kbd> + <kbd>G</kbd> / `:info` | Statistiques complètes (Ligne, octets, taille presse-papiers) |

---

### D. Suppression, Recherche & Substitution
| Commande | Action |
| :--- | :--- |
| `:<num>d` | Supprime une ligne unique (ex: `:5d`) |
| `:<deb>:<fin>d` | Supprime une plage de lignes (ex: `:12:45d`) |
| `:rch <motif>` | Recherche textuelle circulaire instantanée sans latence |
| `:s/ancien/nouveau/` | Remplace la 1ère occurrence sur la ligne active |
| `:s/ancien/nouveau/g` | Remplace toutes les occurrences sur la ligne active |
| `:%s/ancien/nouveau/g` | Remplace sur l'ensemble du document |
| `:<deb>:<fin>s/anc/nouv/g` | Remplace dans une plage précise de lignes |
| `:u` ou <kbd>Ctrl</kbd> + <kbd>Z</kbd> | Annulation / Rétablissement de la dernière action (*Undo/Redo*) |

---

### E. Entrées/Sorties & Arrêt
| Commande | Action |
| :--- | :--- |
| `:w!` | Sauvegarde atomique synchrone sur disque (`fsync()`) |
| `:q!` | Quitte immédiatement l'éditeur sans confirmation |
| `:help` ou `:?` | Ouvre l'écran d'aide détaillé dans le terminal |

---

## 6. Arborescence du Code Source

```text
rap-edit/
├── include/
│   ├── rap_edit_buffer.h    # Gestionnaire multi-onglets BSS
│   ├── rap_edit_config.h    # Limites architecturales et constantes ANSI
│   ├── rap_edit_delete.h    # Déclarations moteur de suppression
│   ├── rap_edit_events.h    # Boucle d'événements et décodage VT100
│   ├── rap_edit_hud.h       # Rendu du cockpit HUD (Lignes 1 et 2)
│   ├── rap_edit_ops.h       # Primitives d'édition et curseur UTF-8
│   ├── rap_edit_plugin.h    # Table d'enregistrement des greffons
│   ├── rap_edit_replace.h   # Moteur de substitution de chaînes
│   ├── rap_edit_term.h      # Contrôle du mode brut (raw mode) TTY
│   ├── rap_edit_types.h     # Structures globales (contexte, buffer, clip)
│   └── rap_edit_undo.h      # Instantanés d'annulation BSS
├── src/
│   ├── cli/
│   │   └── rap_edit_main.c   # Point d'entrée, dispatch CLI et boucle
│   ├── engine/
│   │   ├── rap_edit_core.c   # Chargement, indexation des lignes et I/O
│   │   ├── rap_edit_events.c # Routage prioritaire des touches et commandes
│   │   ├── rap_edit_hud.c    # Tableau de bord dynamique avec onglets
│   │   ├── rap_edit_ops.c    # Insertion, suppression de caractères, UTF-8
│   │   ├── rap_edit_reg.c    # Registre central des greffons
│   │   ├── rap_edit_term.c   # Gestion des signaux (SIGWINCH) et terminal
│   │   └── rap_edit_ui.c     # Moteur de rendu VT100 et écran d'aide
│   └── plugins/
│       ├── mod_buffer.c      # Multi-onglets (#1 à #4), :bd, :n, :p
│       ├── mod_clipboard.c   # Presse-papiers, :pp, macros de lignes
│       ├── mod_delete.c      # Suppression unitaire et par plage (:d)
│       ├── mod_io.c          # Sauvegarde atomique sécurisée (:w!)
│       ├── mod_nav.c         # Navigation BOL/EOL et sauts de lignes
│       ├── mod_replace.c     # Substitution de motifs (:s, :%s)
│       ├── mod_search.c      # Moteur de recherche circulaire (:rch)
│       ├── mod_text.c        # Utilitaires texte
│       └── mod_undo.c        # Moteur Undo/Redo instantané (:u)
├── Makefile                  # Chaîne de compilation statique 0-heap
└── README.md                 # Spécification technique opérationnelle
```

---

## 7. Licence

Projet développé sous architecture déterministe pour interventions d'urgence et environnements critiques. Code autonome libre d'utilisation et d'adaptation en environnement système.

## Installation rapide

```bash
# 1. Cloner le dépôt
git clone [https://github.com/BINOKUB-GAME/rap-edit.git](https://github.com/BINOKUB-GAME/rap-edit.git)
cd rap-edit

# 2. Compiler le binaire statique 0-heap
make

# 3. Installer dans le système
sudo make install
