# ==============================================================================
# SCRIPT      : Makefile
# RÉVISION    : Rev 2.8 (Global System Installation & UNIX Man Page Deployment)
# PROJET      : RAP-EDIT (RAPTOR DEFENSE COMPANION)
# INTÉGRATION : Prise en charge des variables standard POSIX (PREFIX, BINDIR, MANDIR, DESTDIR).
#               Déploiement conjoint du binaire statique épuré (755) dans BINDIR et de la
#               page de manuel UNIX doc/rap-edit.1 (644) dans MANDIR.
#               Cible uninstall symétrique et mise à jour de la directive .PHONY.
# ==============================================================================

CC ?= gcc
CFLAGS = -std=c99 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic -O2 -static -Iinclude

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/share/man/man1

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
      src/plugins/mod_panic.c \
      src/plugins/mod_diff.c \
      src/plugins/mod_hex.c \
      src/plugins/mod_protect.c \
      src/cli/rap_edit_main.c

OBJ = $(SRC:.c=.o)
TARGET = bin/rap-edit

.PHONY: all clean install uninstall

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

install: $(TARGET)
	@mkdir -p $(DESTDIR)$(BINDIR)
	@mkdir -p $(DESTDIR)$(MANDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/rap-edit
	install -m 644 doc/rap-edit.1 $(DESTDIR)$(MANDIR)/rap-edit.1
	@echo "[✓] RAP-EDIT binaire installé dans $(DESTDIR)$(BINDIR)/rap-edit"
	@echo "[✓] Page de manuel installée dans $(DESTDIR)$(MANDIR)/rap-edit.1"

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/rap-edit
	rm -f $(DESTDIR)$(MANDIR)/rap-edit.1
	@echo "[✓] RAP-EDIT et sa page de manuel ont été désinstallés."
