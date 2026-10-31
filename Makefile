# ==============================================================================
# SCRIPT      : Makefile
# RÉVISION    : Rev 2.3
# PROJET      : RAP-EDIT (RAPTOR DEFENSE COMPANION)
# INTÉGRATION : Ajout du greffon mod_sanitize.c (Assainissement brut 0-heap).
# ==============================================================================

CC ?= gcc
CFLAGS = -std=c99 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic -O2 -static -Iinclude

SRC = src/engine/rap_edit_core.c \
      src/engine/rap_edit_reg.c \
      src/engine/rap_edit_hud.c \
      src/engine/rap_edit_ui.c \
      src/engine/rap_edit_term.c \
      src/engine/rap_edit_ops.c \
      src/engine/rap_edit_events.c \
      src/plugins/mod_text.c \
      src/plugins/mod_io.c \
      src/plugins/mod_clipboard.c \
      src/plugins/mod_nav.c \
      src/plugins/mod_search.c \
      src/plugins/mod_undo.c \
      src/plugins/mod_delete.c \
      src/plugins/mod_replace.c \
      src/plugins/mod_buffer.c \
      src/plugins/mod_sanitize.c \
      src/cli/rap_edit_main.c

OBJ = $(SRC:.c=.o)
TARGET = bin/rap-edit

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJ)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $(OBJ)
	@strip $@
	@echo "[✓] Binaire autonome 0-heap statique généré : $(TARGET)"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)
