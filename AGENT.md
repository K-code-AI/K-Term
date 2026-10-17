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

## Current Status (build/test/asan re-verified 2026-10-17, session 70)

- Clean tree: `make` exit 0, **0 warnings** under `WARN` (`-Wall -Wextra -Wshadow -Wwrite-strings
  -Wformat=2 -Wdiscarded-qualifiers`). `make test` -> **553 checks, 0 failures** + **MODEL OK (0 problems)** +
  **CLI: 86 checks, 0 failures, 0 skipped**. `make asan` -> 553/0 + MODEL OK, no LeakSanitizer/UBSan report.
- **Image support (kitty graphics protocol) is in the tree**: APC/DCS parser states, chunk assembly,
  base64 + zlib + PNG (libpng simplified API) + raw f=24/32 decode, premultiplied pixels held in the
  model, images anchored to absolute grid rows, winsize pixel geometry + `CSI 14 t`/`CSI 16 t`, and a
  renderer that draws each picture between the background batch and the text batch. Verified headless
  (real timg bytes through the real parser) and live by log line (`graphics: 1 image(s) drawn, 1
  texture(s) held`, no GL warning) — **not yet confirmed by eye in the window**; the check is
  `timg -p k <file.png>`.
- No probe/experiment left in tree: `grep -rnE "TEMPORARY|PROBE|EXPERIMENT|NEGATIVE" src/ include/ tests/` ->
  empty.
- README flag table (`-e -w -c -d -v -h`) + 27 settings rows match `src/main.c` / `src/config.c`.
- **No open issues.** ISSUES.md "Open" is empty, with verification evidence. The 4 reports that used to be
  there were unproven (never reproduced here) and were dropped from ISSUES.md entirely on 2026-10-17 — they
  are not in "Fixed" either, since no fix was ever verified. Recoverable with `git show d5afe4a:ISSUES.md`;
  recurrence -> file a new Open item with a reproduction.
- **User-confirmed by eye in their window:** multiplexing + tab bar + side-by-side ("works as expected and
  looks great", 2026-10-17); zoom (no stale prompt lines), padding, fullscreen refresh, black-window/no-input
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

## Current Task (s70 — IN PROGRESS: finish image support docs + verify + commit)

**s70 objective:** close out the image-support work (s68 model/parser + s69 renderer/tests). What is
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
top) still needs the user's eyes.

**s70 remote synced:** `git fetch` first — nothing on GitHub to integrate, so `git push origin main` was a
fast-forward (exit 0). Re-fetched: `main` == `origin/main`, `git status -sb` shows no ahead/behind marker.
Read the truth with `git status -sb` / `git log --oneline origin/main -1` — the commit recording this line
is itself pushed.

**s70 STATE at session close:** image support (s68 model + s69 renderer/tests + s70 fixes and docs) is
implemented, documented (README "Images", ISSUES.md "Graphics / images") and committed — read the hash
with `git log --oneline -1`, not from here. The visual
confirmation is the user's, and the exact command to give them is
`timg -p k <some.png>` (plain `timg <png>` deliberately falls back to block characters — see the
auto-detect measurement above). If they report a wrong aspect, a wrong position or text hidden under a
picture, start from the `-d` line `graphics: N image(s) drawn` and the span math in
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

## Old Current Task (s69 — image support / timg, continuing s68)

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

NEXT ACTION: ask the user to look at `timg -p k <png>` and report; then pick from "Next Steps"
(housekeeping — no bug is open), or file a problem with a reproduction.

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
  `graphics: 1 image(s) drawn, 1 texture(s) held`, no GL warning. Visual confirmation is still the user's.
  Rule this session produced: **a documented example is a claim — run it before writing it.** Two of the
  three README examples I drafted were wrong (`--grid=RxC` arranges multiple FILES, not tiles of one image;
  `-V` does not animate in place because timg moves the cursor per frame).

## Next Steps

1. **Image support awaits the user's eye.** Everything measurable here is measured (see s70); what is
   missing is "does the picture look right in the window". Ask them to run `timg -p k <png>` (a test image
   is in `/home/kaida/Project/.scratch/s70/big.png`). If they want plain `timg <png>` to produce a real
   picture instead of block characters, the only honest route is implementing sixel — timg picks sixel
   exactly when the DA1 answer contains `;4` (measured). **Do not put `4`/`16`/`18` back into the DA1
   answer without implementing sixel**: that combination is what made the picture disappear entirely.
2. **No open bugs.** ISSUES.md "Open" is empty. If a dropped report recurs, file it with a reproduction and
   start from scratch — for second-instance crash: source + headless harness, not screen. Old wording of
   those 4 reports: `git show d5afe4a:ISSUES.md`.
3. Housekeeping, none a bug:
   - ASan build of `kai-term` binary itself as a real Makefile target (only tests have one). The
     hand-built one covered mux/layout and found `row[rcap]` overrun; covering Wayland/EGL would pay off
     (also covers heap-grown `paste_buf`).
   - Idle-frame optimization: stop frame cycle when nothing is dirty — `redraw()` now calls
     `layout_apply()` every frame, so that path is hotter than before.
   - Color atlas re-uploaded whole with `glTexImage2D` on every new shaped emoji; `glTexSubImage2D` per new
     shelf would be cheaper.
   - Reflow allocates a fresh row buffer per produced line — fine at terminal sizes, but a huge-width window
     with huge scrollback churns allocator.
   - Window title is `mux_title()`'s own text; an app's OSC title only reaches tab bar label. Showing the
     active session's OSC title in window title is a one-line change in `update_title()`, but it changes
     visible behaviour — ask first.
4. Clipboard hardening niri does NOT require (do not do unasked — untested against a compositor that needs
   it): `wl_data_offer_accept()` before `wl_data_offer_receive()`; offer `UTF8_STRING` +
   `text/plain;charset=utf-8` next to `text/plain`.
5. Emoji paths that exist but have NEVER run here: SCALABLE color font branch (`FT_IS_SCALABLE` ->
   `FT_Set_Pixel_Sizes` + `hb_ft_font_changed` on zoom) — this box has only a bitmap-only Noto CBDT font; and a
   cluster HarfBuzz renders as >1 glyph, deliberately rejected (`ng == 1`), falls back to mono.
6. Scratch cleanup: throwaway probes live in `/home/kaida/Project/.scratch/` (outside project). Tab bar /
   side-by-side is confirmed, so `s61/` + `s61_apinject*` need no longer be kept; keep `s45_old/` +
   `s47_before/` as A/B trees in case another draw-path regression is suspected. `s70/timg_gate.c` is the
   reusable end-to-end image harness — keep it. Ask before deleting anything outside repo — not done.
7. Git: keep committing verified work; `git fetch` before pushing.

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
├── src/                         # config.c vt.c mux.c layout.c pty.c font.c render.c main.c
├── tests/test_vt.c              # 553 checks (see below)
├── tests/dbg.c                  # grid-model invariant checker (permutation + scroll_off/live_off bounds)
├── tests/cli_test.sh            # 86 checks driving the REAL bin/kai-term (argv + config keys + log lines)
├── bin/                         # kai-term, test_vt, dbg (+ *_asan)          [generated, git-ignored]
└── build/                       # *.o + generated/xdg-shell-*                [generated, git-ignored]
```

- `src/vt.c` — unit-tested core: VT/ANSI/UTF-8 parser (incl. the APC/DCS states the kitty graphics
  protocol needs), grid model, the image list (decode + placement + lifetime), selection text
  extraction, alt screen, `term_grid_geometry()`, evdev -> SGR button mapping (`mouse_sgr_button`).
  No Wayland/GL dependency — the decoded PIXELS live here, so the whole protocol is testable headless.
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
  `BarItem[]` rects + labels, `PaneGeom[]` rects (each with cols/rows), 3 hit tests, `layout_grid_size()`,
  `layout_initial_size()`.
- `tests/test_vt.c` — 553 checks: parser, grid, selection, alt screen, `live_off`, emoji clusters, CSI 3 J,
  bracketed paste, `term_init` guard, reflow, `term_grid_geometry`, zoom/alt-screen row-shrink,
  `mouse_sgr_button`, the graphics path (base64/chunking/PNG/zlib/raw/premultiply/placement/delete/scroll
  anchoring/id replacement/DA1-DA2-DSR answers), mux, keybind/keymods parsing, multiplexing config keys,
  `layout_compute` / `layout_grid_size` / `layout_initial_size`. The source is list of what is covered; 2
  invariants with dedicated checkers are `row[]` permutation (also `tests/dbg.c`) and startup-size round trip.
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
