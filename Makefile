# K-Term build
# Layout: src/*.c -> build/*.o -> bin/k-term ; tests/*.c -> bin/test_vt, bin/dbg
# Generated Wayland bindings live in build/generated/ (created by wayland-scanner).

CC      ?= cc
PKG     := wayland-client wayland-cursor wayland-egl egl gl xkbcommon freetype2 fontconfig harfbuzz
CFLAGS  ?= -O2 -std=gnu11 -Wall -Wextra
CFLAGS  += -D_GNU_SOURCE $(shell pkg-config --cflags $(PKG)) -Iinclude/k-term -Ibuild/generated
LDLIBS  += $(shell pkg-config --libs $(PKG)) -lm

PROTO   := /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
SRCS    := src/config.c src/vt.c src/pty.c src/font.c src/render.c src/main.c
OBJS    := $(patsubst src/%.c,build/%.o,$(SRCS))
GENH    := build/generated/xdg-shell-client-protocol.h
GENC    := build/generated/xdg-shell-protocol.c
HDRS    := include/k-term/k-term.h $(GENH)

BIN     := bin/k-term

all: $(BIN)

# order-only prerequisites: create output dirs without forcing rebuilds
bin build build/generated:
	mkdir -p $@

$(GENH): $(PROTO) | build/generated
	wayland-scanner client-header $< $@

$(GENC): $(PROTO) | build/generated
	wayland-scanner private-code $< $@

build/xdg-shell-protocol.o: $(GENC) $(GENH) | build
	$(CC) $(CFLAGS) -c -o $@ $(GENC)

build/%.o: src/%.c $(HDRS) | build
	$(CC) $(CFLAGS) -c -o $@ $<

$(BIN): $(OBJS) build/xdg-shell-protocol.o | bin
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

bin/test_vt: tests/test_vt.c build/vt.o build/config.o | bin
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

bin/dbg: tests/dbg.c build/vt.o build/config.o | bin
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test: bin/test_vt bin/dbg
	./bin/test_vt
	./bin/dbg

# address-sanitizer builds of the VT unit tests (make asan)
ASANFLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -g
# These rules compile the sources directly instead of linking build/*.o, so EVERY source and
# header they consume must be a prerequisite. bin/dbg_asan used to list only tests/dbg.c, which
# left a stale binary on disk: an ASan leak kept being reported from an old src/vt.c that make
# never rebuilt.
bin/test_vt_asan: tests/test_vt.c src/vt.c src/config.c $(HDRS) | bin
	$(CC) $(CFLAGS) $(ASANFLAGS) -o $@ tests/test_vt.c src/vt.c src/config.c $(LDLIBS)

bin/dbg_asan: tests/dbg.c src/vt.c src/config.c $(HDRS) | bin
	$(CC) $(CFLAGS) $(ASANFLAGS) -o $@ tests/dbg.c src/vt.c src/config.c $(LDLIBS)

asan: bin/test_vt_asan bin/dbg_asan
	./bin/test_vt_asan
	./bin/dbg_asan

run: $(BIN)
	./$(BIN)

clean:
	rm -rf bin build

.PHONY: all test asan run clean
