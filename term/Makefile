CC ?= cc
PKG_CONFIG ?= pkg-config
VTE_PC := vte-2.91-gtk4
DEPS := $(CURDIR)/.deps/usr
PREFIX ?= $(HOME)/.local

CFLAGS ?= -O2 -g
CFLAGS += -Wall -Wextra -std=c11

# Usa o VTE do sistema quando disponivel; senao cai para o pacote local
# extraido em .deps (apenas para desenvolvimento).
ifeq ($(shell $(PKG_CONFIG) --exists $(VTE_PC) && echo yes),yes)
  VTE_CFLAGS := $(shell $(PKG_CONFIG) --cflags $(VTE_PC))
  VTE_LIBS := $(shell $(PKG_CONFIG) --libs $(VTE_PC))
  VTE_RPATH :=
else
  VTE_CFLAGS := $(shell PKG_CONFIG_PATH=$(DEPS)/lib/pkgconfig $(PKG_CONFIG) --cflags $(VTE_PC))
  VTE_LIBS := $(shell PKG_CONFIG_PATH=$(DEPS)/lib/pkgconfig $(PKG_CONFIG) --libs $(VTE_PC))
  VTE_RPATH := -Wl,-rpath,$(DEPS)/lib
endif

CFLAGS += $(VTE_CFLAGS)
LDLIBS += $(VTE_LIBS) $(VTE_RPATH)

all: void-term

void-term: src/main.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)

run: void-term
	./void-term

install: void-term
	install -Dm755 void-term $(DESTDIR)$(PREFIX)/bin/void-term
	install -Dm644 void-term.desktop \
		$(DESTDIR)$(PREFIX)/share/applications/void-term.desktop

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/void-term
	rm -f $(DESTDIR)$(PREFIX)/share/applications/void-term.desktop

clean:
	rm -f void-term

.PHONY: all run install uninstall clean
