# Kai-Term — Agent Journal

Read this + ISSUES.md before touching project. User-facing docs: README.md. Current problems: ISSUES.md
"Open" (empty). Root causes of long-fixed bugs: git history, not this repo —
`git show d5afe4a:ISSUES.md`. Host/tool facts: `/home/kaida/Project/ENVIRONMENT.md` (outside repo on purpose
— not project documentation).

> Renamed 2026-10-16 (session 59): `K-Term`/`k-term` -> `Kai-Term`/`kai-term` (dir, header
> `include/kai-term/kai-term.h`, config `~/.config/kai-term/kai-term.conf`, app id `Kai-Term`). Old journal
> entries and git history keep old spelling — they record commands really run and log lines really
> printed. Pre-refactor journal: `git show 1ab9dc8:AGENT.md`.

## Project Overview

Wayland terminal emulator in C. xdg-shell window + PTY shell + VT/ANSI parser + EGL/OpenGL text
rendering, 2 glyph atlases (alpha8 mono, RGBA8 color emoji) plus one GL texture per application picture
(kitty graphics protocol), two vertex batches per frame with the pictures drawn between them. In-window
multiplexing: N sessions per window, tab bar, side-by-side panes. No GUI framework.

## Current Status (re-verified 2026-10-18, session 74 — the file manager is BUILT, WIRED and GREEN; it has never been seen by a human and README/ISSUES.md do not mention it)

- `make clean && make` exit 0, **0 warnings** under `WARN` (`-Wall -Wextra -Wshadow -Wwrite-strings
  -Wformat=2 -Wdiscarded-qualifiers`). `make test` -> exit 0: `CLI: 92 checks, 0 failures, 0 skipped`,
  `./bin/test_vt` **613 checks, 0 failures**, `./bin/dbg` **MODEL OK (0 problems)**, `./bin/test_fm`
  **220 checks, 0 failures**. `make asan` -> exit 0, 613/0 + MODEL OK + 220/0, no sanitizer report.
- Uncommitted work in the tree: `src/img.c` (new), `src/fm.c` (new), `tests/test_fm.c` (new), plus modified
  `Makefile`, `include/kai-term/kai-term.h`, `src/vt.c`, `src/layout.c`, `src/config.c`, `src/render.c`,
  `src/main.c`, `tests/test_vt.c`, `tests/cli_test.sh`, `AGENT.md`. The file manager IS reachable from the
  UI (`Ctrl+Shift+F`) and its band is drawn — but no human has looked at it, and the interactive routing
  (FM keys, focus handover, mouse) has never been exercised outside the model tests.
- **Image support (kitty graphics protocol) is in the tree and user-confirmed by eye** ("The image display
  properly. The feature works as expected", 2026-10-17, session 70, `timg -p k` with a PNG): APC/DCS parser
  states, chunk assembly, base64 + zlib + PNG (libpng simplified API) + raw f=24/32 decode, premultiplied
  pixels held in the model, images anchored to absolute grid rows, winsize pixel geometry +
  `CSI 14 t`/`CSI 16 t`, and a renderer that draws each picture between the background batch and the text
  batch. Also verified headless (real timg bytes through the real parser) and by log line
  (`graphics: 1 image(s) drawn, 1 texture(s) held`, no GL warning).
- No probe/experiment left in tree: `grep -rnE "TEMPORARY|PROBE|EXPERIMENT|NEGATIVE" src/ include/ tests/` ->
  empty.
- README flag table (`-e -w -c -d -v -h`) + 27 settings rows match `src/main.c` / `src/config.c` — but README
  does NOT yet mention the file manager or the libjpeg/libwebp dependencies `src/img.c` now links, and
  `Makefile`'s `PKG` already lists them. Docs lag the tree while the feature is unfinished.
- **No open issues** in the shipped sense (the terminal itself), but `make test` is red on the new fm tests,
  which is tracked in the s72 entry below, not filed as a user-facing bug. The 4 reports that used to be
  there were unproven (never reproduced here) and were dropped from ISSUES.md entirely on 2026-10-17 — they
  are not in "Fixed" either, since no fix was ever verified. Recoverable with `git show d5afe4a:ISSUES.md`;
  recurrence -> file a new Open item with a reproduction.
- **User-confirmed by eye in their window:** multiplexing + tab bar + side-by-side ("works as expected and
  looks great", 2026-10-17); image display via `timg -p k` ("the image display properly. The feature works
  as expected", 2026-10-17); zoom (no stale prompt lines), padding, fullscreen refresh, black-window/no-input
  fix, heavy-output segfault fix, transparency, bracketed paste, `clear` drops scrollback, shell start dir,
  color emoji, text selection, `app_id`, several instances at once, window resize, key-hold repeat, clipboard
  interop both directions.
- Verified at model+live level (not all re-checked by hand): reflow on width change, color emoji + grapheme
  clusters, whole-glyph selection with no band over unwritten space, compositor transparency, CSI 3 J +
  startup dir, client-side key repeat, draw/commit + event-loop pacing.
- Git: `origin = https://github.com/K-code-AI/Kai-Term`, branch `main`. As of s70 close **everything on main
  is pushed** (`main` == `origin/main`, 0 behind / 0 ahead, empty `git diff`). Do not write a hash or an
  ahead-count here — the commit recording it makes it wrong. Read the truth with `git status -sb` and
  `git log --oneline origin/main -1`. `git fetch` before push — user commits on GitHub too. Never commit
  `bin/` or `build/`.

## Current Task (s71 — NEW FEATURE: integrated file manager with live previews)

**s71 objective (from `/home/kaida/Project/TODO.md`, user request 2026-10-18):** an interactive file
manager inside the terminal window — left pane browser with a highlighted selection, right pane
context-sensitive live preview (directory contents, image, text with syntax highlighting, metadata for
anything else), bottom pane the ordinary terminal whose shell follows the directory the file manager
navigates into. Arrow keys (configurable) and mouse navigation, `Ctrl+Shift+Up/Down` to move the keyboard
between the file-manager panes and the terminal pane, Enter opens, Backspace goes to the parent, plus
shortcuts for open / refresh / toggle preview / exit. No new dependencies beyond what the formats need,
no blocking UI on directory or preview loads, resource caps for big files/images, safe on permission
errors, missing files, symlinks and unsupported formats, and closing it must leave the terminal exactly
as it was.

**s71 measured start (before any edit):** `git status -sb` -> `main` == `origin/main`, no modified files.
Tree verified in s70: `make` 0 warnings, `make test` 553 checks 0 failures + MODEL OK + CLI 86/0, `make asan`
553/0 + MODEL OK. Deps present here (pkg-config, measured 2026-10-18): libpng 1.6.58, zlib 1.3.2,
**libjpeg 3.1.4.1 (libjpeg-turbo), libwebp 1.6.0, giflib MISSING** — so GIF must be decoded in-tree and
JPEG/WebP can use the system libraries. PIL 12.3.0 is available in `.scratch/` harnesses to GENERATE test
images (never to render the UI).

**s71 design (decided before writing code, follows the project's existing split):**
1. `src/img.c` (new) — image decoding in one place: PNG (libpng simplified API, moved out of vt.c),
   JPEG (libjpeg), WebP (libwebp), GIF (own LZW decoder, because giflib is absent here). Output is always
   premultiplied RGBA8, the one format the renderer already knows. vt.c's `decode_png`/`premultiply`
   become calls into it so the kitty path and the preview path cannot drift.
2. `src/fm.c` (new) — the file-manager model, no Wayland/EGL: directory scan (opendir/lstat, sort, hidden
   filter, caps), navigation state (dir, selection, top row), preview building (text + syntax highlight,
   directory listing, metadata, image), and painting the two panes into `Cell` grids. The Cell grids are
   what makes it look native: the existing renderer draws them with the existing font, colours and passes.
   The synchronous core is unit-testable; the non-blocking part is a single worker thread that fills a
   result slot and wakes the main loop through a pipe fd added to epoll.
3. `src/layout.c` — the geometry stays here (project rule): with the file manager on, the grid area splits
   into an upper band (browser | preview) and the terminal pane below it. The terminal session keeps living
   in `Layout.pane[]` so every existing hit test, focus border and `W.gx/W.gy` mapping keeps working; the
   two file-manager rects go in a new `FmLayout` inside `Layout`.
4. `src/render.c` — draw the file-manager cell grids in the SAME two batches (backgrounds, then glyphs) and
   draw the preview picture between them with the existing per-image texture path (`img_tex`), clipped to the
   preview rect.
5. `src/main.c` — input routing (FM keys only while the file manager has the keyboard, so the terminal is
   untouched otherwise), mouse click/wheel over the FM panes, focus switching, the `cd <path>` the shell gets
   when the file manager navigates, and the fm result fd in the epoll loop.
6. `src/config.c` — new settings + bindings. FM-internal keys are plain keys (`Up`, `r`, `Escape`), which the
   existing `keybind_parse` refuses on purpose, so a second entry point that allows an unmodified binding is
   needed; the tab bindings keep the refusal.
7. `tests/test_fm.c` (new, wired into `make test` + `make asan`) — dir scan, navigation, preview kinds,
   image decode for all four formats, caps and error paths, and the FM layout rects.

**s71 planned steps (each one: edit -> AGENT.md -> build/test):**
1. AGENT.md plan (this entry).
2. `src/img.c` + Makefile + vt.c switch to it; build, `make test`, `make asan` must stay clean.
3. `src/fm.c` core (scan/nav/preview/cells) + header types; `tests/test_fm.c` against it.
4. layout: FM rects + hit test + tests.
5. render: FM panes + preview image.
6. main: input, focus, mouse, cd-follow, worker thread + epoll fd.
7. config keys + README + ISSUES.md, then a live run for the user to confirm by eye.

**s71 expected outcome:** a file manager that reads as part of the terminal (same cells, font, colours,
borders), a warning-free build, a test count that rises with real coverage of the model/layout/decoders,
no open issues, and the visual verdict from the user (this project's rule: anything visual is confirmed
by their eye, not by a screenshot harness).

### s71 entry 1 — step 2 done: `src/img.c` exists, vt.c uses it, decoders measured

Created `src/img.c` (PNG via libpng, JPEG via libjpeg, WebP via libwebp, GIF decoded in-tree) and moved
`decode_png` + `premultiply` out of `src/vt.c` into it, so the kitty path and the coming preview path share
one decoder. `IMG_DIM_MAX`/`IMG_PIX_MAX` moved to `kai-term.h` (the old `IMAGE_DIM_MAX`/`IMAGE_PIX_MAX`
defines in vt.c are gone) because both paths must refuse the same picture. Makefile: `src/img.c` in `SRCS`,
`libjpeg libwebp` in `PKG`, and `build/img.o` / `src/img.c` added to the `test_vt`, `dbg` and both `_asan`
prerequisite lists (the incomplete-prerequisite trap is recorded in Key Decisions — an asan target that
does not list a source runs a stale binary).

**Measured (exact commands, this session):**
- `make clean && make` -> exit 0, 0 warnings.
- `./bin/test_vt` -> **553 checks, 0 failures** (unchanged: the graphics tests still pass through the moved
  PNG decoder); `make asan` -> **553 checks, 0 failures** + `MODEL OK (0 problems)`, no sanitizer report.
- Decoder evidence, `.scratch/s71/`: PIL 12.3.0 generated `t.png t.jpg t.webp t.gif tr.gif`, ffmpeg generated
  `ni.gif ni2.gif`; `ppm_probe.c` (links the real `src/img.c`) wrote a PPM per file and numpy compared every
  pixel against ffmpeg's own decode: **PNG, JPEG, WebP, interlaced GIF and non-interlaced GIF all maxdiff 0
  (byte-exact)**. `tr.gif` (transparent index 1) matches PIL's alpha pattern exactly — alpha 0 exactly where
  PIL says transparent.
- **Two real bugs found by that probe, both fixed:** (1) `img_decode_jpeg` used `ci.output_scanline` as the
  row index AFTER `jpeg_read_scanlines()`, which bumps it — every row landed one line low and the last row
  wrote past the end of the buffer (`free(): invalid next size`, i.e. heap corruption). The index is now
  captured before the read. (2) `gif_interlaced_row()` had the wrong pass starts/steps (0/8, 8/8, 4/4, 2/2);
  GIF's four passes are 0/8, 4/8, 2/4, 1/2 — with the wrong mapping 22 of 23 rows differed from ffmpeg.
  A Python LZW decoder written independently agreed with the C index stream, which is what proved the
  indices were right and the ROW MAPPING was wrong — PIL's own `P`-mode array is palette-remapped and is
  NOT a valid index reference (it matched only on row 0, which is how it misled at first).
- Malformed input, ASan+UBSan build of the probe: truncated GIF, `GIF89a` + garbage, a non-image file, a GIF
  header claiming 8000x8000, and a corrupt LZW stream all fail cleanly (log line, NULL return), no sanitizer
  report, no crash. The garbage header is caught by the size cap (`gif 65535x65535 px is over the limit`).

**Next action (step 3):** `src/fm.c` — the file-manager model (directory scan, navigation, preview kinds,
cell painting) plus `tests/test_fm.c`. Nothing in main.c/layout.c/render.c has been touched yet, so the
window still looks exactly as it did in s70.

### s71 entry 2 — step 3a: the file-manager types are in the header (fm.c itself is not written yet)

Added the `---------- file manager (src/fm.c) ----------` section to `include/kai-term/kai-term.h`, between
the layout section and the font section: caps (`FM_PATH_MAX` 4096, `FM_NAME_MAX` 256, `FM_ENTRIES_MAX`
8192, `FM_PREVIEW_BYTES` 256 KiB, `FM_PREVIEW_DIR_MAX` 512, `FM_PREVIEW_FILE_MAX` 16 MiB), the kind enums
(`FMK_*` what an entry is, `FMP_*` what the preview shows, `FM_FOCUS_*` which region the keyboard drives,
`FM_LANG_*` the text-preview languages), and the types `FmEntry`, `FmScan`, `FmPreview`, `FmPane`,
`FileMgr`, `FmHl`, plus every fm_* prototype step 3 will implement.

Decisions the types encode, each because the alternative was measured or reasoned out:
- **`FmEntry` stores the NAME, not the path.** 8192 entries x a 4096-byte path is 32 MB for one listing;
  `fm_entry_path()` rebuilds the full path into a caller's buffer in one snprintf.
- **A listing keeps hidden entries and marks them (`FmEntry.hidden`), and `FileMgr.vis[]` is the view
  after the filter.** Toggling dotfiles then costs a rebuild of an int array, not a second scan of the
  disk, and selection is a position in `vis[]` so it survives the toggle.
- **`FmScan`/`FmPreview` carry a `seq` request id and `fm_install_*` drops a stale answer.** Without it a
  slow scan of a directory the user already left overwrites the listing on screen.
- **`fm_scan_build_n(dir, seq, max_entries)` is the capped form; `fm_scan_build` is it with
  `FM_ENTRIES_MAX`.** So a test can pin truncation without creating 8192 files.
- **The panes are `FmPane` = a flat `Cell` array + cols/rows**, walked like a session grid, so the renderer
  needs no new drawing path — "a grid with no cursor and no selection".
- **`fm_image_fit()` returns the picture box in CELLS and lives in fm.c**, because the box is what a test
  can check; the renderer only draws the quad it is given.

Two mistakes the compiler caught while the header was still being written, both fixed in place:
`FmPreview` declared `truncated` twice (once for the directory case, once for "every kind") -> one field
whose comment names all three things it can mean; and `mode_t` was used without `sys/types.h` being
included — it compiles today only because glibc's `string.h` pulls it in, so the include is now explicit
(`.scratch/s71/modechk.c` is the check: a file that includes ONLY kai-term.h and names a `mode_t`).

**Measured:** `make` exit 0, 0 warnings (header change rebuilds the whole tree); `make test` -> 553 checks
0 failures + `MODEL OK (0 problems)` + CLI 86/0/0. No behaviour change is possible yet: no fm.c exists,
so no code calls any of this.

**Next action:** write `src/fm.c` (scan, navigation, preview builders, highlighter, cell painting), then
`tests/test_fm.c` + Makefile wiring.

### s71 entry 3 — step 3b: `src/fm.c` exists (1560 lines, compiles 0 warnings); not yet built into any binary, no tests yet

Created `src/fm.c`: the whole file-manager model, no Wayland/EGL/threads. Sections: colours (accents moved
to the header so a test can pin them; base ink/paper come from Config), formatters (`fm_human_size`,
`fm_human_time`, `fm_mode_string`), paths (`norm_path` collapses `//`, `/.`, `/a/../`; `parent_path`;
`join_path`), listing (`fm_scan_build_n` = opendir + lstat + qsort dirs-first/case-insensitive, entry cap +
`truncated`, per-entry `lstat` failure -> `FMK_MISSING` instead of dropping the name), the `FileMgr` state
(`vis[]` view after the hidden filter, `ensure_visible` scroll, `fm_go`/`fm_parent`/`fm_go_sel`/`fm_refresh`,
select/move/page/home/end), previews (`fm_preview_build`: lstat -> dir scan / image sniff+decode / text
with NUL-in-first-512-bytes = binary / non-regular file = metadata; `meta_add` builds the label-value
lines with owner/group names and the mode string), the text highlighter (14 languages, keyword/type/string/
number/comment/preproc colours, state carried across lines for C block comments and markdown fences), the
cell painters (`pane_puts` decodes UTF-8 strictly - overlongs, surrogates and out-of-range become U+FFFD
consuming one byte - and gives a `cp_is_wide()` codepoint the same `ATTR_WIDE` continuation cell vt.c
uses, so the renderer's wide rules apply to the file manager unchanged), and `fm_image_fit` (aspect
preserved in PIXELS, never upscaled, centred, returned in cells).

Request-id design, corrected while writing: one monotonic issuer counter (`FileMgr.seq`) plus
per-kind newest ids (`scan_req`, `pv_req`). The first draft bumped a single `seq` from both
`fm_go()` and `fm_preview_request()`, which made every preview request invalidate an in-flight
listing answer (`fm_install_scan` compares `s->seq == fm->scan_req`). Ids are shared so they can never
collide; the newest-of-each-kind ids are what the install functions compare against.

Five defects found by the compiler and by re-reading my own code before the first test run:
- `FmPreview` had `truncated` twice -> one field, comment names all three meanings.
- `mode_t` used with no `sys/types.h` include (worked only because glibc's `string.h` pulls it in).
- `lang_tab` initialised from `const char *kw_c[]` -> `error: initializer element is not constant`;
  the keyword lists are now `static const char kw_C[]` ARRAYS, because only an array name is an
  address constant in a static initializer.
- A `/* ... /* ... */` nested comment inside the `Lang` struct comment silently ended early.
- `fm_human_size` printed 12345 bytes as `1.2 KiB`: the old code divided by 1024 and then treated the
  result as if it were in tenths. Now the unit loop uses a scratch copy and the value is split into
  `whole = n / base` and a rounded `frac = ((n % base) * 10 + base/2) / base` -> `12.1 KiB`.
- `fm_mode_string` had an unreachable second ternary branch for the permission bits.
- `fm_set_size` compared `fm->list_cols != lc` AFTER assigning it, so a resize never reallocated; now
  the sizes are assigned first and the realloc decision is explicit (and the old grid is freed if the
  grow fails, so an OOM resize cannot leak).
- `paint_preview_text` bounded a line by `sizeof hl` (bytes) instead of the element count.

**Measured:** `cc ... -c src/fm.c` -> exit 0, **0 warnings** under the project `WARN` set (four
`-Wformat-truncation` warnings were fixed by giving the path `%s` an explicit precision in the error
lines rather than by growing the buffers - a path longer than the error line is cut, which is the
honest behaviour). `make` exit 0, 0 warnings; `make test` -> 553 checks 0 failures + MODEL OK + CLI
86/0/0 (fm.c is not in any binary yet, so the counts cannot have moved - they are re-run to prove it).

**Next action:** `tests/test_fm.c` (real temp directory tree: dirs, dotfile, dangling symlink, FIFO,
unreadable dir, 4 image formats, binary file, truncation, stale-answer drop, navigation, painting,
`fm_image_fit`) + Makefile wiring into `test` and `asan`.

### s72 entry 1 — session start: measured the real state, `make test` is RED on 2 fm checks

User request: "Continue the work on Kai-Term latest feature" -> the integrated file manager (TODO.md),
picking up at s71's step 3/4 boundary. Read AGENT.md + ISSUES.md first, then measured instead of trusting
the journal:

- `git status -sb` -> `main` == `origin/main`; modified: `AGENT.md Makefile include/kai-term/kai-term.h
  src/vt.c`; untracked: `src/fm.c src/img.c tests/test_fm.c`. So s71's steps 1-3b are in the tree,
  uncommitted, exactly as its entries say.
- `make clean && make` -> exit 0, **0 warnings** (the link line now carries `-ljpeg -lwebp` too).
- `make test` -> `test_vt` 553/0, `dbg` MODEL OK, then **`./bin/test_fm`: 188 checks, 2 failures** —
  `tests/test_fm.c:732` "paint: syntax highlighting reaches the cells (int/return)" and `:733` "and so does
  the string literal". `make test` stops there (exit 2), so `test-cli` never ran this session.

**Root cause of both failures, measured — the code is right, the test's geometry is wrong.**
`.scratch/s72/hl.c` calls `fm_hl_line()` directly on the two lines of the test tree's `code.c`: the
highlighter colours line 1 fully as a preprocessor line, and line 2 `int main(void) { /* greet */
printf("hi"); return 0; }` as TYPE(`int`,`void`) CMT(`/* greet */`) STR(`"hi"`, 4 cells) KEY(`return`,
6 cells) NUM(`0`). So highlighting works. `.scratch/s72/paint.c` then runs the model's real
request -> build -> install -> paint path and dumps the preview pane as colour letters: the pane is 40
columns wide, `paint_preview_text` starts text at x=2 and **clips a line at the pane width instead of
wrapping it** (correct behaviour for a preview pane), and `"hi"` starts at line column 33 -> x=35, so only
2 of its 4 cells fit, while `return` at line column 43 -> x=45 is off-pane entirely. Hence KEY=0, STR=2.

**Decision: fix the test, not the code.** The check's purpose is "highlighting reaches the cells"; making it
pass by asserting fewer cells would delete the claim. Give the text-preview check a preview pane wide
enough for the line, and pin the clipping behaviour with its own check so the next reader knows the cut is
deliberate.

**s72 objective:** finish the file manager — green tests, then s71's steps 4-7 (layout rects, render, main
input/focus/mouse/cd-follow/worker thread, config keys, README + ISSUES.md).

**s72 planned steps:** (1) fix the two paint checks + add the clipping check; re-run `make test` and
`make asan` and record counts. (2) `src/layout.c`: the FM band split + hit tests, `tests/test_fm.c` or
`test_vt.c` checks on the rects. (3) `src/render.c`: draw the two FM cell grids in the existing batches +
the preview picture clipped to the preview rect. (4) `src/main.c`: FM key routing, focus switching, mouse,
`cd` follow, worker thread + result fd in epoll. (5) `src/config.c` settings/bindings + README + ISSUES.md,
then a live run for the user to confirm by eye.

**s72 expected outcome:** `make test` green with the fm checks included, `make asan` green including
`bin/test_fm_asan`, and a file manager that reads as part of the terminal (same cells, font, colours,
borders) — visual verdict from the user, per this project's rule.

### s72 entry 2 — step 1 done: `make test` and `make asan` are GREEN (fm tests included); 4 test bugs fixed

The two red checks turned out to be **four separate defects in `tests/test_fm.c`**, none in `src/fm.c`:

1. **The test tree's `code.c` was written truncated.** `wfile("code.c", <literal>, 63)` — the literal is 74
   bytes, so the file on disk ended mid-word: `... printf("hi"); ` and the `return` the check looked for
   was never in the file. Fixed by a `wstr()` helper that writes `strlen(s)`; no hand-typed byte count is
   left for a text file.
2. **Two of those hand-typed counts were LARGER than their literal** — `notes.md` (14-byte literal, asked
   for 16) and `Makefile` (23, asked for 24) — i.e. `fwrite` read past the end of the string literal. UB,
   silently writing whatever rodata followed. Same fix. `binary.bin` (15-byte literal, 16 asked) only
   wrote the literal's own NUL terminator; it is now an explicit `unsigned char elf[]` with `sizeof`.
3. **The highlight check ran in a 40-column preview pane** while `code.c`'s second line is 54 columns.
   `paint_preview_text()` starts text at x=2 and CUTS at the pane edge, so `"hi"` (line column 33) lost 2
   of its 4 cells and `return` (line column 43) was off-pane. Measured with `.scratch/s72/paint2.c`, which
   runs the real request -> build -> install -> paint path in an 80-column pane and prints the pane as
   colour letters: `..TTTbbbbbbTTTTbbbbCCCCCCCCCCCbbbbbbbbSSSSbbbKKKKKKbNbbb` = TYPE `int`/`void`, CMT
   `/* greet */`, STR 4, KEY 6, NUM `0`. The check now uses an 80-column pane and pins the exact counts
   (6 and 4) instead of `>= 3`.
4. **The preview request was never re-issued in that check** — the selection had not moved, so
   `fm_preview_request()` returned 0 and the build reused the previous id (it happened to still be the
   newest, so the install passed for the wrong reason). The check now MOVES the selection to `code.c`
   (`find_pos` + `fm_select`) and asserts the request returns 1 — the path `main.c` will use.

New checks added while fixing them (188 -> 193): the tree has `code.c`; moving the selection asks for a new
preview; its answer installs; **a text line longer than the pane fills it to the right edge and does NOT
wrap onto the next row** (that cut is deliberate — wrapping would push the metadata and the rest of the file
off the pane). One existing check was corrected honestly: "a pane too short for its chrome still shows the
entries" compared row 0 against the SELECTED entry, which only held while the selection was also the top of
the view; it now compares against `vis_at(fm, fm.top)` (new test helper) because the painter starts at
`top`, and the earlier move of the selection exposed that assumption.

**Measured (exact commands, this session):**
- `make clean && make` -> exit 0, 0 warnings.
- `make test` -> exit 0: `CLI: 86 checks, 0 failures, 0 skipped`, `test_vt` **553 checks 0 failures**,
  `dbg` **MODEL OK (0 problems)**, `test_fm` **193 checks 0 failures**.
- `make asan` -> exit 0: `test_vt_asan` 553/0, `dbg_asan` MODEL OK, `test_fm_asan` **193/0**, and
  `grep -cE "AddressSanitizer|runtime error|LeakSanitizer|SUMMARY|ERROR"` over the whole run -> **0**.

**Next action (step 2):** `src/layout.c` — with the file manager on, split the grid area into the upper
band (browser | preview) plus the terminal pane below, keep the terminal session in `Layout.pane[]` so every
existing hit test / focus border / `W.gx,W.gy` mapping keeps working, and put the two file-manager rects in a
new `FmLayout` inside `Layout`. Then tests for the rects.

### s72 entry 3 — step 2 done: the file-manager band is in `src/layout.c` (geometry only, nothing visible yet)

`include/kai-term/kai-term.h` (layout section): new `FmGeom` (band `on`, list rect + `list_cols/rows`,
`prev_on` + preview rect + `prev_cols/rows`), `FM_HIT_NONE/LIST/PREVIEW`, `FM_ROWS_MIN` 3 / `FM_ROWS_MAX` 64 /
`FM_TERM_MIN_ROWS` 3, `FmGeom fm` inside `Layout`, and `fm_on` / `fm_preview_on` / `fm_rows` inside
`LayoutReq`. `layout_initial_size()` gained `fm_on, fm_rows` before the two out-params.
`src/layout.c`: `layout_compute()` now splits the grid area HORIZONTALLY first — a band of `fm_rows` cell
rows at the top (browser left half, preview right half, one `gap` between, remainder on the right like every
other whole-cell remainder), then the terminal pane(s) get `pane_y = grid_y + band_h + gap` and
`pane_h = grid_h - band_h - gap`. The sessions stay ordinary `pane[]` entries, only shorter, which is what
keeps `layout_hit_pane`, `layout_pane_of_tab`, `layout_grid_size`, the focus border and `W.gx/W.gy` working
unchanged with the band on AND off. New `layout_hit_fm()` (must be checked BEFORE `layout_hit_pane()`).
The band is clamped so the shell keeps `FM_TERM_MIN_ROWS` rows, and dropped entirely when even that leaves
less than `FM_ROWS_MIN` rows — the tab bar's "the terminal wins" rule, applied again. Bounds are clamped in
layout.c, not config.c, so no caller can ask for a 1-row band.

**Measured:** `make` exit 0, 0 warnings. `make test` -> CLI 86/0/0, `test_vt` **576 checks 0 failures**
(553 -> 576: 23 new fm-layout checks), `dbg` MODEL OK, `test_fm` 193/0. One check failed first and the TEST
was wrong: it probed `list_y + list_h + 1`, which is inside the 8 px gap between the band and the terminal
pane, and expected `layout_hit_pane` to say "pane" — it correctly says -1. Split into two checks: the gap
belongs to neither, and a point at `pane[0].y + 1` is the terminal and only the terminal.

What the new checks pin: band starts at the top margin and is `fm_rows` rows tall; the half-split plus the
gap fills the band exactly; each pane's cols x rows come from `term_grid_geometry()` on its own rect; the
terminal pane keeps the FULL width (the band only costs height) and every session is sized to the terminal
pane (115x45 in the 936x1002 example, not 115x58); hit tests for list / preview / the gap between them /
below the band / the top margin; preview off -> the browser owns the band; an oversized `fm_rows` shrinks so
the terminal keeps exactly `FM_TERM_MIN_ROWS`; a 120 px window drops the band and the terminal gets the whole
grid area (6 rows) back; and the startup round trip `layout_initial_size(..., fm_on=1, fm_rows=12, ...)` ->
`layout_compute()` returns the configured 80x30 for the terminal pane.

`src/main.c` passes `0, 0` for the two new `layout_initial_size()` arguments for now — the file manager is
off at startup and `main.c` has no fm state yet. That line is part of the main.c wiring step.

**Next action (step 3):** `src/config.c` — the settings (`file_manager`, `fm_rows`, `fm_preview`, arrow-key
bindings, the toggle/focus/quit/open/refresh/hidden bindings) and the second keybind entry point that allows
unmodified keys for FM-internal shortcuts, since `keybind_parse` deliberately refuses them for terminal keys.

### s72 entry 4 — step 3 done: the file manager's settings and bindings are in `src/config.c`

`include/kai-term/kai-term.h`: `FmKeys` struct (up, down, pageup, pagedown, home, end, open, parent,
refresh, preview, hidden, quit), `Config.fm_on/fm_rows/fm_preview/fm_show_hidden/fm_follow`,
`Config.key_file_manager/key_fm_focus/key_fm_release`, `Config.fm_key`, and the prototype
`keybind_parse_plain()`.
`src/config.c`: `keybind_parse` is now a wrapper over a new static `keybind_parse_into(spec, out,
allow_plain)` — one parser, two policies, so the FM keys cannot drift from the strict one's other rules
(unknown key name, two keys, no key). `keybind_parse_plain` allows `mods == 0`, which is safe ONLY because
those bindings are matched just while the file manager owns the keyboard. `set_fm_keybind()` mirrors
`set_keybind()` (empty/none/off/no unbinds; an unparseable value keeps the current binding).
Defaults: FM off, `fm_rows = 12`, preview on, dotfiles hidden, `fm_follow = 1` (navigating sends
`cd <path>` to the shell), `Ctrl+Shift+F` toggle, `Ctrl+Shift+Up` / `Ctrl+Shift+Down` focus switch, and
bare `Up Down Page_Up Page_Down Home End Return BackSpace r p . Escape` for the FM itself.
15 new config keys: `file_manager, fm_rows, fm_preview, fm_show_hidden, fm_follow, key_file_manager,
key_fm_focus, key_fm_release, fm_key_up, fm_key_down, fm_key_pageup, fm_key_pagedown, fm_key_home,
fm_key_end, fm_key_open, fm_key_parent, fm_key_refresh, fm_key_preview, fm_key_hidden, fm_key_quit`
(20 counting every fm_key_*).

**Measured:** `make` exit 0, 0 warnings. `make test` -> `test_vt` **613 checks 0 failures** (576 -> 613: 8
`keybind_parse_plain` checks + 29 file-manager config checks), `dbg` MODEL OK, `test_fm` 193/0.
`make test-cli` -> **CLI: 92 checks, 0 failures, 0 skipped** (86 -> 92: the FM keys are known, two `none`
bindings log their unbind, and a default run never mentions a file manager).

**Next action (step 4):** `src/render.c` — draw `Layout.fm.list` / `Layout.fm.prev` cell grids in the same
two batches the terminal uses, and the preview picture between them clipped to the preview rect, using
`fm_image_fit()` for the box. Then `src/main.c`: the `FileMgr` instance, the worker thread + result fd, key
routing, mouse, `cd` follow, and `fm_set_size()` from `layout_apply()`.

### s73 entry 1 — session start: measured the real state, the tree does NOT build (step 4 was half-applied)

User request: "Continue adding the new feature the Kai-Term" + "Don't stop until you have a question or are
done with the feature integration." -> continue the integrated file manager (TODO.md), picking up at s72's
step 4 (render.c).

**s73 measured start (before any edit this session):**
- `git status -sb` -> `main` == `origin/main`; modified: `AGENT.md Makefile include/kai-term/kai-term.h
  src/config.c src/layout.c src/main.c src/vt.c tests/cli_test.sh tests/test_vt.c`; untracked: `src/fm.c
  src/img.c tests/test_fm.c`. So s72's steps 1-4 (img.c, fm.c, test_fm.c, layout band, config keys) are in
  the tree, uncommitted, as its entries say.
- `make -j8` -> **FAILS, 2 errors**: `src/render.c:766: error: conflicting types for 'render_draw'` (the
  header at `include/kai-term/kai-term.h:1057` already declares the 5-argument form with
  `const FileMgr *fm`) and `src/main.c:409: error: too few arguments to function 'render_draw'`. So the
  header half of step 4 (render_draw prototype + `Render.fm_tex/fm_tex_uid/fm_tex_w/fm_tex_h/fm_px/
  fm_px_cap`) is already applied, but `src/render.c` has **zero** fm references (`grep -n fm src/render.c`
  -> empty) and `src/main.c` has no fm state. That is an interrupted step 4, not recorded by s72's entries —
  treat it as work in progress whose result is known: the build is red until render.c and main.c catch up.
- Nothing else is broken: `src/layout.c` and `src/config.c` diffs match s72 entries 3 and 4, `src/main.c`
  carries only the `layout_initial_size(..., 0, 0, ...)` placeholder line.

**s73 objective:** finish the file-manager integration — make the tree build again, draw the band, wire the
UI (keys, mouse, focus, `cd` follow, worker thread + result fd), keep `make test`/`make asan` green with
real coverage, and bring README/ISSUES.md in line with what actually exists.

**s73 planned steps (each one: edit -> AGENT.md -> build/test):**
1. `src/render.c`: accept `const FileMgr *fm`; emit the two FM cell grids through the existing three passes
   (backgrounds into batch A, glyphs into batch B) and the preview picture between the batches, clipped to
   `Layout.fm.prev`, sized by `fm_image_fit()`; one texture for the preview keyed on `FmPreview.uid`.
2. `src/main.c`: the `FileMgr` instance, `fm_set_size()` from `layout_apply()`, `LayoutReq.fm_*`, the
   toggle/focus/release bindings, FM key routing while the FM owns the keyboard, pointer click + wheel over
   the two panes, the `cd <path>` follow, and the worker thread + result fd in the epoll loop.
3. Tests for whatever is testable headlessly (the render/main halves are not in any test binary, so the
   honest coverage is `tests/cli_test.sh` for the new config surface + `test_fm`/`test_vt` for the model).
4. README (feature, new config keys, libjpeg/libwebp in Requirements) + ISSUES.md limitations.
5. `make clean && make`, `make test`, `make asan` from a clean tree; exact counts recorded; then a live run
   for the user to confirm by eye (this project's rule for anything visual).

**s73 expected outcome:** `make` 0 warnings, `make test` + `make asan` green including `bin/test_fm_asan`, a
file manager that reads as part of the terminal (same cells, font, colours, borders), docs that match the
tree, and the visual verdict from the user.

### s73 entry 2 — step 1 done: `src/render.c` draws the band (compiles 0 warnings; the link is still red until main.c catches up)

`render_draw` now takes the 5th argument the header already declared (`const FileMgr *fm`; NULL or
`fm->on == 0` draws no band). What was added:

- **`emit_cell_glyph()`** — the pass-3 glyph body (color-emoji cluster path + mono path + `ATTR_DIM`
  + `cluster_is_wide()` span) lifted out of `render_draw` into one function, called by the session panes
  AND by the two band panes. Reason: a file name can hold any codepoint, so the band needs the same
  emoji/wide rules; a second copy is how they would drift. The session call site keeps its own
  inverse/cursor/selection colour resolution and passes `fg/bg` in already resolved.
- **`emit_border()`** — the 4-quad focus frame, factored for the same reason; the side-by-side pane
  border now calls it (identical geometry, 2 px, drawn in the gap outside the rect).
- **Band pass 1 + pass 3** (`fm_pane_rect()` / `fm_pane_prepare()` / `FmPaneDraw`): the two `FmPane`
  grids go through the SAME two batches as a session grid — backgrounds into batch A, glyphs into
  batch B — with the same remainder rule (`pad_gl = vh - rect_y - nrows*ch`) and the same defensive
  clamp of cols/rows to what the rect can hold. Indexing uses the grid's own stride (`FmPane.stride =
  FmPane.cols`), not the clamped column count.
- **`fm_preview_tex()` + `draw_fm_preview()`** — one texture for the preview picture (at most one
  preview exists, so no cache), re-uploaded when `FmPreview.uid`/`w`/`h` change; `fm.c` hands out plain
  RGBA so the copy in `Render.fm_px` is premultiplied with `img_premultiply()` before upload. The quad
  is placed by `fm_image_fit()` — the CELL box positions it, the PX box is what is drawn, centred in it,
  so the ceil()-into-cells slack cannot stretch the aspect — clipped to `Layout.fm.prev`, composited
  against the preview pane's own cell background, drawn between the batches like any other picture
  (counted into the existing `graphics: N image(s) drawn` line).
- **Focus border**: with the band on, exactly one region gets the 2 px frame — the browser, the preview,
  or (when the file manager has the band but not the keyboard) the terminal pane. With the file manager
  off the rule is unchanged: side-by-side only.
- `render_destroy` deletes `Render.fm_tex` while the context is current and frees `Render.fm_px`.

One defect found by re-reading the edit before compiling: the band pass-3 background colour first came
out as a nonsense triple `col2f` (an unfinished inverse branch). Replaced with the same swap pass 1 does.

**Measured:** `make build/render.o` -> exit 0, **0 warnings** under the project `WARN` set. The full link is
still red: `src/main.c:409` calls `render_draw` with 4 arguments. `make test` cannot pass until main.c is
wired (it does not link render.c at all, so the fm/vt/mux/layout tests are unaffected by this edit — they
were green at s72 entry 4 and nothing they link changed).

**Next action (step 2):** `src/main.c` — the `FileMgr` instance, the worker thread + result fd, key and
pointer routing, focus, `cd` follow, `fm_set_size()`/`fm_paint()` from the layout/frame path, and the
`layout_initial_size()` arguments.

### s73 entry 3 — step 2 done: `src/main.c` wires the file manager; the binary links and `make test` is green

**`src/fm.c` + header — two functions main.c needs rather than re-derives:**
- `fm_set_hidden(fm, show)` — dotfiles on/off by rebuilding `vis[]` from the listing already in memory
  (no second `opendir`), clamping the selection into the new view and marking the model dirty. Without it
  the `.` shortcut would have had to call `fm_refresh()` and re-read the disk.
- `fm_row_pos(fm, row)` — the list pane's row -> view position, using the painter's own `list_metrics()`.
  A click must select the entry drawn under it, including in a pane too short for its header/footer where
  the entries start at row 0; that rule belongs next to the painter, not in main.c.

**`src/main.c` — the wiring (all new code is in one section, "the in-window file manager: the wiring"):**
- `static FileMgr g_fm;` plus the worker state: two request slots (`FMQ_SCAN`/`FMQ_PREVIEW`, each holding
  only the NEWEST request of that kind — a superseded request is overwritten, so the disk is not even
  read for it), a mutex + condvar, a quit flag, and a pipe whose read end joins epoll.
- `fm_worker()` runs **only** `fm_scan_build()` / `fm_preview_build()` — the two fm.c functions that touch
  nothing but malloc, the filesystem and their argument — so it never reads or writes `FileMgr`. The main
  thread keeps ownership and installs every answer, which is why the request-id rule in `fm_install_*()`
  is the only synchronisation the model needs. Answers cross the pipe as one 24-byte `FmResult` (atomic,
  far under `PIPE_BUF`); a write that does not land is freed on the spot rather than leaked.
- `fm_worker_start()` is **lazy**: no thread and no descriptor exist until the band is first opened, so a
  session that never uses the feature runs exactly the code it ran before. `fm_shutdown()` sets quit,
  broadcasts, joins, then drains the pipe (draining AFTER the join is what makes it leak-free) and frees
  the model.
- `layout_apply()` now fills `LayoutReq.fm_on/fm_preview_on/fm_rows` and calls `fm_set_size()` with the
  band's rects — the one place that knows them. `redraw()` calls `fm_paint(conf.fg, conf.bg)` when
  `g_fm.dirty`, after the sizing and before `render_draw(..., &g_fm)`.
- Keyboard: `key_file_manager` (toggle), `key_fm_focus` (Ctrl+Shift+Up -> browser), `key_fm_release`
  (Ctrl+Shift+Down -> terminal) are matched with the tab bindings and work from anywhere. `fm_keys()` is
  matched just before the PTY paths and only while the band owns the keyboard — that gate is what makes
  the bare `Up`/`r`/`Escape` defaults safe. Movement keys return 1 (holding one walks the listing, and the
  existing key-repeat machinery does it); open/parent/refresh/preview/hidden/quit return 0.
- Pointer: `layout_hit_fm()` is checked in `ptr_button()` BEFORE the mouse-reporting branch (the bar's
  rule — an application that grabbed the mouse must not eat the browser). A click in the browser selects
  the row under the pointer via `fm_row_pos()`; a click in the preview takes the keyboard; a click in a
  terminal pane hands the keyboard back (`fm_focus_set(FM_FOCUS_TERM)`), so pointer and keyboard never
  disagree. `fm_wheel()` runs first in `ptr_scroll_done()`: over the band the wheel walks the listing
  instead of scrolling scrollback.
- Shell follow: `fm_send_cd()` writes `cd '<path>'\r` to the active session, single-quoted with the
  shell's `'\\''` escape (shared `shell_quote()` helper with the path-typing action), and never to a
  session on the alternate screen — vim/htop/less would receive it as typing.
- **Enter on a file types its quoted path at the prompt WITHOUT a newline.** Kai-Term does not guess what
  "open" means for a file and never runs one. (Flagged for the user: if they want Enter to actually
  launch something, that is a one-line change with different safety implications.)
- Startup: `fm_init(&g_fm, wdir)` — the browser starts where the shell starts; `layout_initial_size()`
  gets `conf.fm_on, conf.fm_rows`; with `file_manager = 1` the first listing is requested right before
  the loop, once the worker and its fd exist.

**Makefile:** `src/fm.c` was missing from `SRCS` — the first link of the real binary failed with ~30
`undefined reference to fm_*`. It had been compiled only through the `bin/test_fm` pattern rule, which is
why s72's green `make test` never noticed. Added to `SRCS` (order: after `src/layout.c`). No `-lpthread`
was needed: glibc 2.43 has pthread in libc (measured with a standalone `pthread_create` probe compiled
without `-lpthread`).

**Two mistakes made and fixed in this session, both recorded because they are traps:**
1. An `edit_file` anchored on the literal text `int main(void)` and matched the **string literal** for
   `code.c` inside `make_tree()` (line 226) instead of the real `main()` at line 875, injecting a whole
   function into a test fixture. Repaired with a Python pass that cut the block out and re-inserted it
   before the real `main()`; the fixture literal was restored byte-for-byte. Rule: anchor on something
   unique (`\nint main(void)\n{\n    kai_term_loglevel`), never on a bare identifier that also appears in
   test data.
2. A new check asserted that toggling dotfiles back ON restores the previous selection. It does not, and
   the model is right: the selection is a position in the view, so it stays where the clamp left it. The
   check now pins that behaviour instead of the one I had assumed.
3. Also fixed a pre-existing `-Wparentheses` warning in `test_fit` (`A || B && C` — the meaning was
   already right, the parentheses were missing). It was in the tree at s72's close despite the
   "0 warnings" entry, because `make test` output was being filtered when the count was read.

**Measured (this session, after the wiring):** `make -j8` -> exit 0, `bin/kai-term` links. `make test` ->
CLI 92/0/0, `test_vt` 613/0, `dbg` MODEL OK, `test_fm` **220 checks 0 failures** (198 -> 220: the 22 new
`fm_set_hidden`/`fm_row_pos` checks). Note `test_fm` was already 198 at session start, not the 193 s72
entry recorded — the tree carried 5 more checks than the journal said (the interrupted step-4 session),
which is another reason to measure rather than trust.

**Next action:** clean-tree re-verification (`make clean && make`, `make test`, `make asan` with exact
counts), then README + ISSUES.md, then a live run for the user's eye.

### s74 entry 1 — session start: measured the real state; the feature is wired and green, the docs and the user's eye are what is left

User request: "continue working on the Kai-Term Project" -> finish the integrated file manager (TODO.md),
picking up at s73's handoff ("clean-tree re-verification, then README + ISSUES.md, then a live run").

**s74 measured start (before any edit this session):**
- `git status -sb` -> `main` == `origin/main` (HEAD `f75be90`); modified: `AGENT.md Makefile
  include/kai-term/kai-term.h src/config.c src/layout.c src/main.c src/render.c src/vt.c
  tests/cli_test.sh tests/test_vt.c`; untracked: `src/fm.c src/img.c tests/test_fm.c`. Matches s73's
  description — steps 1-4 (img.c, fm.c, test_fm.c, layout band, config keys, render band, main wiring)
  are all in the tree, uncommitted.
- `make clean && make -j8` -> exit 0, **0 warnings** (link line carries `-lpng16 -lz -ljpeg -lwebp`).
- `make test` -> exit 0: `CLI: 92 checks, 0 failures, 0 skipped`, `test_vt` **613 checks 0 failures**,
  `dbg` **MODEL OK (0 problems)**, `test_fm` **220 checks 0 failures**.
- `make asan` -> exit 0: `test_vt_asan` 613/0, `dbg_asan` MODEL OK, `test_fm_asan` 220/0, and 0 lines
  matching AddressSanitizer|runtime error|LeakSanitizer|SUMMARY|ERROR over the whole run.
- **The feature runs for real** (log evidence, not a screenshot): `./bin/kai-term -c <conf with
  file_manager = 1> -d -w .scratch/s74/tree -e 'sleep 4'` logs `fm: worker started (result fd 35)`,
  `fm: dropping stale preview of … (request 2, newest 3)` (the request-id rule firing on a real race),
  and `graphics: 1 image(s) drawn, 0 texture(s) held` — the PNG in that directory reached the preview
  texture and the draw path. The `-d` grid line proves the band costs the terminal its rows: 154x51 with
  `fm_rows = 10` in a 1253x1002 window (61 rows without the band).

**s74 objective:** close the feature out — the two things s73 left undone are (a) documentation that
matches the tree (README has no file-manager section, no `fm_*` settings, no libjpeg/libwebp in
Requirements; ISSUES.md has no file-manager limitations) and (b) the user's eye on it. Also close the
evidence gap that the *interactive* half (toggle binding, FM keys, focus handover, mouse) has never been
exercised: nothing in any test binary links `main.c`, so the routing gate — "the terminal is untouched
unless the band owns the keyboard" — is currently unverified.

**s74 planned steps (each one: edit -> AGENT.md -> build/test):**
1. Add the missing debug log line for the `cd <path>` the shell gets (`fm_send_cd()` performs a
   `pty_write` and logs nothing, so the follow-the-directory behaviour has no client-side evidence).
2. Live key-injection run (`.scratch/inject`, uinput -> niri -> the real window) with `-d`: toggle the
   band open, walk the listing, Enter on a directory (navigation + `cd`), `Ctrl+Shift+Down` to release
   the keyboard, then prove the bare `Up`/`Down` keys reach the PTY again (ground truth = the bytes the
   PTY received, per ENVIRONMENT.md, not the screen), then close it. This is the A/B of the routing gate.
3. README: a "File manager" feature bullet + a usage section (keys, mouse, what Enter does), the 20
   `fm_*`/`file_manager`/`fm_rows`/`fm_preview`/`fm_show_hidden`/`fm_follow`/`key_*` settings rows, and
   libjpeg + libwebp in Requirements.
4. ISSUES.md: a "File manager" limitations group (what is and is not implemented, the caps), and refresh
   the stale "Open" verification line with this session's counts.
5. `make clean && make`, `make test`, `make asan` from a clean tree with exact counts; commit; hand the
   user a live command and record their verdict.

**s74 expected outcome:** docs that describe only what exists, a green clean-tree run, one commit on
`main`, and the visual verdict from the user — this project's rule for anything visual.

### s74 entry 2 — two log lines made honest, and a live injection run found a REAL input-routing bug

**Edits made (both first, then measured):**
- `src/main.c` `fm_send_cd()`: it performed a `pty_write` and logged nothing, so the
  follow-the-directory behaviour had no client-side evidence at all. Now logs
  `fm: told the shell to follow: cd <dir>` AFTER the write, naming the directory the model actually holds
  (the project's "a log line that prints its own argument can lie" rule — the alt-screen gate can refuse).
- `src/render.c`: the `graphics: N image(s) drawn, M texture(s) held` line was folding the file-manager
  preview into the application-picture count, while `M` counts only the `ImgTex` cache and the preview has
  its own texture (`Render.fm_tex`). Measured: a preview alone logged
  `graphics: 1 image(s) drawn, 0 texture(s) held` — a number that contradicted itself. The two are now
  counted and logged separately: `... 0 image(s) drawn, 0 texture(s) held + the file-manager preview`.

**Live run (`.scratch/s74/inj_test.sh`, the project's own technique): real `bin/kai-term` in the real
niri session, keys injected through uinput (`.scratch/inject`), ground truth = the bytes the PTY received
(`-e 'stty raw -echo; cat > REC'`), not the screen.** `stty raw` was needed: in canonical mode the tty
line discipline holds every line until Enter, so the arrow bytes would not have appeared in order.

PTY received, in order: `ab` `'/home/kaida/…/tree/b.jpg'` `^M` `^[[A^[[B` `z^M` `'/home/kaida/…/tree/a.png'` `^M`
Log: `fm: opened on …`, `fm: typed the selected path at the prompt: …b.jpg`, `fm: keyboard -> terminal`,
`fm: closed — the terminal is the whole window again`, `fm: opened on …` (reopening keeps the listing).

**What that proves (all good):** `Ctrl+Shift+F` opens and closes the band; while the band owns the keyboard
`Down` `Down` never reach the PTY; the selection really moved (Enter typed `b.jpg`, two entries down from
`sub`); `Ctrl+Shift+Down` hands the keyboard back and the very next `Up`/`Down` arrive as `ESC[A ESC[B`;
ordinary typing works after closing; the band survives close/reopen.

**The bug it found — every one-shot file-manager key ALSO reached the shell.** `send_key()`'s return value
means "is this press repeatable" (1 = arm the key repeat, 0 = handled one-shot), and falling off the
function is what sends the key to the PTY. The call site was `if (fm_keys(base, kbmods)) return 0;`, and
`fm_keys` returned 0 for BOTH "not one of mine" and "handled, do not repeat" — so the one-shots fell
through. Measured consequences: **Enter on a file typed its quoted path and then sent `^M`, i.e. the shell
would have EXECUTED it** — the exact thing the design said it would never do; `BackSpace` would go to the
parent AND delete a character at the prompt; `r`/`p`/`.` would type themselves at the prompt; `Escape`
would close the band AND send ESC to the shell. Two meanings in one int, and the compiler could not see it.

**Fix:** `fm_keys()` now has three states — `-1` not the band's key, `0` handled and must not repeat,
`1` handled and repeatable — and the call site is `int fmk = fm_keys(...); if (fmk >= 0) return fmk;`.
The movement keys keep auto-repeat (holding an arrow walks the listing); the one-shots are now consumed.

**Also found in the same run (a test-script bug, not a product bug):** step 10 pressed `Up` once expecting
to land on the directory `sub`, but the selection was on `b.jpg`, so it landed on `a.png` — the `cd`
follow was therefore not exercised. The re-run presses `Up` twice and adds BackSpace / `r` / `p` / `.` /
`Escape` checks, because those are exactly the keys this bug affected.

**Next action:** rebuild, re-run the injection test against the fixed binary (A/B against the pre-fix
`src/main.c` copy in `.scratch/s74/` if the byte stream alone is not convincing), then README + ISSUES.md.

### s74 entry 3 — the second live run found a WORSE bug: every listing after a navigation was thrown away

Re-ran with a script aimed at exactly the keys the first bug affected
(`.scratch/s74/inj_keys.sh`: Return, `r`, `p`, `.`, Escape, BackSpace, plus the focus handover both ways).

**The one-shot routing fix works.** PTY bytes for the whole run:
`ab` `'/home/kaida/…/tree/b.jpg'` `cd '…/tree'^M` `cd '…/tree/sub'^M` `cd '…/tree'^M` `^[[A^[[B` `z^M`
— no `^M` after a typed path (Enter no longer executes it), no `r`, no `p`, no `.`, no `ESC`, no `^H`.
The `^[[A` after Escape and the `^[[A^[[B` after `Ctrl+Shift+Down` prove the keyboard really returns to the
shell. `fm: open -> …/sub`, `fm: parent -> …/tree`, `fm: told the shell to follow: cd …` all appear.

**But the log exposed a second, more serious defect:** every listing issued after a navigation carried
request id 0 — `fm: dropping stale listing of …/tree/sub (request 0, newest 9)`. Cause:
`fm_nav(fm_go_sel(&g_fm, &seq), seq, "open")`. C evaluates function arguments in an **unspecified
order**, and gcc reads the caller's `unsigned long seq = 0` BEFORE `fm_go_sel()` fills it. Proved in
isolation with `.scratch/s74/argorder.c` (same shape, `-O2` and `-O0`): prints `seq=0`. Consequence:
`fm_install_scan()` compares the answer's id with `fm->scan_req`, they never match, so **the listing of
every directory the user navigated into was discarded and the browser kept showing the directory they had
left** — while the model's own `dir` had moved, so the header, the preview and the `cd` were all for a
directory whose entries were never displayed. Compiles with zero warnings under the project's `WARN` set.
Fixed at all three sites (`open`, `parent`, `refresh`): the model call now happens in its own statement
and `seq` is read afterwards. `grep` over `src/` finds no other out-param-shared-with-arguments call.

**Also visible in that byte stream, and wrong:** pressing `r` (refresh) typed `cd '<current dir>'\r` at
the prompt — `fm_nav()` sends the `cd` unconditionally, and a refresh is a navigation that does not change
the directory. Typing and executing a command at the user's prompt because they asked to re-read a listing
is not acceptable; the `cd` must only go out when the directory actually changed.

**Next action:** make `fm_send_cd()` fire only on a real directory change, rebuild, re-run
`inj_keys.sh` and require: no `cd` line for `r`, and no `dropping stale listing` for a navigation.

### s74 entry 4 — all three fixes verified by the same harness; the suite is still green

Third fix: `fm_send_cd()` now keeps `fm_last_cd` (the directory the shell was last sent to, declared with
the other file-manager state because `tab_switch()` resets it) and returns early when the directory has
not changed. `fm_last_cd` is seeded from `g_fm.dir` at startup — the shell already starts there — and
cleared by `tab_switch()`, so the session that becomes visible is re-synced by its next navigation
instead of being skipped because some other session had been told.

**Re-run of `.scratch/s74/inj_keys.sh` against the rebuilt binary (exact PTY bytes received):**
`ab` `'/home/kaida/…/tree/b.jpg'` `^[[A` `cd '/home/kaida/…/tree/sub'` `^M` `cd '/home/kaida/…/tree'` `^M` `^[[A^[[B` `z^M`

| Requirement | Result |
|---|---|
| Enter on a file types the path and sends **no** newline | ✓ no `^M` after the quoted path |
| `r` / `p` / `.` / Escape / BackSpace never reach the PTY | ✓ no `r`, `p`, `.`, `ESC`, `^H` anywhere |
| A navigation's listing is installed, not dropped | ✓ **no `dropping stale listing` line at all**; `fm: listing …/sub (request 9)` and `(request 10)` both land |
| Refresh does not type a `cd` at the prompt | ✓ the only two `told the shell to follow` lines are the two real navigations |
| The keyboard returns to the shell | ✓ `^[[A` right after Escape, `^[[A^[[B` after `Ctrl+Shift+Down` |
| Closing and reopening works | ✓ `fm: closed …` / `fm: opened on …`, listing kept |

The one remaining `dropping stale preview … (request 2, newest 3)` at open is the designed rule working:
`fm_open()` previews the directory before any listing exists, and `fm_drain_results()` re-asks once the
listing lands (a listing request always invalidates an in-flight preview, deliberately, because a refresh
means the file may have changed).

**Measured:** `make -j8` exit 0, 0 warnings. `make test` -> exit 0: `CLI: 92 checks, 0 failures, 0 skipped`,
`test_vt` **613 checks 0 failures**, `dbg` **MODEL OK (0 problems)**, `test_fm` **220 checks 0 failures**.
`make asan` -> exit 0: 613/0 + MODEL OK + 220/0, no sanitizer report. (Neither `main.c` nor `render.c` is
linked into any test binary, so these counts cannot move from these edits — they are re-run to prove that,
not to claim coverage.)

**Coverage gap, stated honestly:** the input-routing contract (`send_key`'s 0/1 return, `fm_keys`'s
-1/0/1) lives in `main.c`, which is in no test binary, so these three bugs are covered only by the
injection harness in `/home/kaida/Project/.scratch/s74/` (`inj_keys.sh` + `inj_test.sh`), not by
`make test`. Adding key injection to `tests/cli_test.sh` was rejected: it would make the test suite depend
on a live compositor and an injected keyboard, which the suite deliberately does not.

**Next action:** README (feature section, the 20 file-manager settings, libjpeg/libwebp in Requirements,
the shortcut table) and ISSUES.md (a "File manager" limitations group + refresh the stale "Open"
verification line), then a clean-tree re-verification, a commit, and the live run for the user's eye.

### s74 entry 5 — README now documents the file manager (every claim read back off the source)

`README.md`:
- **Features**: a "File manager" bullet between "Side-by-side view" and "Font zoom".
- **Requirements**: `libjpeg` and `libwebp` added (the Makefile's `PKG` has carried them since s71; the
  list was stale). GIF needs no library — it is decoded in `src/img.c` because giflib is not installed here.
- **Usage → "### File manager"** (new, after "### Images"): what the band looks like, what the browser's
  header/footer/rows show (name, `@` for a symlink, size for a file — read off `paint_entry()`), a table of
  what the preview shows for a directory / image / text / anything else, the key table, the mouse rules,
  the focus handover, and the `cd` follow. Every number in it is the source's own: 512 directory entries
  (`FM_PREVIEW_DIR_MAX`), 256 KiB of text (`FM_PREVIEW_BYTES`), the 13 highlighter languages and the
  extension map (`fm_lang_for_path()`), magic-byte sniffing rather than file names (`img_sniff`).
- **Available settings**: 20 rows added — `file_manager`, `fm_rows`, `fm_preview`, `fm_show_hidden`,
  `fm_follow`, `key_file_manager`, `key_fm_focus`, `key_fm_release` and the 12 `fm_key_*` — each with the
  default `config_default()` really sets. The clamping paragraph now names the band height, and a new
  paragraph explains why `fm_key_*` may be unmodified when no other binding may.
- **Keyboard shortcuts**: `Ctrl+Shift+F` and `Ctrl+Shift+Up`/`Down` rows, a "File-manager keys" subsection
  at the end of the section (first placed mid-section, which orphaned the selection/repeat prose under it —
  moved), and the key-repeat paragraph now says the movement keys repeat and the one-shots do not.

Nothing is documented that the tree does not do: no "open with default application", no thumbnails, no
multi-select, no tabs in the browser.

**Next action:** ISSUES.md — a "File manager" limitations group and a refresh of the stale "Open"
verification line (it still quotes the pre-file-manager 553-check counts).

### s74 entry 6 — ISSUES.md updated; one new limitation found by probing, not by reading

`ISSUES.md`:
- `## Open` verification line refreshed to this session's numbers (it still quoted the pre-file-manager
  553 / 86 counts). States plainly that the file manager's **appearance has not been confirmed by a human
  eye** — that is the one open item, and it belongs to the user, not to the code.
- New `**File manager**` limitations group (10 bullets), each checked against the source: the band costs
  the terminal rows and the terminal wins (`FM_ROWS_MIN/MAX`, `FM_TERM_MIN_ROWS`, the 154x51 vs 154x61
  `-d` line); one browser per window shared by every session; the `cd` follow is one directory deep and
  never reaches the alternate screen; Kai-Term never opens a file; the caps (8192 entries, 512 preview
  entries, 256 KiB of text, 16 MiB of image file, then the decoder's 4096 px / 16 Mpx) and the
  worker-thread + stale-answer rule; the text preview is not a viewer and cuts long lines instead of
  wrapping them; fixed sorting; a click selects but does not open; the band is not part of the terminal's
  grid, so selection/scrollback/mouse-reporting do not reach it.

**A claim I nearly wrote wrong, caught by measuring.** I had written "an animated GIF or a multi-frame
WebP shows frame one". `.scratch/s74/animprobe.c` (links the real `src/img.c`) on 4-frame files made by
PIL says otherwise: `anim.gif: sniff='gif' decoded=yes 20x20` but
`anim.webp: sniff='webp' decoded=NO 0x0` + `image: webp: decode failed (20x20)` — libwebp's simple decode
refuses an animated container entirely. The preview still behaves (metadata + `webp file could not be
decoded`), so this is a documented limitation, not a bug; decoding that first frame would need
`libwebpdemux` (1.6.0 is installed here), which the project's "no unnecessary dependencies" rule keeps
out. Same probe confirms PNG / JPEG / static WebP / GIF all decode (60x40 each). ISSUES.md now says what
was measured.

**Next action:** refresh AGENT.md "Project Files & Facts" (it still describes 9 sources and no fm.c /
img.c) and "Next Steps", then `make clean && make`, `make test`, `make asan` from a clean tree with exact
counts, commit, and hand the user a live command for the visual verdict.

### s74 entry 7 — AGENT.md's standing sections brought in line with the tree

- **Project Files & Facts**: the tree block now lists `src/img.c`, `src/fm.c`, `tests/test_fm.c` and the
  current check counts (613 / 220 / 92); new per-file entries for `src/img.c` (one decoder set for both
  the kitty path and the previews, GIF first-block-only, animated WebP refused) and `src/fm.c` (model only,
  the two worker-callable builders, `fm_image_fit`); `src/layout.c` now names `FmGeom` and `layout_hit_fm()`.
- **Architecture & Invariants**: new "**File manager band**" block — layout splits the grid area vertically
  first and the band horizontally, sessions stay ordinary `pane[]` entries (that is what keeps every
  existing hit test working), the band is clamped in layout.c and the terminal wins, `FmPane` is a `Cell`
  grid so the renderer needs no new path and shares `emit_cell_glyph()`, the worker may call exactly two
  functions and the request-id rule is the only synchronisation, and the keyboard gate is the feature's
  safety.
- **Key Decisions & Pitfalls**: three new traps, each from this session — an out-parameter passed as an
  argument next to the call that fills it (unspecified evaluation order, zero warnings, `.scratch/s74/
  argorder.c` reproduces it); one int return meaning both "not handled" and "handled, do not repeat"; and a
  log line that counts two different things with one number.
- **Debugging this project**: a "**File manager**" bullet listing every `fm:` line and what its absence
  means, plus the `+ the file-manager preview` suffix on the graphics line.
- **Next Steps**: the file manager is now item 1 with exactly one open item (the user's eye) and the two
  decisions worth their sign-off (`Return` types instead of runs; `fm_follow` sends `cd` unprompted).

**Next action:** `make clean && make`, `make test`, `make asan` from a clean tree with exact counts, then
commit, then hand the user a live run for the visual verdict.

### s74 entry 8 — clean-tree re-verification, all green; committed

Exact commands and results, from `make clean`:
- `make clean && make -j8` -> exit 0; `grep -ciE "warning|error"` over the whole build log -> **0**.
- `grep -rnE "TEMPORARY|PROBE|EXPERIMENT|NEGATIVE" src/ include/ tests/` -> **0** (no probe left in tree).
- `grep -rn "TODO|FIXME|XXX|HACK" src/ include/ tests/` -> **1** hit, and it is the test payload
  `"\x1b[41mXXXXXXXXXX\x1b[0m"` at `tests/test_vt.c:788`, not a task marker.
- `make test` -> exit 0: `CLI: 92 checks, 0 failures, 0 skipped`, `test_vt` **613 checks 0 failures**,
  `dbg` **MODEL OK (0 problems)**, `test_fm` **220 checks 0 failures**.
- `make asan` -> exit 0: 613/0 + MODEL OK + 220/0, and `grep -cE
  "AddressSanitizer|runtime error|LeakSanitizer|SUMMARY|ERROR"` over the run -> **0**.

**Session's code changes, all in `main.c` and `render.c` (the two files no test binary links), all verified
by the live injection harness rather than by the unit tests:**
1. `fm_keys()` three-state return — a one-shot file-manager key no longer also reaches the shell (Enter on
   a file used to type its path AND a newline, i.e. run it).
2. Out-parameter evaluation order at the three `fm_nav()` call sites — a navigation's listing is now
   installed instead of dropped as stale.
3. `fm_send_cd()` fires only on a real directory change (`fm_last_cd`, cleared by `tab_switch()`) — the
   refresh key no longer types a command at the prompt.
4. Two log lines made honest: `fm: told the shell to follow: cd …` added; the `graphics:` line counts the
   file-manager preview separately.

**Committed** (one commit: `src/fm.c`, `src/img.c`, `tests/test_fm.c` new; `Makefile`,
`include/kai-term/kai-term.h`, `src/{config,layout,main,render,vt}.c`, `tests/{test_vt.c,cli_test.sh}`,
`README.md`, `ISSUES.md`, `AGENT.md` modified). `bin/` and `build/` stay git-ignored (verified with
`git check-ignore`).

**STILL OPEN — the one thing this session cannot do:** the file manager has never been seen by a human eye.
Per this project's rule the visual verdict belongs to the user. Ask them to run it and record what they
say; until then ISSUES.md "Open" and AGENT.md Current Status both say so explicitly.

**Next action:** give the user the live command (`./bin/kai-term -d`, `Ctrl+Shift+F`), record their verdict,
then `git fetch` + push. If they report anything about how it looks, start from the `fm:` log lines listed
under "Debugging this project", not from the shader.

(Previous task, s70 — image support close-out; its entry follows.)

## Old Current Task (s70 — CLOSED: image support done, user-confirmed by eye)

**s70 objective:** close out the image-support work (s68 model/parser + s69 renderer/tests). What was
left per the s69 entry: README (feature entry + libpng/zlib in Requirements), ISSUES.md limitations,
a clean-tree re-verification of build/test/asan, a commit, and a live `timg` run for the user to
confirm by eye.

**s70 measured start (before any edit this session):** `git status -sb` -> `main` == `origin/main`,
modified-uncommitted: `AGENT.md Makefile include/kai-term/kai-term.h src/main.c src/pty.c src/render.c
src/vt.c tests/test_vt.c` (+1860/-56). So the whole s68+s69 implementation is in the tree, uncommitted.
README.md and ISSUES.md are NOT yet touched by it — README still lists no libpng/zlib dependency and
no image feature; ISSUES.md "Open" still quotes the pre-image 501-check counts.

**s70 MEASURED — the s68 auto-detect claim was WRONG, and `timg foo.png` shows nothing today.**
Probes in `/home/kaida/Project/.scratch/s70/` (`capture.py`, `gate.py`): timg 1.6.3 run in a PTY with
Kai-Term's winsize (80x24, ws_xpixel 384 / ws_ypixel 640), its queries answered from the harness, and
the strings of `/usr/bin/timg` read directly. Results:
- timg's probe sequence is `ESC[>q ESC[5n` (250 ms wait), then `ESC[c` (250 ms), then
  `ESC]11;?ESC\` (1501 ms). It prints `Query: ... Response: '' (250ms)` for each under `--verbose`.
- Answering `ESC[>q` with a kitty-style DA2 (`ESC[>1;1030;0c`) AND `ESC[5n` with `ESC[0n` AND `ESC[c`
  with a DA1 that has NO `;4`: still `Effective pixelation: Using quarter block.` — **no combination of
  query answers makes timg choose kitty graphics.** The s68 entry's "with 640x384 at 80x24 it prints
  cell-pixels: 4x26 and emits kitty" is false; the captured `k_raw.ans`/`big_k0.ans` came from the
  forced `-p k` runs in `.scratch/s68/probe.py`. `cell-pixels: 4x26` was real, the kitty emission was not.
- What DOES gate kitty: `TERM=xterm-kitty` or `TERM=xterm-ghostty` (measured: APC=1, "Using kitty
  graphics") — and only together with non-zero pixel geometry (`TERM=xterm-kitty` with ws 0x0 ->
  quarter block). `TERM_PROGRAM=kitty|ghostty|WezTerm|konsole` does nothing. The list is hardcoded in
  the binary (strings at 262921: `TERM`, `xterm-kitty`, `xterm-ghostty`, `TERM_PROGRAM`, `WarpTerminal`,
  plus known-DA1 signatures `iTerm2`, `Konsole 2`, `WezTerm`, `mlterm`, `XTerm`, `foot`, `tmux`,
  `WindowsTerminal`).
- What gates SIXEL: the DA1 answer containing `;4`. Answering `ESC[c` with
  `ESC[?64;1;2;4;6;7;12;16;18;22c` -> "Using sixel graphics"; the same answer without `;4` -> quarter
  block. **Kai-Term's DA1 answer (src/vt.c, `case 'c'`) is exactly the `;4` one**, and s68 made vt.c
  DISCARD DCS — so today `timg foo.png` in Kai-Term emits sixel that is thrown away: the user sees
  nothing at all, where before the image work they saw quarter blocks.

**s70 CONSEQUENCE (next action, in this order):**
1. `src/vt.c` DA1 answer must stop claiming graphics Kai-Term does not have: drop `4` (sixel), `16`
   (sixel+ReGIS) and `18` (wide sixel) from the response. Verified by re-running the harness: timg must
   fall back to quarter blocks instead of sixel. No test pinned the old string (grep for `64;1;2` in
   tests/ -> nothing), so add one that pins the new answer.
2. Answer `CSI 5 n` (DSR, "terminal OK") with `CSI 0 n` — Kai-Term never answered it, and it is the
   second half of timg's first probe (it currently burns the full 250 ms timeout). Test it.
3. README must NOT promise `timg foo.png` auto-detects graphics: document `timg -p k foo.png` and say
   why (measured above). ISSUES.md gets the limitation.
4. Then the live check for the user is `timg -p k <png>`.

**s70 IN PROGRESS — README examples must be verified, not assumed.** The first draft of the README
"Images" section showed `timg --grid=2x2` and `timg -V animation.gif`; neither has been run through
Kai-Term. Before documenting them: capture what timg 1.6.3 really emits for plain / `--grid=2x2` /
`-V` in a PTY with Kai-Term's winsize (80x24, cell 8x16 -> ws 640x384) into
`/home/kaida/Project/.scratch/s70/`, then feed each capture through the existing headless harness
(`.scratch/s68/gfx_probe.c`, rebuilt against the current sources) and read image count / span /
cursor. Anything that does not behave gets dropped from the README or moved to ISSUES.md.

**s70 LIVE LOG EVIDENCE (renderer path reached, not yet seen by a human):**
`timeout 10 ./bin/kai-term -d -e 'timg -p k /home/kaida/Project/.scratch/s70/big.png; sleep 4'` ->
`grid resized to 154x61 (window 1253x1002 px, cell 8x16, padding 8, ...)`, then
`graphics: 0 image(s) drawn, 0 texture(s) held` -> `graphics: 1 image(s) drawn, 1 texture(s) held`, no
GL warning, clean exit (`the last session is gone, exiting`). So the picture really went through decode
-> texture -> draw on the real NVIDIA/GL 4.6 context; whether it LOOKS right (aspect, position, text on
top) was still open at that point — closed by the user's verdict below.

**s70 remote synced:** `git fetch` first — nothing on GitHub to integrate, so `git push origin main` was a
fast-forward (exit 0). Re-fetched: `main` == `origin/main`, `git status -sb` shows no ahead/behind marker.
Read the truth with `git status -sb` / `git log --oneline origin/main -1` — the commit recording this line
is itself pushed.

**s70 CLOSED — user confirmation received:** "The image display properly. The feature works as expected"
(2026-10-17, their own window, `timg -p k` on a PNG). So the picture, its aspect, its placement at the
cursor and the text over it are confirmed by eye, not only by log line. Nothing about the feature is
open; the remaining limits are the documented ones in ISSUES.md "Graphics / images" (no sixel/iTerm2,
no crop/file/animation actions, one background per picture, raw-format `s=` unverified).

**s70 STATE at session close:** image support (s68 model + s69 renderer/tests + s70 fixes and docs) is
implemented, documented (README "Images", ISSUES.md "Graphics / images"), committed and pushed — read the
hash with `git log --oneline -1`, not from here. If a picture ever looks wrong (aspect, position, text
under it), start from the `-d` line `graphics: N image(s) drawn` and the span math in
`image_span_rows/cols`, not from the shader.

**s70 DONE — three source fixes, each measured, tests 542 -> 553.**
1. `src/vt.c` DA1 answer: `ESC[?64;1;2;4;6;7;12;16;18;22c` -> `ESC[?64;1;2;6;7;12;22c`. Kai-Term has no
   sixel/ReGIS (the parser discards DCS) and was claiming it. A/B PROVED the behaviour change
   (`.scratch/s70/timg_gate.c` + `vt_old.c` = the same vt.c with the old string, both linked against the
   real parser and fed the real timg output): old -> `Using sixel graphics`, 8015 bytes of DCS, model
   images=0, i.e. nothing drawn; new -> `Using quarter block`, 26620 bytes of block glyphs, visible
   output. Test pins the exact new string and asserts `;4;`/`;16;`/`;18;` are absent.
2. `src/vt.c` answers `CSI 5 n` (DSR) with `ESC[0n` — never answered before; it is the second half of
   timg's first probe, which then burns its whole 250 ms timeout. Test pins it, and pins that `CSI 6 n`
   still answers the cursor position.
3. `src/vt.c` `kitty_complete()`: a transmission naming an id that already exists now REPLACES that
   image instead of appending another. Found by feeding the real `timg -p k -V anim.gif` bytes through
   the model: timg alternates between TWO ids for a four-frame gif, and the old code kept 4 pictures
   (crow 0,2,4,6) where 2 belong. Now: 2 images, `version` 2 each. `a=T` re-places at the cursor and
   moves it; `a=t`/`a=f` replace the pixels without moving anything. 6 new checks pin all of that.
   Also fixed a brittle absolute count in the `a=d` test (`== 9` -> `n_alive - 1`).

**s70 MEASURED — what timg 1.6.3 actually does against Kai-Term** (`.scratch/s70/timg_gate.c`: real
`timg` in a real PTY with Kai-Term's winsize, its bytes through Kai-Term's real `term_parse()`, and
Kai-Term's own queued answers written back; no display): with `TERM=xterm-256color` (what pty.c sets)
and ws 384x640 px: `timg foo.png` -> quarter blocks; `timg -p k foo.png` -> kitty graphics, model holds
1 image 64x40 px, span 8x3 cells, cursor below it. With `TERM=xterm-kitty` even plain `timg foo.png`
-> kitty graphics. `--grid=RxC` arranges MULTIPLE files (measured: `--grid=2x2` with one file sends one
image; `--grid=1x2 t64.png big.png` sends two, the second in 9 chunks) — so the README draft's
`timg --grid=2x2 foo.png` example was wrong and is dropped.

**s70 VERIFIED myself from a clean tree (not inherited from s69), exact commands:**
- `make clean && make` -> exit 0, 0 warnings, 0 errors (WARN set unchanged; link line now carries
  `-lpng16 -lz`).
- `make test` -> exit 0, **542 checks, 0 failures** + `MODEL OK (0 problems)` + `CLI: 86 checks, 0
  failures, 0 skipped`. Note: 542, not the 540 the s69 entry recorded — the tree is what counts.
- `make asan` -> exit 0, **542 checks, 0 failures** + `MODEL OK (0 problems)`, and 0 lines matching
  AddressSanitizer|runtime error|LeakSanitizer|SUMMARY|ERROR.

**s70 planned steps:** (1) re-verify from a clean tree myself instead of trusting the s69 entry:
`make clean && make`, `make test`, `make asan` — record exact counts. (2) README: add the image
feature to Features, `libpng` + `zlib` to Requirements, and a limitations note where usage describes
what an app can do. (3) ISSUES.md: refresh the "Open" verification line with the new counts and add
the image limitations (raw-format `s=` semantics unverified against a real client, byte budget /
capacity behaviour, anything else the source shows). (4) commit the whole s68+s69 change set, `git
fetch`, push. (5) hand the user a live `timg` command to confirm by eye.

**s70 expected outcome:** docs match the code (dependency list builds, feature list only claims what
is implemented), a clean-tree build/test/asan run with recorded counts, one commit on `main` pushed,
and a live check the user can run.

(Previous task, s69 — image support renderer/tests half; its entry follows and is the source for what
was changed and why.)

## Older Current Task (s69 — image support / timg, continuing s68)

**s69 objective:** finish the remaining halves of the s68 plan — `src/render.c` (image batch +
texture cache), `src/main.c` (push cell px into `Term`/`Pty`), `tests/test_vt.c` checks, README +
ISSUES wording — then re-verify build/test/asan and hand the live run to the user.

**s69 planned steps:** (1) render.c: `u_img` sampler + `a_glyph == 3` path, split the frame into
batch A (backgrounds) -> images -> batch B (bands + glyphs + chrome), one GL texture per image keyed
by `serial`, clip the quad to its pane. (2) main.c: `term_set_cell_px()` + `pty_set_cells()` from
`layout_apply()`. (3) tests: parser/chunking/base64/placement/delete/cursor-move/scroll-anchor/
DCS-discard/`CSI 16 t`. (4) README feature + libpng/zlib in Requirements; ISSUES limitations.
(5) `make clean && make`, `make test`, `make asan` from a clean tree; exact counts recorded.

**s69 expected outcome:** `timg foo.png` shows the picture at the right aspect with the prompt under
it, text stays readable on top of it, build stays warning-free, test count rises, no open issues.

**s69 state at start (measured, before any edit this session):** `git status -sb` -> `main` ==
`origin/main`, modified-uncommitted: `AGENT.md Makefile include/kai-term/kai-term.h src/pty.c
src/vt.c` (the s68 work below). `make` exit 0; `make test` -> 501 checks 0 failures + MODEL OK +
CLI 86/0/0. So the s68 model/parser work is in the tree, uncommitted, and the renderer/main/tests
half is not started.

**s68 objective:** make `timg <image>` display an image properly in Kai-Term.

MEASURED FIRST (probes in `/home/kaida/Project/.scratch/s68/`: `probe.py`, `probe2.py`, captured
`timg` output `k_raw.ans`, `k_zlib.ans`, `big_k0.ans`, `big_k9.ans`) — timg 1.6.3 at `/usr/bin/timg`:

- Pixelation choices `-p [h|q|s|k|i]`: half/quarter blocks, sixel, **kitty**, iterm2. Auto-detect
  picks **kitty graphics iff the PTY reports pixel geometry** (`ws_xpixel`/`ws_ypixel` in
  `TIOCSWINSZ`, or an answer to `ESC[16t`). With `ws_xpixel=ws_ypixel=0` it prints
  `cell-pixels: -1x-2` and falls back to quarter blocks; with 640x384 at 80x24 it prints
  `cell-pixels: 4x26` and emits kitty. `TERM_PROGRAM`/`LC_TERMINAL` did NOT change the choice here.
- `src/pty.c:pty_setsize()` leaves `ws_xpixel`/`ws_ypixel` zero, so timg never picks kitty today.
- timg's kitty emission is one APC per chunk:
  `ESC_G a=T,i=<id>,q=2,f=100,m=<more> ; <base64> ESC\` — first chunk carries the whole control
  block, continuation chunks carry only `q=2,m=0` (no `i=`). `m=1` = more chunks follow, `m=0` =
  last. Chunk size 3072 raw bytes (4096 base64 chars). Payload is **PNG** (`f=100`, magic
  `8950 4e47 0d0a 1a0a`) even with `--compress=9` — no zlib seen, so `m` is the more-flag here.
  No `s=`/`r=`/`c=` keys: timg sizes the image in PIXELS and relies on the terminal's cell pixel
  size (from winsize) to know how many cells it covers.
- After the image timg emits only `\r\n`, so it relies on `a=T` moving the cursor down past the
  image rows.
- `pkg-config`: zlib 1.3.2 and libpng 1.6.58 are installed (headers + .so present).

PLANNED STEPS:
1. `src/pty.c` — report `ws_xpixel = cols*cell_w`, `ws_ypixel = rows*cell_h` in `TIOCSWINSZ` so
   timg auto-detects graphics and gets the right cell aspect. Needs cell px passed in.
2. `src/vt.c` — APC `ESC _ G` state (growable buffer, ST or BEL terminator, like the OSC path),
   kitty control-block parser (`k=v;k=v;` + base64 payload), chunk assembly, PNG/raw decode to
   RGBA, per-`Term` image store, actions `t`/`T`/`f`/`d`/`m`/`c`, cursor move for `a=T`,
   `q=` query responses.
3. Image placement model: anchor each image to an ABSOLUTE grid row so it scrolls with the text,
   clips at the viewport edges and comes back in scrollback (must not break the `row[]`
   permutation invariant).
4. `src/render.c` — RGBA texture per image, drawn between cell backgrounds and text (text stays
   on top, as kitty does).
5. Tests: extend `tests/test_vt.c` (headless: parser, chunking, base64, placement, delete, cursor
   move, scroll anchoring). `make test`/`make asan` must stay green.
6. Docs: README (feature + any new config key), ISSUES.md limitations, this journal.

EXPECTED OUTCOME: `timg foo.png` in Kai-Term shows the image at the correct aspect ratio, the
prompt lands below it, and it scrolls with the text. Build stays warning-free; test counts go up.

DESIGN DECIDED after reading vt.c/render.c/main.c (all line refs re-read this session):
- **Anchor images by ABSOLUTE grid row, not by viewport row or row pointer.** `Grid.row_base` =
  the document row number of slot 0; slots always hold a contiguous document window (oldest at
  slot 0), so document row A is viewport row `A - row_base - scroll_off`. row_base only moves when
  a row is evicted at the top (`rot_left` of the used region in scroll_up / grid_resize_rows) or
  when the window slides over its scrollback (erase_scrollback) — no per-row bookkeeping, and it
  survives rotation, eviction and reflow. Row pointers were rejected (reflow frees every buffer,
  row_resize reallocs); a per-row ref array parallel to `wrap[]` was rejected because reflow
  rebuilds rows and would have to move refs with the cells.
- **Pixels live in the model** (`KImage.rgba`, premultiplied, same rule as the color-emoji atlas),
  the renderer keeps one GL texture per image keyed by `KImage.serial` (unique per Term, never
  reused) and drops textures for images not on screen. An image atlas was rejected: images are
  megabytes each and most of them sit in scrollback.
- **Two vertex batches instead of one** (backgrounds, then images, then bands+glyphs+chrome) so
  text stays on top of images the way kitty draws it; one extra draw call per visible image.
  The staging buffer is reused between batches (batch A is already on the GPU when B is built).
- **vt.c needs the cell size in px** (the protocol sizes images in pixels, the grid is in cells):
  `Term.cell_w/cell_h` pushed from main.c, and `Pty.cell_w/cell_h` so `pty_setsize()` can fill
  `ws_xpixel/ws_ypixel` — that is what makes timg pick kitty at all. `CSI 16 t` (cell px) and
  `CSI 14 t` (text-area px) answers added for the same reason over ssh.
- **APC parser**: `ESC _` -> PS_APC with a growable buffer + a cap (garbage stream cannot OOM);
  `ESC P` -> PS_DCS discarded, so sixel output no longer leaks into the grid as text.

DONE so far:
- `include/kai-term/kai-term.h`: added `KImage`, `ImgTex`, `ImgView`, `KittyTx`; `PS_APC`/
  `PS_DCS`; Parser fields `apc/apc_len/apc_cap/apc_drop/apc_kind/tx`; `Grid.row_base`;
  Term fields `images/image_bytes/image_serial/image_last_id/cell_w/cell_h`; Pty fields
  `cell_w/cell_h` + `pty_set_cells()`; Render fields `u_img/imgtex/nimgtex/imgtex_cap`;
  prototypes `term_set_cell_px/term_visible_images/term_image_count`.
- `src/pty.c`: `pty_setsize()` fills `ws_xpixel/ws_ypixel` from `Pty.cell_w/cell_h`; new
  `pty_set_cells()` stores them and re-issues `TIOCSWINSZ` (zoom changes the cell without
  changing cols x rows).
- `src/vt.c`: graphics section (base64, zlib-inflate, PNG via libpng simplified API, raw f=24/32,
  premultiply; image list with doc-row anchors, span math, prune, byte budget, delete, place;
  control-block parser; APC/DCS parser states; `CSI 14 t`/`CSI 16 t` answers; `term_respond()`
  replacing the `TERM_RESP` macro body; `term_init`/`term_destroy` free images + parser buffers
  behind the `GRID_VALID` guard; `row_base` maintained in `grid_init`, `scroll_up`,
  `grid_resize_rows`, `grid_reflow`, `erase_scrollback`; `image_prune()` at the end of
  `term_parse`).
- `Makefile`: `libpng zlib` added to `PKG` (pkg-config gives `-I/usr/include/libpng16 -lpng16 -lz`).

MEASURED while wiring this (three protocol facts the captured bytes corrected):
1. **timg separates the control keys with `,` and the payload with `;`**, not `:` as the protocol
   documents: `\033_Ga=T,i=%u,q=2,f=100,m=%d` + `;` + base64 (format strings found in the timg
   binary at offset 261549: `Ga=T,i=%u,q=2,f=100,m=%d`, `,U=1,c=%d,r=%d`, `Gq=2,m=%d;`). The first
   implementation split only at `:` and reported "control block too long (4128 bytes)". Now the
   control block is walked as `letter=value` elements separated by `;` or `,` and the payload is
   whatever follows the last one (base64 contains none of `:`, `;`, `,`).
2. **The APC kind byte must not be part of the payload**: buffering `G` from `ESC _ G` made the
   whole string look like one non-control element, so every chunk was dropped ("APC chunk with no
   action"). `ESC _` now consumes the kind byte separately.
3. timg never emits `a=d` (no `a=d` string in its binary), so image lifetime is entirely the
   terminal's job — the doc-row prune + byte budget are not optional cleanup, they are the only
   thing that reclaims memory.

VERIFIED headless (`.scratch/s68/gfx_probe.c`, links src/vt.c + src/config.c and feeds the
CAPTURED timg bytes through term_parse; cell px 8x16, grid 24x80):
- `k_raw.ans` (single chunk): 1 image, 64x40 px, span 8x3 cells, cursor ends at (0,4) = 3 rows of
  image + timg's own CR LF, first texel `01 01 81 ff` (premultiplied) — PNG decode works.
- `big_k0.ans` (two chunks, `m=1` then `m=0`): 240x180 px image, span 30x12 — chunk assembly works.
- `grid.ans` (4 images): 6 images total, 5 visible, one at crow=-6 (partly above the viewport,
  still drawn) — placement and z/arrival ordering work.
- After 40 more lines: `row_base` unchanged (scrollback had room), `scroll_off` 51, `visible=0`
  while all 6 images are still retained — i.e. the picture scrolls out with its text and stays in
  scrollback instead of being dropped early.
`make` after the vt.c edits: exit 0, 0 warnings.

STILL TO DO: render.c (u_img sampler + a_glyph 3, two batches, per-image texture cache), main.c
(push cell px into Term and Pty), tests/test_vt.c checks, README/ISSUES, then a live run for the
user to confirm by eye.

s69 DONE (renderer half, all four files compiled clean — `make` exit 0, 0 warnings):
- `src/render.c`: `u_img` sampler (texture unit 2) + `a_glyph > 2.5` branch in `fs_src` (same
  premultiplied composite as the color-emoji path, so still no GL blending); `img_tex()` cache of one
  GL texture per `KImage` keyed by `serial`, re-uploaded when `version`/`w`/`h` change, LINEAR +
  CLAMP_TO_EDGE (NPOT is legal without mipmaps on GL 2.1); `img_tex_sweep()` drops textures no image
  claimed this frame; `draw_quads()` (one batch = one upload + one draw, instanced or expanded);
  `setup_draw_state()` (viewport/program/3 samplers/selection colour/mvp, called before each batch);
  `draw_images()` per pane. `render_draw` now: emit all three passes into ONE staging buffer, record
  `nq_bg` after pass 1 + bar chrome, then draw batch A `[0,nq_bg)` -> pictures -> batch B
  `[nq_bg,nquads)`. `glClear` stays once per frame (clearing between batches would erase batch A).
  `render_destroy` deletes the image textures while the context is still current, then frees the array.
  Measured: the two-batch split needed NO duplication of the pass 2/3 emission code — the records are
  fixed-size, so a batch is just a range of the same buffer (an earlier draft of this edit duplicated
  ~200 lines; rejected before it was applied).
- `include/kai-term/kai-term.h`: `Render.img_frame` (the `ImgTex.seen` stamp); `render_draw` comment
  no longer claims one draw call.
- `src/vt.c`: `image_span_rows/cols` now derive the missing dimension from the picture's aspect ratio
  when only one of `c=`/`r=` was given. Reason found while writing the renderer: the span is also what
  `a=T` moves the cursor past and what `image_prune()` measures, so if the renderer scaled one way and
  the model another, the prompt would land on the wrong row. Integer ceil, no math.h.
- `src/main.c`: `layout_apply()` pushes `font.cell_w/cell_h` into every session with
  `term_set_cell_px()` + `pty_set_cells()` in its own loop BEFORE the resize loop — a zoom can change
  the cell without changing cols x rows, so the existing `if (cols == ... ) continue;` would have
  skipped the PTY update.

STILL TO DO (s69): tests/test_vt.c graphics checks, README (feature + libpng/zlib in Requirements),
ISSUES.md limitations, then `make clean && make` + `make test` + `make asan` and a live `timg` run for
the user to confirm by eye.

s69 DONE (tests + three defects the tests found). `tests/test_vt.c` gained a graphics section (39 new
checks, 501 -> 540) with its own base64 ENCODER (the decoder under test is static in vt.c, so a bug in
one cannot hide a bug in the other), a `gfx_feed()` that emits timg's exact chunk shape, and two
embedded byte arrays: an 8x8 RGBA PNG and the same PNG inside a zlib stream (generated with
python3/zlib, so the test needs no image file). Covered: a=T creates + marks shown + moves the cursor;
span from px; premultiply (2 checks); m=1/m=0 chunking incl. the continuation chunk's bytes; a=t not
drawn until a=p; q=1 OK response; PNG and zlib-PNG decode; `c=` without `r=`; z ordering incl. a
negative z; a=d by id and d=A for all; scroll out -> kept, scroll up -> same history row, eviction ->
freed; CSI 3 J drops anchored pictures; reflow keeps the picture on the row after its line; DCS and
non-graphics APC discarded and the parser recovers; `CSI 16 t`/`CSI 14 t` answers and no answer when
the cell size is unknown. The last check leaves 2 images alive so `term_destroy` frees them under
LeakSanitizer.

THREE DEFECTS FOUND BY THE NEW TESTS (all in the s68 code, none visible in the s68 probe):
1. `premultiply()` used `(x*a + 255)/255`: for x=255,a=255 that is 256 and the `(uint8_t)` cast made a
   fully opaque WHITE pixel BLACK. Now `+128` (proper rounding; opaque white stays white, 255x128 ->
   128). The probe's sample texel was `01 01 81 ff` — never white — which is why it passed.
2. `b64_decode()` accumulated into a plain `int` without masking the emitted bits, so by the 5th
   character `acc << 6` overflowed int — UBSan reported it (`src/vt.c:1705: runtime error: left shift
   of 1071648831 by 6 places`) on the first real payload, not on the probe's short one. Now `unsigned
   acc` masked after every emitted byte.
3. `kitty_ctl_parse()` rejected any value with a leading '-', so `z=-5` silently became z=0 and the
   z-ordering test would have failed. Now a leading '-' is part of the number.

ALSO s69: `decode_raw()` no longer assumes what `s=` means. The kitty spec says s= is the BYTE size of
the raw data (width = s/(r*bpp)); s68 assumed it was the WIDTH (payload = s*r*bpp bytes). Nothing on
this box can settle it — timg only ever emits PNG — so the payload length now decides which reading
fits, and both are accepted. UNVERIFIED against a real raw-format client: recorded as a limitation.

VERIFIED (exact commands, this session): `make` exit 0, 0 warnings; `make test` -> 540 checks 0
failures + MODEL OK (0 problems) + CLI 86 checks 0 failures 0 skipped; `make asan` -> 540/0 + MODEL OK,
0 lines matching AddressSanitizer|runtime error|LeakSanitizer|SUMMARY|ERROR.

(Previous task, closed in s67: both source loose ends done — see the s67 journal entry.)

- 6 dangling `ISSUES.md` citations removed from kai-term.h / vt.c / dbg.c / test_vt.c / main.c; the comments
  state the mechanism themselves now. `grep -rn ISSUES src/ include/ tests/` finds no dead pointer.
- Emoji-strike comments in font.c + render.c now attribute 136x128 / 128px to the installed font, and the
  hard-coded `color_px = 128` fallback is `f->px`. **The premise that these were stale was false**: the strike
  really is 136x128 px and `color_px == 128` here (FreeType probe + the app's own `-d` line). The bad "56px"
  figure came from reading CBLC's `bitmapSize` (a byte count) as a pixel size; ENVIRONMENT.md carried it and
  has been corrected.

NEXT ACTION: pick from "Next Steps" (housekeeping — no bug is open, image support is confirmed), or file a
problem with a reproduction.

## Progress Journal (root causes of fixed bugs: git history)

- **2026-10-12/14, s1-12** — scaffold; first build. vt.c grid -> rotation permutation (`row[]` always valid);
  CSI params; event loop -> `prepare_read -> epoll_wait -> read_events`; EGL/FreeType init; render fixes
  (vertex stride, MVP mapping, 6-vertex quads, atlas upload format, first-frame bootstrap, clearing
  realloc-grown cols). First 2 user bugs: black window (font metrics in font units, not px); 10 s startup
  (unanswered DA1/CPR/DA2).
- **2026-10-14, s13-23** — HW instancing; alt screen; SGR mouse; scrollback keyboard nav; zoom; selection +
  Wayland clipboard; wheel scroll (seat v5); `Grid.live_off` anchor (fixed wheel direction + auto-follow).
  Tree -> project template; Makefile rewritten; `.gitignore` + stricter warnings; docs rewritten.
- **2026-10-14, s24-32** — selection precision: sub-cell endpoints; per-fragment highlight without GL
  blending; selection-vs-pointer causes: grid anchoring, `wl_fixed_to_int`. User confirmed.
- **2026-10-14, s33-38** — doc audit; fontconfig fallback faces with source face stored per glyph (so
  `atlas_grow` re-renders from right one); 3 back-to-front passes; COLOR emoji end to end (clusters in
  vt.c, ONE glyph id via HarfBuzz, FreeType color bitmap, RGBA8 atlas, un-tinted, wide emoji = 2 cells,
  regional-indicator flag pairs, U+FE0F/U+20E3 presentation).
- **2026-10-14, s39** — CSI 3 J so `clear` drops scrollback (`clear_scrollback` gate); shell start dir
  (`-w DIR` > config `working_dir` > `$HOME`, tilde expansion, `stat` check).
- **2026-10-15, s40** — bracketed paste (DEC 2004, `paste_wrap()` as tested reference, buffered, <=4096 B
  chunks, 8 MiB cap, 400 ms idle watchdog); latent `term_init` free-of-uninitialized-grid bug -> `GRID_VALID`.
- **2026-10-15, s41-43** — `app_id`; real window `opacity` (premultiplied source-over); selection endpoints
  snapped to whole cells; no band over never-written cells; **REFLOW** on width change (`Grid.wrap` soft-wrap
  marks, `grid_reflow()`, `grid_resize_plain()` for alt screen, cursor travels with its logical cell,
  narrowing evicts oldest rows).
- **2026-10-15, s44-45** — CLIENT-SIDE KEY REPEAT (`krfd` timerfd, `send_key` repeatability,
  `repeat_delay`/`repeat_rate`), user-confirmed; CLI cut back; `copy_on_release` (default off);
  fullscreen-freeze cause found (frame callback armed on an empty commit).
- **2026-10-15, s46-47** — draw/commit cycle rewritten -> 3 frame-pacing rules. Pre-existing segfault on
  heavy output in a grown window: `sb_limit() = min(scrollback_max, rcap - rows)` as single bound. Both
  user-confirmed.
- **2026-10-15, s48-49** — docs/state audit; "K-Term has its own clipboard" investigated against a foreign
  client, NOT reproduced (likely intentional `copy_on_release = 0`). No source change.
- **2026-10-15, s50** — GitHub; dead code removed; dangling `NextFeature #N` refs deleted; `WARN` tightened
  (tree clean under it); `.gitignore` replaced (had ignored neither `bin/` nor `build/`).
- **2026-10-15, s51** — CLI: config flag `-c` (was `-C`); retired-flag messaging deleted; one generic
  unknown-option message.
- **2026-10-15, s52-55** — `tests/cli_test.sh` + `make test-cli` (real binary — `main()` is in no linked
  test); zoom stale-prompt cause in `grid_resize_rows`; WINDOW PADDING (`padding` key + single
  `term_grid_geometry()` deciding cols/rows AND origin), which exposed `DEF_BG` clear-colour bug, fixed
  same session. Padding + zoom user-confirmed.
- **2026-10-15/16, s56-58** — MULTIPLEXING (`src/mux.c`: N `{Term, Pty}` per window, config-driven key
  bindings) + TAB BAR + SIDE-BY-SIDE VIEW (`src/layout.c`: pure geometry). s58 reported a fullscreen-freeze
  regression.
- **2026-10-16, s59** — RENAME `K-Term` -> `Kai-Term` (dir, header, binary, config path,
  `DEF_APP_ID`/`DEF_TITLE`, log prefix, `TERM_PROGRAM`, Makefile, tests pinning those literals) vs a
  pre-rename baseline. 4 items OUTSIDE repo listed for user (niri window rule,
  `/usr/local/bin/k-term`, `/usr/local/bin/desktop-terminal` + its .desktop entry, GitHub repo name): repo
  rename done, other 3 pending when last checked. Fullscreen freeze returned intermittently; user deferred
  (then ISSUES.md Open #4).
- **2026-10-16, s60-62** — s56/57 finished: main.c wired to `layout_apply()` + `render_init(..., conf.fg)` +
  `render_draw(&rend, &font, &g_layout, terms)` (this made binary link again); live key/pointer-injection
  verification; 3 defects fixed — bar/pane click logged a switch it never made (`tab_switch()` never called
  `mux_goto()`), `BTN_RIGHT`/`BTN_MIDDLE` swapped in SGR mapping, `tab_bar = always` asked compositor
  for `cell_w` instead of `cell_h` of extra height (startup grid 1 row short; arithmetic ->
  `layout_initial_size()` so a check pins it). Tests 436 -> 501; pushed `d05c817`.
- **2026-10-16, s63 — docs cleanup.** AGENT.md had grown to ~2070 lines, mostly per-session detail
  duplicating era journal + ISSUES.md root causes; `docs/ARCHITECTURE.md` mixed project design with host
  facts, which must not ship inside project. Created `/home/kaida/Project/ENVIRONMENT.md` outside repo
  (host/toolchain/library/tool inventory + every debug technique that works here + traps; project-agnostic;
  name matches content). `docs/ARCHITECTURE.md` deleted (`git rm`, path git-ignored so it cannot return);
  invariants -> "Architecture & Invariants", debug entry points -> "Debugging this project". AGENT.md
  2067 -> 491 lines (189 KB -> 42 KB). README + ISSUES refs and `.gitignore` updated. Committed `3351214`;
  pushed in s64.
- **2026-10-17, s64 — user confirmation + issue clear-out + push.** User viewed tab bar + side-by-side
  view: "it works as expected and looks great" -> multiplexing confirmed by eye, not only by log lines. Asked
  to clear 4 ISSUES.md "Open" items (never reproduced here, not established Kai-Term defects):
  `## Open` (62 lines) -> `## Cleared (2026-10-17)`, one line each, kept as unproven rather than moved to
  "Fixed", `Open #1`-`#4` labels kept so dated entries resolve; 705 -> 673 lines. Dangling cross-refs fixed
  ("Fixed: Fullscreen" entry, Current Status, Next Steps, file tree, README Documentation line). Committed
  `dbcc597`, `a2a5a54`, `1ab9dc8`; `git fetch` first (0 behind -> fast-forward push).
- **2026-10-17, s65 — AGENT.md refactor, 4 passes.** Measured first: 526 lines / 45.8 KB; sentence-level diff
  against ISSUES.md -> only 3 near-duplicate pairs, so size was density, not copy-paste from ISSUES.md.
  P1 structure (`867d50e`): 10 prose walls -> bulleted invariants per module; project facts (deps, naming,
  layout, license, git) moved Key Decisions -> "Project Files & Facts", leaving Key Decisions as bug traps +
  working rules; journal eras merged; s63/s64 cut from blow-by-blow to era level (their host inventory dropped
  only after `pkg-config --modversion` re-checks put harfbuzz/freetype2/fontconfig versions into
  ENVIRONMENT.md); 3 rules were stated twice (once as invariant, once as pitfall) -> merged into the
  invariant, which gained 2 failure modes it lacked (reflow traps; `GRID_VALID` + `32511` measurement;
  empty-commit frame callback + `solicit_frame()` deletion note).
  P2 words: telegraphic style — connectives + narrative framing removed; `->`/`=` for causality; one fact per
  line.
  P3: journal + per-file descriptions cut to minimum; deps/license trimmed to what README does not already
  carry (README "Requirements"/"License").
  P4: mechanical article strip — `the ` removed outside code spans, quoted log strings and fenced blocks
  (152 lines changed), indentation preserved; verified protected strings intact (`the pty of tab i/n closed`,
  `the last session is gone, exiting`); one line left reading badly was rewritten by hand.
  RESULT: 526 -> 469 lines, 45.8 -> 37.2 KB (-18.7%), 6679 -> 5232 words (-21.7%). Line count is a poor proxy
  for context cost — bytes/words are what matter.
  Fact check vs pre-refactor text: 174 identifiers, 5 absent — 3 live in ISSUES.md/ENVIRONMENT.md, 2 are dead
  `/tmp` log paths; 0 project facts lost. Numbers dropped are refactor bookkeeping only. Docs only — no
  source, header, Makefile or test file touched; `make`/`make test`/`make asan` re-run from a clean tree after
  each pass; numbers in Current Status are those runs. A `DESIGN.md` split (Architecture + Debugging are ~40%
  of file) was considered and NOT done — user chose word-level compression.
- **2026-10-17, s66 — ISSUES.md gutted to current + verified info only.** User directive: keep only
  up-to-date relevant info; the file should be nearly empty unless a real verified problem exists.
  VERIFIED FIRST from a clean tree: `make` exit 0 / 0 warnings; `make test` 501/0 + MODEL OK + CLI 86/0/0;
  `make asan` 501/0 + MODEL OK, 0 sanitizer reports; `grep TODO|FIXME|XXX|HACK` over src/ include/ tests/ ->
  only a test payload string. So no verified open problem exists.
  BEFORE: 673 lines / 9059 words — 78.9% of words in 54 historical "Fixed" root-cause entries, 15.2% Known
  limitations, 4.0% the 4 unproven "Cleared" reports.
  AFTER: 100 lines / 1149 words. `## Open` = none, with the verification evidence and the rule for filing
  (symptom + exact reproduction + what `-d` logs; not reproducible -> not filed). `## Known limitations` =
  all 19 limitations kept, grouped Grid/scrollback, Selection/clipboard, Sessions/layout, Rendering/input,
  each re-checked against the source rather than trusted: `rcap` fixed at startup (vt.c:255) and
  `sb_limit()` (vt.c:245); `term_select_clear` on scroll (main.c:307) and resize (vt.c:728);
  `copy_on_release = 0` default (config.c:48); `CLUSTER_MAX 8` (header:34); color atlas 1024 -> 8192
  (font.c:22,24); only `text/plain` offered (main.c:1379) and no `wl_data_offer_accept` call anywhere;
  window title from `mux_title()` (main.c:451-452) with OSC title kept in `t->title` (vt.c:1615); alt screen
  via `grid_resize_plain()` (vt.c:572,713); floor-division remainder (vt.c:689-693); min size 200x100
  (main.c:1598); `PASTE_BUF_MAX` 8 MiB sent raw on overflow (main.c:155,1980,1403); DEC 2004 gate
  (vt.c:1397,1414); `repeat_info` logged but deliberately unused (main.c:160-164,1448); `split_max` clamp +
  `*` mark in `bar_label` (layout.c:52,67).
  DROPPED: the 54 "Fixed" entries and the 4 "Cleared" reports — history, not current info, and neither was a
  verified current problem. Recoverable: `git show d5afe4a:ISSUES.md`. The rules those bugs produced stay in
  "Architecture & Invariants", which is what an agent should read.
  Dangling refs fixed: AGENT.md preamble, journal header, Architecture header, Next Steps #1, Project Files
  tree, Current Status; README Documentation line. Historical journal entries that mention the old sections
  were left alone — they record what was really done.
  STALE FACT FOUND while re-checking: ISSUES.md and `src/font.c` comments (529, 794) claimed a single
  **128px** CBDT strike for the installed NotoColorEmoji.ttf. Reading the CBLC size table directly: exactly
  one strike, `numSizes=1`, `strikeSize=56`; `fc-scan` gives `pixelsize=109`, `unitsPerEm=2048`. Behaviour
  (bitmap-only -> one strike -> zoom scales, never re-rasterizes) is real; the px number is a host fact, so
  ISSUES.md now states the behaviour with no hard-coded number and the measurement went to ENVIRONMENT.md
  ("Fonts", outside the repo). The two stale `font.c` comments + the 128 fallback constant (font.c:574) are
  a source change — left for the user to approve.
  **CORRECTED IN s67 — the 56px figure above is wrong.** It came from reading CBLC's `bitmapSize` (a BYTE
  count) as a pixel size. FreeType and the running app both report the strike as 136x128 px /
  `color_px = 128`, so font.c was right and ENVIRONMENT.md was polluted with the bad number.
  Docs only — no source, header, Makefile or test file touched. `make`/`make test`/`make asan` re-run from a
  clean tree afterwards; numbers in Current Status are those runs.

- **2026-10-17, s66b — s66 work committed, verification re-run independently.** `git status` showed AGENT.md,
  ISSUES.md and README.md modified but uncommitted (the s66 work above); that was the outstanding commit.
  Re-ran the verification myself from a clean tree instead of trusting the entry: `make clean && make` exit 0
  with 0 warnings; `make test` -> 501 checks 0 failures + MODEL OK + CLI 86/0/0; `make asan` -> 501/0 +
  MODEL OK, no sanitizer report; probe grep empty; only `TODO|FIXME|XXX|HACK` hit is the `XXXXXXXXXX` payload
  at tests/test_vt.c:667. Counts the entry claims also confirmed: `git show d5afe4a:ISSUES.md` = 673 lines
  with 54 `## Fixed` entries and 4 `Cleared` items; new ISSUES.md = 100 lines / 1149 words with 19 limitation
  bullets (19 before -> 19 after, none lost).
  Committed `a59e7ae` (3 files, +148/-686). NOT pushed — user asked for the commit only; `main` is 3 ahead.
  NEW FINDING while checking cross-refs: 6 code comments still cite ISSUES.md sections this commit deleted
  (listed under Current Task). s66's "dangling refs fixed" covered the docs, which is what it claimed, but the
  code side is still dangling — a source change, so left for the user.
  Dating note: `date` on this host reports 2026-10-16 while sessions 64-66 are dated 2026-10-17 — journal
  convention and the host clock disagree. Keep the journal's convention; do not rewrite past entries.
- **2026-10-17, s67 — objective: clear the two source loose ends, then make origin/main match local main.**
  User approved both source fixes (font.c emoji-stale claims + the 6 dangling ISSUES.md refs in code) and
  asked for the remote to fit the local tree (= push, `git fetch` first).
  MEASURED BEFORE TOUCHING CODE — and the first loose end turned out to be based on a FALSE premise:
  `.scratch/s67/facts.c` (FreeType, the API the code actually uses) on
  `/usr/share/fonts/noto-emoji/NotoColorEmoji.ttf`: `FT_IS_SCALABLE=0`, `num_fixed_sizes=1`,
  `available_sizes[0] = 136x128`, after `FT_Select_Size(0)`: `metrics.height/64 = 128`, `y_ppem = 109`,
  `unitsPerEm = 0`; loaded color bitmaps really are 136x128 px (U+1F600 gid 883, U+1F1FA also 136x128).
  The app agrees: `./bin/kai-term -d -e true` logs `color emoji: … (128px strike)`, i.e. `f->color_px == 128`.
  So font.c's "128px strike" / "136x128 px per glyph" comments and its 128 fallback are CORRECT for this
  box, and the s66 entry's "single 56px strike" is WRONG. Where 56 comes from: a hand-rolled CBLC parse read
  a **byte-count** field as a pixel size. Re-parsing CBLC properly (header is 8 bytes: major 3, minor 0,
  numSizes 1; BitmapSize record at +8) gives `bitmapSize = 56` BYTES of index data, `bitmapOffset = 16000`,
  `indexFormat = 3` — 56 is a size in bytes, never a px dimension. Evidence: `.scratch/s67/facts.c`.
  CONSEQUENCE: the stale host fact is in ENVIRONMENT.md ("Fonts", outside the repo) and in the s66 journal
  entry, not in font.c. Both get corrected; font.c keeps its numbers but stops asserting them as constants.
  PLANNED STEPS: (1) font.c — attribute the px figures to the installed font + replace the magic 128 fallback
  with `f->px` (font_size is clamped 6..256 at config.c:288, so it is always > 0; the branch is unreachable
  here since metrics.height/64 = 128 and y_ppem = 109 both > 0). (2) render.c:761 — same host-specific
  "128px on this box" claim. (3) the 6 dangling ISSUES.md citations in kai-term.h/vt.c/dbg.c/test_vt.c/main.c
  -> comments state the mechanism (they already do) and stop pointing at deleted sections. (4) ENVIRONMENT.md
  Fonts section corrected. (5) `make`/`make test`/`make asan` from a clean tree + the `-d` emoji line re-read.
  (6) commit, `git fetch`, push so origin/main == local main.
  EXPECTED OUTCOME: no behaviour change (comments + one unreachable-branch fallback), 0 warnings, same test
  counts, and a remote that matches the local tree.
  DONE (1) src/font.c: 4 comment sites now attribute the figures to the installed font instead of stating
  them as properties of "Noto Color Emoji" (18, 528-530, 568-571, 796-800), and the magic `f->color_px = 128`
  fallback became `f->px` with the reason written next to it (color_px is the `cell_h / color_px` divisor, so
  it must stay > 0; font_size is clamped 6..256 at config.c:288, so f->px is; measured unreachable here).
  Mistake made and fixed in-session: the font.c edit first produced `(int)face->size->metrics.y_ppem);` — a
  stray paren — caught by compiling right after the edit (`make` exit 0, 0 warnings afterwards).
  DONE (2) include/kai-term/kai-term.h:192 — dead `(ISSUES.md #4)` citation dropped; the comment already
  states the mechanism, so it now ends `(the zoom leftover-lines bug)`.
  (Second in-session slip: this very append replaced the paragraph above instead of following it — the journal
  edit anchored on the wrong line. Re-added. Anchor on the LAST line of the entry, not on a line you mean to
  keep above it.)
  DONE (3) src/render.c:761 — "(128px strike on this box)" -> the strike's own pixel height as the font
  reports it, 128 named as the installed font's value.
  DONE (4) the 6 dangling ISSUES.md citations removed: kai-term.h:192, src/vt.c:494, tests/dbg.c:158,
  tests/test_vt.c:438 (all four just drop the dead `ISSUES.md #4` tag — each comment already states the
  mechanism, so the citation added nothing), src/main.c:187 and src/main.c:647 (now say the root causes are in
  git history, not in ISSUES.md). No git SHAs put in source comments — they age; the mechanism is stated
  inline instead.
  CHECKED after: `grep -rn ISSUES src/ include/ tests/ Makefile` -> only the two new "not in ISSUES.md"
  sentences; `grep -n 128 src/font.c src/render.c` -> every hit attributed to the installed font.
  DONE (5) `/home/kaida/Project/ENVIRONMENT.md` "Fonts" (outside the repo) rewritten with the measured
  numbers and a new trap entry: do not parse CBLC by hand, `bitmapSize` is a byte count; read the font through
  FreeType or read what the program logs. This is where the false 56px fact lived and it is what would have
  re-infected the next session.
  VERIFIED from a clean tree (`make clean` first), exact commands and results:
  `make` -> exit 0, 0 warnings, 0 errors; `make test` -> exit 0, 501 checks 0 failures + MODEL OK (0 problems)
  + CLI: 86 checks 0 failures 0 skipped; `make asan` -> exit 0, 501/0 + MODEL OK, 0 lines matching
  AddressSanitizer|runtime error|LeakSanitizer|SUMMARY. Behaviour unchanged, and proven rather than assumed:
  `timeout 6 ./bin/kai-term -d -e true` still logs `color emoji: … (128px strike)` with 0 warn/error lines,
  so the primary metrics path is taken and the rewritten fallback is not reached on this box.
  DONE (6) remote synced, on request ("make sure the git version fits with the local version"). `git fetch
  origin` first: 0 behind / 6 ahead, nothing on GitHub to integrate -> fast-forward `git push origin main`
  pushed `1ab9dc8..eb7ae75` (exit 0). Re-fetched and verified: `main` == `origin/main` == `eb7ae75`,
  0 behind / 0 ahead, `git diff main origin/main` empty. The journal commit recording this push is itself
  pushed, so the two stay equal.

- **2026-10-17, s70 — image support finished: docs, three defects found by testing the docs.** Started from
  the s69 handoff (s68+s69 code in the tree, uncommitted; README/ISSUES untouched). Re-verified the tree
  myself first (clean `make` 0 warnings; `make test` 542/0 + MODEL OK + CLI 86/0/0; `make asan` 542/0 +
  MODEL OK, no reports) instead of trusting the s69 numbers.
  **The README draft was the thing that caught the bugs.** Writing an honest example meant proving what
  `timg` really does against Kai-Term, and the s68 "auto-detect picks kitty when the PTY reports pixel
  geometry" claim turned out to be false (the s68 captures came from forced `-p k` runs). Measured with
  `.scratch/s70/{capture,gate}.py` + strings of `/usr/bin/timg`: timg probes `ESC[>q ESC[5n`, then `ESC[c`,
  then `ESC]11;?`; it chooses kitty ONLY for `TERM=xterm-kitty|xterm-ghostty` plus non-zero pixel geometry,
  and it chooses SIXEL whenever the DA1 answer contains `;4` — which Kai-Term's answer did. Combined with
  s68's DCS discard, `timg foo.png` emitted sixel that was thrown away: no picture at all, worse than the
  pre-image quarter blocks. Fixed by dropping the graphics capability bits (4/16/18) from the DA1 answer;
  A/B against a copy of vt.c with the old string proves it (old: `Using sixel graphics`, 8015 bytes of DCS,
  model images=0; new: `Using quarter block`, 26620 bytes of blocks). Also answered `CSI 5 n` with `ESC[0n`
  (never answered; it is half of timg's first probe). Feeding the real `timg -p k -V anim.gif` bytes through
  the model showed a third defect: timg reuses two ids across four frames and `kitty_complete()` appended a
  new image every time, so an animation left a trail — a transmission to an existing id now replaces that
  image (4 pictures -> 2, `version` bumped). Tests 542 -> 553, all green, `make asan` clean; README gained a
  verified "Images" section (libpng/zlib in Requirements, `timg -p k`, no false auto-detect claim) and
  ISSUES.md a "Graphics / images" limitations group. Live log evidence the renderer path runs:
  `graphics: 1 image(s) drawn, 1 texture(s) held`, no GL warning. Visual confirmation was still pending
  then (closed below).
  Rule this session produced: **a documented example is a claim — run it before writing it.** Two of the
  three README examples I drafted were wrong (`--grid=RxC` arranges multiple FILES, not tiles of one image;
  `-V` does not animate in place because timg moves the cursor per frame).
  CLOSED by the user's own verdict in their window: "The image display properly. The feature works as
  expected" — image display is now user-confirmed by eye, joining the other visually confirmed features in
  Current Status. AGENT.md Current Status / Current Task / Next Steps and the ISSUES.md "Open" note were
  updated to say confirmed rather than pending; no source change, so no rebuild was needed (the tree that
  was verified in s70 is the tree that is on `main`).

## Next Steps

1. **File manager: the one open item is the user's eye.** Built, wired into the window, documented and
   green (220 model/layout/config checks, plus the s74 live key-injection run that proves the routing and
   the `cd` follow), but no human has seen the band yet. Hand the user a live run
   (`./bin/kai-term -d`, then `Ctrl+Shift+F`) and record their verdict — this project's rule for anything
   visual. If something looks wrong, start from the `-d` lines `fm: opened on …`,
   `fm: listing <dir> (request N)`, `fm: dropping stale …`, `fm: keyboard -> …` and
   `graphics: … + the file-manager preview`, not from the shader. Two decisions the user may want to
   revisit, both flagged and both one-line changes with different safety implications:
   `Return` on a file types its path instead of running it, and `fm_follow` sends `cd <path>` unprompted.
2. **Image support is done and confirmed by eye** (2026-10-17), and the user has settled its scope:
   "The goal was image support, Not auto-detection by timg so it work fine using `-p k`". So `-p k` IS the
   supported way to show a picture and nothing more is wanted here. Do NOT "improve" this by re-adding the
   graphics bits (4/16/18) to the DA1 answer or by inventing a `TERM` value: measured A/B (s70), advertising
   sixel without implementing it makes the picture vanish entirely, and timg's auto-detect list is hardcoded
   in its binary. Sixel/iTerm2 support would be a NEW feature — only on an explicit request.
3. **No open bugs.** ISSUES.md "Open" is empty. If a dropped report recurs, file it with a reproduction and
   start from scratch — for second-instance crash: source + headless harness, not screen. Old wording of
   those 4 reports: `git show d5afe4a:ISSUES.md`.
4. Housekeeping, none a bug:
   - ASan build of `kai-term` binary itself as a real Makefile target (only tests have one). The
     hand-built one covered mux/layout and found `row[rcap]` overrun; covering Wayland/EGL would pay off
     (also covers heap-grown `paste_buf`), and it is the only thing that would exercise `fm_shutdown()`
     under LeakSanitizer.
   - The file manager's input routing lives in `main.c`, which is in no test binary, so it is covered only
     by the injection harness in `.scratch/s74/`. Extracting the key-routing table into a testable unit
     would close that, but it is a refactor of the file the three s74 bugs were in — do it deliberately,
     not as a side effect.
   - Animated WebP previews are refused by libwebp's simple decode (measured, s74). Taking the first frame
     would need `libwebpdemux` — a new dependency, so only on an explicit request.
   - Idle-frame optimization: stop frame cycle when nothing is dirty — `redraw()` now calls
     `layout_apply()` every frame, so that path is hotter than before.
   - Color atlas re-uploaded whole with `glTexImage2D` on every new shaped emoji; `glTexSubImage2D` per new
     shelf would be cheaper.
   - Reflow allocates a fresh row buffer per produced line — fine at terminal sizes, but a huge-width window
     with huge scrollback churns allocator.
   - Window title is `mux_title()`'s own text; an app's OSC title only reaches tab bar label. Showing the
     active session's OSC title in window title is a one-line change in `update_title()`, but it changes
     visible behaviour — ask first.
5. Clipboard hardening niri does NOT require (do not do unasked — untested against a compositor that needs
   it): `wl_data_offer_accept()` before `wl_data_offer_receive()`; offer `UTF8_STRING` +
   `text/plain;charset=utf-8` next to `text/plain`.
6. Emoji paths that exist but have NEVER run here: SCALABLE color font branch (`FT_IS_SCALABLE` ->
   `FT_Set_Pixel_Sizes` + `hb_ft_font_changed` on zoom) — this box has only a bitmap-only Noto CBDT font; and a
   cluster HarfBuzz renders as >1 glyph, deliberately rejected (`ng == 1`), falls back to mono.
7. Scratch cleanup: throwaway probes live in `/home/kaida/Project/.scratch/` (outside project). Tab bar /
   side-by-side is confirmed, so `s61/` + `s61_apinject*` need no longer be kept; keep `s45_old/` +
   `s47_before/` as A/B trees in case another draw-path regression is suspected. `s70/timg_gate.c` is the
   reusable end-to-end image harness — keep it. `s74/inj_keys.sh` + `s74/inj_test.sh` are the file-manager
   input harness and `s74/animprobe.c` the decoder probe — keep those until the feature is confirmed, then
   keep `inj_keys.sh` as the regression harness for the routing gate. Ask before deleting anything outside
   repo — not done.
8. Git: keep committing verified work; `git fetch` before pushing.

## Project Files & Facts

```
Kai-Term/
├── .gitignore                   # bin/, build/, *.o/*.obj/*.d/*.so/*.a, editor leftovers, docs/ARCHITECTURE.md
├── LICENSE                      # MIT, "Copyright (c) 2026 Kaida"
├── AGENT.md                     # this journal
├── ISSUES.md                    # open problems (none now) + known limitations
├── README.md                    # features, deps, build, flags, config keys, shortcuts
├── Makefile                     # src/*.c -> build/*.o -> bin/kai-term; all/test/test-cli/asan/run/clean
├── include/kai-term/kai-term.h  # the single shared header: all types, prototypes, log levels, helpers
├── src/                         # config.c vt.c img.c mux.c layout.c fm.c pty.c font.c render.c main.c
├── tests/test_vt.c              # 613 checks (see below)
├── tests/test_fm.c              # 220 checks: the file-manager model, previews, decoders, band geometry
├── tests/dbg.c                  # grid-model invariant checker (permutation + scroll_off/live_off bounds)
├── tests/cli_test.sh            # 92 checks driving the REAL bin/kai-term (argv + config keys + log lines)
├── bin/                         # kai-term, test_vt, dbg (+ *_asan)          [generated, git-ignored]
└── build/                       # *.o + generated/xdg-shell-*                [generated, git-ignored]
```

- `src/vt.c` — unit-tested core: VT/ANSI/UTF-8 parser (incl. the APC/DCS states the kitty graphics
  protocol needs), grid model, the image list (decode + placement + lifetime), selection text
  extraction, alt screen, `term_grid_geometry()`, evdev -> SGR button mapping (`mouse_sgr_button`).
  No Wayland/GL dependency — the decoded PIXELS live here, so the whole protocol is testable headless.
- `src/img.c` — every image decoder in one place: PNG (libpng), JPEG (libjpeg), WebP (libwebp) and GIF
  (own LZW decoder, because giflib is not installed here), all returning premultiplied RGBA8, plus
  `img_sniff()` (magic bytes, never a file name) and `img_premultiply()`. Shared by the kitty graphics
  path in vt.c and the file manager's previews, so the two cannot drift. GIF decodes the FIRST image
  block only; an animated WebP is refused by libwebp's simple decode.
- `src/fm.c` — the file-manager model, no Wayland/EGL/threads: directory scan (`opendir`/`lstat`, sort,
  hidden filter, caps), navigation state, preview building (directory listing / image / text with syntax
  highlighting / metadata) and painting the two panes into `Cell` grids the existing renderer can draw
  unchanged. Also `fm_image_fit()` (the preview box in cells AND in px, aspect kept in pixels, never
  upscaled, centred) — the renderer only draws the box it is given, which is what makes the box testable.
  The two functions that touch nothing but malloc and the filesystem (`fm_scan_build`,
  `fm_preview_build`) are the only ones main.c's worker thread may call.
- `src/main.c` — all Wayland shell/input/clipboard/epoll logic: xkb keyboard, client-side key repeat, session
  shortcuts, pointer (bar clicks, pane focus, selection, mouse reporting, wheel), `layout_apply()`,
  frame-paced redraw, one epoll loop over every session's pty fd, SIGCHLD reaping by pid, CLI flags,
  data-device clipboard. Look here for pointer, keyboard, resize, frame pacing.
- `src/render.c` — EGL (platform Wayland) + GL 2.x: 2 glyph atlases + one GL texture per visible image,
  1 shader, 3 back-to-front passes emitted into ONE staging buffer but drawn as TWO batches (backgrounds,
  then bands + glyphs + chrome) with one draw per picture between them; instanced or expanded. Draws what
  `src/layout.c` says, owns no geometry.
- `src/font.c` — fontconfig pattern -> FreeType face; alpha8 glyph atlas (shelf packer + hash by codepoint);
  fontconfig fallback faces; HarfBuzz-shaped color emoji (`font_color_glyph`) + RGBA8 color atlas;
  `font_resize()` re-rasterizes at a new pixel size.
- `src/pty.c` — `posix_openpt`/`grantpt`/`unlockpt`; child: `setsid` + `TIOCSCTTY` + `chdir` + exec `$SHELL`
  (or `-e COMMAND`); sets `TERM`/`COLORTERM`/`TERM_PROGRAM`; `pty_setsize`.
- `src/config.c` — `key = value` config file, defaults, clamping, key-binding spec parser
  (`keybind_parse`/`keymods_parse`).
- `src/mux.c` — sessions of one window: dense `Tab` array (`{Term, Pty, attention}`), active index,
  open/close/goto/next/prev, `max_tabs`, unread-output flag, `mux_title()`. No Wayland/EGL/fd/fork — that is
  what makes it unit-testable.
- `src/layout.c` — pure geometry: in window px + cell px + padding + gap + mode + session list; out bar strip,
  `BarItem[]` rects + labels, `PaneGeom[]` rects (each with cols/rows), the file-manager band (`FmGeom`:
  browser rect + preview rect + their cols/rows) and `layout_hit_fm()`, `layout_grid_size()`,
  `layout_initial_size()`.
- `tests/test_vt.c` — 613 checks: parser, grid, selection, alt screen, `live_off`, emoji clusters, CSI 3 J,
  bracketed paste, `term_init` guard, reflow, `term_grid_geometry`, zoom/alt-screen row-shrink,
  `mouse_sgr_button`, the graphics path (base64/chunking/PNG/zlib/raw/premultiply/placement/delete/scroll
  anchoring/id replacement/DA1-DA2-DSR answers), mux, keybind/keymods parsing, multiplexing config keys,
  `layout_compute` / `layout_grid_size` / `layout_initial_size`. The source is list of what is covered; 2
  invariants with dedicated checkers are `row[]` permutation (also `tests/dbg.c`) and startup-size round trip.
- `tests/test_fm.c` — 220 checks against a real temporary directory tree (dirs, a dotfile, a dangling
  symlink, a FIFO, an unreadable dir, four image formats, a binary file): scan + sort + caps + truncation,
  the hidden filter and `fm_set_hidden`, selection and scrolling (`fm_row_pos`, `ensure_visible`),
  navigation, the request-id rule (a stale answer is dropped), every preview kind and its error path, the
  highlighter reaching the cells, the pane painters, `fm_image_fit`, and the band's rects inside
  `layout_compute()`.
- `Makefile` — pkg-config-driven; `-Iinclude/kai-term -Ibuild/generated`; output dirs via order-only
  prerequisites so they never force rebuilds; generates xdg-shell bindings with `wayland-scanner`.

**Facts not in code** (deps + MIT license: README "Requirements"/"License"):

- **Dependency rules:** harfbuzz required since s37 — README must keep listing it. Deliberately no
  cairo/pango/Qt/GUI framework. xdg-shell bindings generated at build time, not vendored.
- **Naming:** project/binary/identifiers `kai-term` (`kai_term` in C), display name `Kai-Term`. Renamed from
  `K-Term`/`k-term` 2026-10-16 (s59) due to a name conflict with another of user's projects — never
  "restore" short form anywhere: dir, binary, header, `DEF_APP_ID`/`DEF_TITLE`, log prefix,
  `TERM_PROGRAM`, config path all move together; tests/test_vt.c pins two `DEF_*` literals so a
  half-rename fails a check.
- **Layout:** sources `src/`, public header `include/kai-term/`, tests `tests/`, binaries `bin/`, objects +
  generated bindings `build/`. `make clean` deletes `bin/` and `build/` entirely. No `docs/` any more: the
  architecture doc was removed 2026-10-16 (s63) because it carried host-system facts; its invariants are in
  "Architecture & Invariants" below, its tooling parts in `/home/kaida/Project/ENVIRONMENT.md`.
- **License:** MIT, "Copyright (c) 2026 Kaida" — user's choice; must ship with any redistribution. No
  source-file SPDX headers exist; if added they must read `SPDX-License-Identifier: MIT`.
- **Git:** `origin = https://github.com/K-code-AI/Kai-Term`, branch `main`; agent has push access (user
  said so 2026-10-15). Still run `git log --oneline` / `git status` before assuming anything is committed;
  never commit `bin/` or `build/`.
- **Known limitations a newcomer must not "fix" by accident:** narrowing can evict OLDEST scrollback rows;
  alternate screen is deliberately NOT re-wrapped; a taller window keeps less scrollback than configured
  (`rcap` fixed at startup); window title is Kai-Term's own text, not app's OSC title. Full list:
  ISSUES.md "Known limitations".

## Architecture & Invariants

Rules a change must not break. Why each exists: git history of fixed bugs (`git show d5afe4a:ISSUES.md`).

**Data flow.** PTY bytes -> `term_parse()` (vt.c) -> one `Grid` per session -> `Mux.tab[]` owns one
`{Term, Pty}` per session, one ACTIVE -> `layout_compute()` (window px + cell px + padding + mode + session
list) -> bar strip, item rects, pane rects -> `render_draw(&rend, &font, &g_layout, terms)` -> bar + one grid
per pane + focus border -> EGL -> `wl_surface`. Input: `kb_key`/`ptr_*` -> pty_write to ACTIVE session,
after `ptr_*` hit-tested bar then panes.

**Event loop (main.c).**

- epoll over: `wl_display` fd; one pty master fd per session (`tab_for_fd()` maps back; pty EOF closes that
  session, not program); 8 ms timerfd (`FRAME_INTERVAL_NS`); `signalfd(SIGCHLD)` (SIGCHLD blocked so only
  signalfd reads it; reap `waitpid(-1,…,WNOHANG)`, match pids via `tab_for_child()`, then zero `pty.child`);
  paste idle timerfd; key-repeat timerfd; clipboard paste pipe read end joins transiently.
- **Finish wayland read before handling ANY other descriptor** (`read_events`/`cancel_read`, then
  `dispatch_pending`): `eglSwapBuffers` waits for a `wl_buffer.release`; while a
  `wl_display_prepare_read()` is outstanding that release can never be read -> swap blocks forever -> terminal
  looks dead. `read_events()` inside a drain loop instead blocks on display fd and starves
  timerfd/signalfd.

**Frame pacing — 3 rules, each from a real freeze.**

1. Arm a frame callback only together with a commit that attaches a NEW buffer (from `redraw()` after a
   successful swap). Damage-driven compositors run a render pass only on change, so a callback armed on an
   EMPTY commit can stay pending forever, and every draw path is gated on "no callback in flight" -> frozen
   display. `solicit_frame()` was deleted for exactly this — do not re-add it. Failed swap: keep `W.dirty` for
   timer to retry, arm nothing.
2. `eglSwapBuffers` is not guaranteed to return — it waits for compositor to release a buffer. The
   constraint is on wayland READ, not on listeners: swapping inside a listener is fine once read is
   complete. At most one buffer in flight (`W.frame_cb`).
3. The FIRST buffer must be attached by FIRST `xdg_surface_configure` handler (`!W.swapped_once`): a
   window without a buffer never maps, so never gets a frame callback either. A configure changing pixel
   size sets `W.resized`; timerfd branch and `frame_done()` draw when `dirty || resized` — waiting on
   `dirty` alone is what froze screen in fullscreen.

**Wayland/input.**

- Pointer coords are fixed point: `wl_fixed_t` is `int32_t` encoding px*256 -> apply `wl_fixed_to_int` before
  storing (storing raw int compiles warning-free and is 256x too large).
- Key repeat is client's job: `wl_keyboard.key` delivers one press + one release per physical keypress;
  `repeat_info` only reports system setting. `send_key()` returns whether press is repeatable; `krfd`
  arms for `repeat_delay`, re-arms at `1/repeat_rate`; release / focus loss / keymap change call
  `repeat_stop()`, which DRAINS an expiry that already fired. `it_interval.tv_nsec` must stay < 1e9. The key
  is re-TRANSLATED on every repeat -> Shift while holding `a` starts producing `A`.
- Seat bound at v5 for `axis_discrete`/`frame`; every listener slot bound version can dispatch must be
  non-NULL or libwayland aborts.
- Clipboard is push-based (no `request_selection`): copy = `set_selection`; paste = receive offered
  `text/plain` into a pipe whose read end joins epoll. One paste = ONE bracketed block: accumulate, write only
  at EOF (or 400 ms watchdog), in <=4096-byte pieces, wrapped `ESC[200~` … `ESC[201~` when DEC mode 2004
  is on; over 8 MiB sent unwrapped with a warning.

**Multiplexing/layout.**

- `Mux` owns a dense `Tab tab[TAB_MAX]` plus `n`/`active`; `cur_term()`/`cur_pty()` resolve through
  `mux_cur()` — no second copy of "which session is visible".
- `term_init()` memsets whole `Term`; `term_destroy()` frees both grids -> a session is initialized and
  destroyed exactly once; `mux_close()` zeroes vacated slot. Because `mux_close()` shifts later tabs down
  it must RE-ARM `tab[i].term.ps.t` for every moved tab and must capture `Pty` BEFORE shift.
- Output on a session with no pane: set `Tab.attention`, do NOT dirty frame. Output on a drawn-but-inactive
  session: dirty it, set no flag.
- `layout_apply()` is only caller of `layout_compute()`; resizes EVERY session to `layout_grid_size()` (so
  a tab switch costs no resize or reflow) and sets `W.gx/W.gy` to active session's pane origin, which
  `sel_cell()`/`mouse_cell()` map through. Runs from every geometry change AND from `redraw()`, because bar
  labels are part of picture.
- `ptr_button()` checks `layout_hit_bar()` BEFORE mouse-reporting branch — an app that grabbed mouse
  still cannot eat bar. Re-check `cur_term()` after a middle click (closing last session makes it NULL
  inside a listener).
- Chrome colours come from CONFIGURED palette (`Render.fg`/`Render.bg`), never from a `Term`'s pen — that
  is application's current SGR state and would let `printf '\e[31m'` repaint bar that names its tab.

**Grid model (vt.c).**

- `Grid.row` has fixed capacity `rcap = rows + scrollback_max` and is always a PERMUTATION of allocated
  buffers: scrolling is rotation (`rot_left`/`rot_right`), never `memmove` of pointers -> no slot is NULLed,
  duplicated or double-freed (`tests/dbg.c` asserts it). `grid_vprow(g,y) == row[scroll_off + y]`.
- Anchor invariant: `0 <= scroll_off <= live_off <= sb_limit = min(scrollback_max, rcap - rows)` and
  `live_off + rows <= rcap`. `live_off` is `scroll_off` showing newest output; user navigation moves
  `scroll_off` below it; `snap_to_live()` restores it before parsing so new output lands at live bottom.
- `cy` is a LIVE-view coordinate (parser addresses `row[live_off + cy]`) -> `term_resize` re-anchors `cy`
  with `live_off`. Cursor drawn only when `scroll_off == live_off`. Whole-viewport scrolls snap `scroll_off`
  back to `live_off` first.
- `Grid.valid == GRID_VALID` between init and destroy — a plain 0/1 flag is not enough because uninitialized
  stack memory is routinely non-zero (stack garbage measured `32511`).
- Alt screen: `Term.g` stays ACTIVE grid, main grid is parked in `Term.alt` (alt has scrollback 0);
  `?1049` saves/restores cursor, `?47`/`?1047` switch without saving, `?1048` is cursor-only;
  `term_resize` resizes parked grid too.
- CSI 3 J: `erase_scrollback()` moves viewport rows to slots `0..rows-1` through a validated scratch
  array, blanks rest, sets `scroll_off = live_off = 0`; gated by a process-wide setting, deliberately NOT a
  `Term` field because RIS memsets whole `Term`.
- Reflow: `Grid.wrap[i] != 0` means line in `row[i]` was broken by RIGHT MARGIN and continues in
  `row[i+1]`; a byte array parallel to `row[]` that rotates with it and is cleared by every operation that
  blanks or erases a slot. `grid_reflow()` measures each row with `row_used()`/`cell_pristine()`, not full
  width — otherwise never-written cells and wide-glyph padding become literal spaces and tracked cursor
  cell gets trimmed; joins and re-breaks at new width, pulling a boundary back only when cell that
  would START next chunk carries `ATTR_WIDE` — peek NEXT source cell, because reacting to a wide pair
  that legitimately ENDED a chunk loses a character; builds output oldest-first in a fixed `2*rcap+2` array,
  evicting oldest rows once `rcap` exist; commits by installing new buffers (permutation kept). The
  eviction step must run for EVERY produced row, blank rows included, or `kept` can exceed `rcap` and the
  commit drops buffers instead of installing them (a leak). Those 3 traps each cost a real bug.
  `grid_resize_plain()` is no-reflow variant for ALTERNATE screen (a full-screen app repaints itself;
  re-wrapping would scramble it) — parked main grid is still reflowed.

- **Images are anchored to an ABSOLUTE document row** (`Grid.row_base` + the row the cursor was on), never
  to a viewport row or a row pointer: a viewport row is invalidated by every scroll and a pointer by every
  reflow. `row_base` moves only when rows are evicted at the top or the window slides over its scrollback,
  so `image_prune()` can decide "this picture's rows no longer exist" from two numbers. The span in cells
  is computed in vt.c, not in the renderer, because it is also what `a=T` moves the cursor past — if the
  renderer scaled differently from the model the prompt would land on the wrong row.

**File manager band (fm.c + layout.c + render.c + main.c).**

- The band is geometry, model and drawing in the same three places as everything else: `layout_compute()`
  splits the grid area VERTICALLY first (a band of `fm_rows` rows on top, terminal pane(s) below at
  `pane_y = grid_y + band_h + gap`), then splits the band horizontally (browser | preview). Sessions stay
  ordinary `pane[]` entries, only shorter — that is what keeps `layout_hit_pane`, `layout_pane_of_tab`,
  `layout_grid_size()`, the focus border and `W.gx/W.gy` working with the band on AND off. `layout_hit_fm()`
  must be checked BEFORE `layout_hit_pane()`.
- The band is clamped, never trusted: `fm_rows` is reduced so the shell keeps `FM_TERM_MIN_ROWS` rows, and
  dropped entirely when even that leaves fewer than `FM_ROWS_MIN`. Clamped in layout.c, not config.c, so no
  caller can ask for a 1-row band — the tab bar's "the terminal wins" rule again.
- The two panes are `FmPane` = a flat `Cell` array + cols/rows, walked exactly like a session grid, so the
  renderer needs no new drawing path: "a grid with no cursor and no selection". Glyphs go through the same
  `emit_cell_glyph()` as a session (a file name can hold any codepoint, so the emoji/wide rules must be
  shared, not copied), backgrounds into batch A, glyphs into batch B, the preview picture between them.
- **The worker thread may call exactly two functions** — `fm_scan_build()` and `fm_preview_build()` — which
  touch nothing but malloc, the filesystem and their own argument. The main thread owns `FileMgr` and
  installs every answer, so the request-id rule in `fm_install_scan()`/`fm_install_preview()` is the ONLY
  synchronisation the model needs. Ids come from one monotonic counter (`FileMgr.seq`) with per-kind newest
  ids (`scan_req`, `pv_req`); a single shared counter was wrong because every preview then invalidated an
  in-flight listing. Answers cross a pipe as one 24-byte `FmResult` (atomic, far under `PIPE_BUF`); the
  request slots hold only the NEWEST request of each kind, so a superseded directory is never read at all.
- **The keyboard gate is the feature's safety:** `fm_keys()` is matched only while `g_fm.on` and
  `g_fm.focus != FM_FOCUS_TERM`, which is the only reason the default bindings can be unmodified keys.
  `fm_keys()` returns -1 (not the band's), 0 (handled, must not repeat) or 1 (handled, repeatable) — two
  states cannot express that, and getting it wrong sends Return/BackSpace/`r`/`Escape` to the shell as well
  as to the browser (measured live, s74).
- `fm_send_cd()` is the only place the feature writes to the shell, never to a session on the alternate
  screen, and only when the directory actually changed (`fm_last_cd`, cleared by `tab_switch()`).

**Geometry: one helper decides a grid's size AND its origin.**

- `term_grid_geometry()` (vt.c) is called once per pane from `layout_compute()` with `pad = 0` (window's
  `Config.padding` margin and pane gap are already in pane rect). 3 users must agree:
  `layout_apply()` (grid + PTY size), `sel_cell()`/`mouse_cell()` (pointer -> cell), `render_draw()` (where the
  quads go).
- Padding applied on all 4 sides, capped at `(window - one cell)/2` per side; `cols = (win_w - 2*xoff)/cell_w`;
  whole-cell remainder stays at RIGHT and BOTTOM, never top/left, so `row = (ptr_y - gy)/cell_h`,
  `col = (ptr_x - gx)/cell_w` is exact. Anchor anywhere else and every click, drag and SGR report lands on the
  wrong row.
- The margin is painted by `glClear`, so clear colour must be CONFIGURED background (`Render.bg`
  premultiplied by `Config.opacity`), not a hardcoded default; `render_draw` clears every frame.

**Rendering.**

- 2 glyph atlas textures (alpha8 mono, RGBA8 color emoji), each uploaded only when dirty, AFTER emit
  passes and BEFORE draw — glyph lookups run inside pass 3, so uploading at top of `render_draw` shows
  every new glyph one frame late (and a first emoji as a black box). Application pictures are the other
  kind of texture: one per image the model shows, keyed by `KImage.serial` (unique per session, never
  reused, so a recycled pointer cannot hand back a stale texture), re-uploaded when `version` changes,
  and deleted after any frame in which no visible image claimed it (`ImgTex.seen` stamped with
  `Render.img_frame`) — the GPU never holds more than what is on screen.
- One shader; `a_glyph` selects path: 0 background quad, 1 mono glyph (tinted by `v_fg`), 2 color emoji
  (premultiplied, not tinted), 3 an application picture (same premultiplied composite against the
  picture's own texture; vt.c premultiplies every decoded pixel, so still no GL blending).
- Pass order back to front: all cell backgrounds, then selection bands, then glyph quads — a WIDE glyph
  quad spans into next cell and neighbour's background must not be emitted after it. Continuation
  cells (`ATTR_WIDE`) are skipped in pass 3.
- **A frame is TWO draw batches, not one call**, because a picture must be over the cell backgrounds and
  under the text and blending is never enabled: batch A = pass 1 + bar backgrounds (range `[0,nq_bg)` of
  the staging buffer), then one draw per visible picture, then batch B = bands + glyphs + labels + focus
  border. Both ranges live in ONE staging buffer (records are fixed-size, so a batch is just a range);
  `glClear` runs ONCE per frame — clearing between batches would erase batch A. `setup_draw_state()` is
  re-applied before each batch because an atlas texture can be created between them.
- Instanced path when both `GL_ARB_draw_instanced` and `GL_ARB_instanced_arrays` are advertised; otherwise the
  same 19-float records are expanded to 21 floats per vertex and drawn with `glDrawArrays`.
- No GL blending is ever enabled: fragment shader composites `premul + bg*(1-a)` and a Wayland EGL ARGB
  buffer is PREMULTIPLIED, so writing raw colour with partial alpha reads washed out; `max(a, bg.a)` is wrong
  too (it painted glyph's bounding box opaque). `Config.opacity` must be clamped with `fclamp_`, never
  `iclamp_` (an int clamp turns 0.5 into 0).
- Color emoji: `putch` folds ZWJ, variation selectors, skin tones, keycap, tag chars and second regional
  indicator into ANCHOR cell's `extra[]` (`CLUSTER_MAX` 8) so a `Cell` stays copyable by value;
  `font_color_glyph()` hands whole sequence to `hb_shape`, which collapses it to one glyph id (anything
  else falls back to mono). FreeType loads it with `FT_LOAD_COLOR | FT_LOAD_NO_SCALE`; renderer scales by
  `cell_h / color_px`, so zoom never re-rasterizes an emoji. A wide cluster reserves TWO grid cells
  (`cluster_is_wide()` is single source of truth, also true when U+FE0F/U+20E3 switch base to emoji
  presentation) and `render_draw` sizes quad with same predicate. Cluster state is invalidated on
  every row rotation, cursor jump, BS/HT/CR, newline and resize. Misses are cached; color atlas starts at
  1024x512 and doubles up to 8192; a glyph that cannot fit falls back to mono rather than drawing with stale
  texcoords.

**Selection.**

- A drag only ever produces CELL edges: `sel_cell()` maps pointer to a cell; `sel_set_endpoints()` gives
  earlier cell its LEFT edge and later one its RIGHT edge; direction is recomputed on every motion
  event, so dragging left past anchor SWAPS pair.
- Pass 2 skips a cell that was never written (`ch == 0`) so a drag past end of a line stops at last
  glyph, while real spaces and `ATTR_WIDE` continuation cells keep their band; pass 1's cursor block uses the
  SAME predicate.
- Band edges stay ABSOLUTE buffer pixels — correct across panes because panes never overlap in x.

**Config.**

- `key = value`, read once at startup: `-c FILE` > `$XDG_CONFIG_HOME/kai-term/kai-term.conf` >
  `~/.config/kai-term/kai-term.conf`; CLI flags applied AFTER file so they win and never write back.
- `#` starts a comment only at line start or after a space/tab -> a value may contain `#`. Unknown keys log and
  are ignored; a missing file is not an error. Colours accept `#RRGGBB`, `0xRRGGBB`, `AARRGGBB`. Ranges are
  clamped at parse time.
- Defaults live in `config_default()` AND `DEF_*` macros — change both or README drifts.

## Debugging this project

- `./bin/kai-term -d` (log level 3) prints: font chosen + `color emoji: <path> (<n>px strike)`; GL version
  + instancing state; `shell starts in <dir>`; `grid resized to CxR (window WxH px, cell wxh, padding P,
  unused ux x uy px, tab i/n, tab view|side-by-side view, N panes)`; one
  `layout: tab view|side-by-side view, N pane(s), tab bar on|off, grid CxR per session` line per LAYOUT SHAPE
  change (client-side evidence for any bar/pane report; what `tests/cli_test.sh` greps for); session
  lifecycle (`mux: session 1/N started`, `tab i/n opened (pid P, pty fd F, grid CxR)`, `tab i/n visible`,
  `tab i closed, N left`, `the pty of tab i/n closed`, `the shell of tab i/n exited`,
  `the last session is gone, exiting`); bar clicks (`bar: click selects session i/n`,
  `bar: middle click closes session i/n`); `reflow: OLD -> NEW rows at N cols (K evicted)`;
  `compositor repeat_info: rate R/s delay D ms (kai-term uses …)`; `toplevel state: fullscreen F maximized M
  activated A (configure WxH px)` (only client-side evidence of entering fullscreen);
  `configure answered: buffer WxH attached (grid CxR)`; `zoom: font Npx, grid CxR`;
  `selection copied: N bytes` / `selection finished: ax,ay -> bx,by (copy_on_release off — Ctrl+Shift+C to
  copy)`; while a drag is live `sel press/drag cell=(col,row) frac=… ptr=(x,y) …` — how to check a
  selection-vs-pointer complaint without a screenshot, and it gives window's absolute position as
  `click_target - ptr`. A `swap failed (N in a row), retrying on the next frame` warning means
  `eglSwapBuffers` returned failure; a few are self-healing, a long streak means compositor is not showing
  this window.
- **Images.** `graphics: N image(s) drawn, M texture(s) held` is logged once per CHANGE in how many
  pictures are on screen (not per frame) — it is the client-side evidence that the renderer reached the
  image path at all; an accompanying `image glTexImage2D failed: 0x...` names the GL problem. Others:
  `graphics: dropped image N (W x H px) to stay within the budget`, `graphics: deleted N image(s)`,
  `graphics: unsupported payload (f=..., n bytes)`, `graphics: APC payload over ... bytes, discarded`,
  `graphics: file transmission (t=x) is not supported`. `./bin/kai-term -d -e 'timg -p k FILE.png; sleep 4'`
  exercises the whole chain and prints those lines. For the model alone, `.scratch/s70/timg_gate.c` runs
  the real timg in a PTY with Kai-Term's winsize, feeds its bytes through `term_parse()` and writes back
  the answers the model queues — no window, and it reports what timg chose (`--verbose` line) next to what
  the model holds. `timg`'s own `--verbose` output shows every query it sent and what it got back.
  The file-manager preview is counted separately and named — the line ends
  ` + the file-manager preview` when one is drawn, because it has its own texture (`Render.fm_tex`) and is
  not in the `M` count. `fm preview glTexImage2D failed: 0x... (WxH)` names a GL problem with it.
- **File manager.** `fm: opened on <dir>` / `fm: closed — the terminal is the whole window again`; a
  request `fm: listing <dir> (request N)` and its rejection `fm: dropping stale listing of <dir>
  (request X, newest Y)` (that pair is how you see the request-id rule working, and how a broken id shows
  up — s74's argument-order bug made every one of them read `request 0`); navigation
  `fm: open -> <dir>` / `fm: parent -> <dir>` / `fm: refresh -> <dir>`; the keyboard handover
  `fm: keyboard -> terminal|browser|preview`; a click `fm: click row N selects <name>`; what was typed at
  the prompt `fm: typed the selected path at the prompt: <path>`; the shell follow
  `fm: told the shell to follow: cd <dir>` (absent = the directory did not change, or the session is on the
  alternate screen, or `fm_follow = 0`); a failed read `fm: cannot open <dir>: <reason>`;
  `fm: worker started (result fd N)` — absent means the band was never opened, so no thread and no
  descriptor exist. The band's size is visible in the ordinary `grid resized to CxR …` line: with the band
  on, `R` is `fm_rows` + gap smaller than without it.
- `./bin/dbg` — scripted scroll/resize/reflow/zoom stages; each prints `scroll_off/live_off/rows/rcap/used`
  plus one 2-character token per slot (`v` viewport / `s` scrollback, then row's first cell), and asserts
  `row[]` is a NULL-free, duplicate-free permutation.
- `tests/cli_test.sh` drives real binary — `main()` is in no linked test, so it is the only honest coverage of
  argv + config keys.
- Headless harness: link `src/vt.c` + `src/config.c` (+ `mux.c`, `layout.c`) into a throwaway program in
  `/home/kaida/Project/.scratch/` to exercise logic Wayland input events cannot reach.

## Key Decisions & Pitfalls

- **`eglSwapBuffers` blocks, and a blocked swap looks like a dead terminal.** It waits until driver gets a
  buffer back and only compositor releases buffers, so a client whose surface is not being rendered stalls
  inside swap: no keyboard, no PTY reads, no redraw (measured `wchan = futex_do_wait`;
  `eglSwapInterval(0)` changes nothing on this NVIDIA driver). The rule is about wayland READ — earlier
  "never swap inside a listener" wording was BACKWARDS and caused a regression. Answering EVERY configure with
  a swap is also wrong (it spammed `eglSwapBuffers failed: 0x3003` every frame).
- **Geometry lives in `src/layout.c`, not main.c.** main.c is in no test binary, so arithmetic there is
  untestable, and N panes need N origins, which one global offset cannot express. The round trip
  `layout_initial_size()` -> `layout_compute()` -> `layout_grid_size()` is what proves startup height is
  right — it caught `cell_w`/`cell_h` typo (s62). Same reasoning put `mouse_sgr_button()` in vt.c.
- **The evdev button order is not protocol order.** `BTN_LEFT` 0x110, `BTN_RIGHT` 0x111,
  `BTN_MIDDLE` 0x112, but SGR numbers them 0 left, 1 middle, 2 right. A switch written in kernel order
  silently swaps RIGHT and MIDDLE — in bar's middle-click-close AND in every SGR report to vim/htop.
- **A log line that prints value it was GIVEN can lie.** `tab_switch(idx)` logged `mux: tab idx/n visible`
  while nothing had changed, so log looked like proof click worked and bug survived every reading
  of it. Log what owner of state REPORTS (`mux_active()`).
- **`Term` is not a place for settings.** `ESC c`/RIS calls `term_init`, which memsets whole `Term`, so
  process-wide settings are file-scope values with a setter (`kai_term_loglevel`, `g_clear_scrollback`) applied
  from config in main.c. `bracketed_paste` is deliberately opposite: it is application-negotiated, so a
  reset SHOULD clear it.
- **A Kai-Term-to-Kai-Term clipboard test proves nothing** — it works even if client never shares its
  selection. Test against a foreign client (`wl-copy` in, `wl-paste` out). With `copy_on_release = 0` (the
  default) a mouse drag does NOT call `set_selection`; only `Ctrl`+`Shift`+`C` does — that is what reads like
  "Kai-Term keeps a private clipboard".
- **A sanitizer target with an incomplete prerequisite list produces confident, wrong results.** `bin/dbg_asan`
  once kept running a stale binary and reported a 312-byte leak current source did not have. When a
  sanitizer finding contradicts tests, check binary's mtime against sources first.
- **A `(void)x;` line does not mean `x` is unused.** `(void)def1;` in `csi_dispatch` read like a leftover, but
  `def1` is used by `CSI J`, `CSI K` and `CSI 6 n`. Grep for uses, then let compiler confirm.
- **Never pass an out-parameter and the call that fills it as arguments of the same call.**
  `fm_nav(fm_go_sel(&g_fm, &seq), seq, "open")` compiles clean under the whole `WARN` set and is broken:
  C evaluates arguments in an unspecified order and gcc reads the caller's `seq = 0` BEFORE `fm_go_sel()`
  writes it. Measured live (s74): every listing issued after a navigation carried request id 0, so
  `fm_install_scan()` discarded all of them and the browser kept showing the directory the user had left.
  Fill the out-param in its own statement. Reproduce with `.scratch/s74/argorder.c` (three lines).
- **One int return value cannot mean "not handled" and "handled, do not repeat" at the same time.**
  `send_key()`'s return says only "is this repeatable", and falling off the function is what sends the key
  to the PTY. `if (fm_keys(...)) return 0;` therefore delivered every one-shot file-manager key to the
  shell too: Return on a file typed its path AND a newline (the shell would have run it), BackSpace went to
  the parent AND deleted a character at the prompt. Three states (-1 / 0 / 1) fixed it. Symptom to look for:
  the action happens AND its key appears in the terminal.
- **A log line that counts two different things as one number lies the same way one that prints its
  argument does.** `graphics: N image(s) drawn, M texture(s) held` began counting the file-manager preview
  in `N` while `M` still counted only the application-picture texture cache, so a preview alone logged
  `1 image(s) drawn, 0 texture(s) held`. Count each thing with the counter that owns it (s74).
