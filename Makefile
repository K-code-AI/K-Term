# Kai-Term build
# Layout: src/*.c -> build/*.o -> bin/kai-term ; tests/*.c -> bin/test_vt, bin/dbg
# Generated Wayland bindings live in build/generated/ (created by wayland-scanner).

CC      ?= cc
# libpng + zlib are for the image protocol (src/vt.c decodes the PNG payloads applications send);
# libjpeg + libwebp are added for the file manager's image previews (src/img.c decodes PNG, JPEG, WebP
# and — in-tree, no library — GIF). See README "Requirements".
PKG     := wayland-client wayland-cursor wayland-egl egl gl xkbcommon freetype2 fontconfig harfbuzz libpng zlib libjpeg libwebp
# The whole tree (src/ and tests/) is warning-free under this set. -Wcast-qual is deliberately NOT
# enabled: the wayland-scanner-generated xdg-shell bindings in build/generated/ trigger it, and that
# file is not ours to fix.
WARN    := -Wall -Wextra -Wshadow -Wwrite-strings -Wformat=2 -Wdiscarded-qualifiers
CFLAGS  ?= -O2 -std=gnu11 $(WARN)
CFLAGS  += -D_GNU_SOURCE $(shell pkg-config --cflags $(PKG)) -Iinclude/kai-term -Ibuild/generated
LDLIBS  += $(shell pkg-config --libs $(PKG)) -lm

PROTO   := /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
SRCS    := src/config.c src/vt.c src/img.c src/mux.c src/layout.c src/fm.c src/pty.c src/font.c src/render.c src/main.c
OBJS    := $(patsubst src/%.c,build/%.o,$(SRCS))
GENH    := build/generated/xdg-shell-client-protocol.h
GENC    := build/generated/xdg-shell-protocol.c
HDRS    := include/kai-term/kai-term.h $(GENH)

BIN     := bin/kai-term

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

bin/test_vt: tests/test_vt.c build/vt.o build/img.o build/mux.o build/layout.o build/config.o | bin
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

bin/dbg: tests/dbg.c build/vt.o build/img.o build/mux.o build/layout.o build/config.o | bin
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# The file manager model is testable on its own: fm.c needs only the image decoders (img.c) and
# klog/cp_is_wide (vt.c), so this binary pulls in exactly those three.
bin/test_fm: tests/test_fm.c build/fm.o build/vt.o build/img.o | bin
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test: bin/test_vt bin/dbg bin/test_fm test-cli
	./bin/test_vt
	./bin/dbg
	./bin/test_fm

# The option parser lives in main() in src/main.c, which is not linked into bin/test_vt, so the
# command-line surface can only be checked by running the real binary. tests/cli_test.sh does exactly
# that (it skips the window-opening checks when there is no Wayland session).
test-cli: $(BIN)
	sh tests/cli_test.sh ./$(BIN)

# address-sanitizer builds of the VT unit tests (make asan)
ASANFLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -g
# These rules compile the sources directly instead of linking build/*.o, so EVERY source and
# header they consume must be a prerequisite. bin/dbg_asan used to list only tests/dbg.c, which
# left a stale binary on disk: an ASan leak kept being reported from an old src/vt.c that make
# never rebuilt.
bin/test_vt_asan: tests/test_vt.c src/vt.c src/img.c src/mux.c src/layout.c src/config.c $(HDRS) | bin
	$(CC) $(CFLAGS) $(ASANFLAGS) -o $@ tests/test_vt.c src/vt.c src/img.c src/mux.c src/layout.c src/config.c $(LDLIBS)

bin/dbg_asan: tests/dbg.c src/vt.c src/img.c src/mux.c src/layout.c src/config.c $(HDRS) | bin
	$(CC) $(CFLAGS) $(ASANFLAGS) -o $@ tests/dbg.c src/vt.c src/img.c src/mux.c src/layout.c src/config.c $(LDLIBS)

bin/test_fm_asan: tests/test_fm.c src/fm.c src/vt.c src/img.c $(HDRS) | bin
	$(CC) $(CFLAGS) $(ASANFLAGS) -o $@ tests/test_fm.c src/fm.c src/vt.c src/img.c $(LDLIBS)

asan: bin/test_vt_asan bin/dbg_asan bin/test_fm_asan
	./bin/test_vt_asan
	./bin/dbg_asan
	./bin/test_fm_asan

run: $(BIN)
	./$(BIN)

clean:
	rm -rf bin build

.PHONY: all test test-cli asan run clean
