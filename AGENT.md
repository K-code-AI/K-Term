# Kai-Term — Agent Journal

Read this (plus ISSUES.md) before touching the project. Bug root causes live in ISSUES.md,
module design in docs/ARCHITECTURE.md, user-facing behaviour in README.md. This file keeps only
the current verified state, the environment facts, the pitfalls a newcomer must know, and a
short history.

> **Renamed 2026-10-16 (session 59):** the project is `Kai-Term` (binary `kai-term`, header
> `include/kai-term/kai-term.h`, config `~/.config/kai-term/kai-term.conf`, app id `Kai-Term`). It used to
> be `K-Term`/`k-term`. The current-state sections below use the new names; the dated Progress and History
> entries keep the names they were written with, because they record commands that were really run and log
> lines that were really printed. Quoted user sentences are kept verbatim too.

## Project Overview
Kai-Term is a lightweight Linux terminal emulator for Wayland written in C: native Wayland window
(xdg-shell) + PTY shell + VT/ANSI parser + EGL/OpenGL text rendering from two glyph atlases (alpha8
mono + RGBA8 color emoji) in one draw call per frame. No GUI framework.

## Current Status (build/test/asan re-verified 2026-10-16, session 62)
- Clean tree: `make` -> zero warnings under the Makefile's `WARN` set (`-Wall -Wextra -Wshadow
  -Wwrite-strings -Wformat=2 -Wdiscarded-qualifiers`, tightened in session 50; the tree is also clean
  under `-Wcast-qual -Wmissing-prototypes -Wstrict-prototypes -Wold-style-definition -Wredundant-decls`
  except inside the generated xdg-shell bindings, which is why `-Wcast-qual` is not in `WARN`);
  `make test` -> `501 checks, 0 failures` + `MODEL OK (0 problems)` + `CLI: 86 checks, 0 failures, 0
  skipped`; `make asan` -> `501 checks, 0 failures` + `MODEL OK`, with NO LeakSanitizer/UBSan report.
  No temporary probe or experiment remains in the tree (`grep -rnE "TEMPORARY|PROBE|EXPERIMENT|NEGATIVE"
  src/ include/ tests/` -> nothing). Re-run from a clean tree by session 62 (`make clean && make` 0
  warnings `/tmp/s62_base_build.log`, `make test` `/tmp/s62_base_test.log`, `make asan`
  `/tmp/s62_base_asan.log`); README's flags table (`-e -w -c -d -v -h`) and its 27 settings rows match
  `src/main.c` and `src/config.c`.
- **Multiplexing, the tab bar and the side-by-side view (sessions 56-62) are implemented, built, unit-tested
  and verified live by log lines — NOT yet confirmed by the user by eye.** What is proven here: `Ctrl+Shift+T`
  opens a session, `Ctrl+Shift+Right/Left` switch, `Ctrl+Shift+1`…`9` jump, `Ctrl+Shift+S` flips tab view and
  side-by-side view, `Ctrl+Shift+W` closes one and closing the last exits — all logged as `mux:` / `layout:` /
  `grid resized` lines by `.scratch/s61/tabs.sh`, and again under an ASan build of the binary itself with no
  sanitizer error. A LEFT click on a bar item switches and a MIDDLE click closes, proven with the absolute
  pointer injector (`.scratch/s61/barclick.sh`). 501 unit checks cover mux (tab bookkeeping, `mux_title`, the
  `ps.t` re-arm), the key-binding parser, the config keys, `layout_compute` (bar strip, item rects, labels,
  pane rects, the hit tests, `layout_grid_size`, `layout_initial_size`) and `mouse_sgr_button`. What is NOT
  proven by any of that: that the bar LOOKS right, that the labels are legible, that the focus border is where
  it should be — the user verifies anything visual themselves.
- **Zoom no longer leaves stale prompt lines — CONFIRMED BY THE USER** (session 54: "Zoom is fixed and
  now works as expected"). Root cause + evidence: ISSUES.md "Fixed: Zooming in and out left stale prompt
  lines and uneven gaps". 23 checks in tests/test_vt.c + `zoom-*`/`alt-*` stages in tests/dbg.c; the
  negative test (those checks against the pre-fix vt.c) gives 16 failures.
- **Window padding (`padding` config key, default 8 px) — CONFIRMED BY THE USER** (session 55: "I confirm
  the padding to be working properly in K-Term"). One helper, `term_grid_geometry()`, decides cols/rows
  AND the grid origin, and is used by the grid/PTY size, the pointer -> cell mapping and the renderer, so
  the margin, the click mapping and the quads cannot drift apart. Its follow-up — the margin being cleared
  with `DEF_BG` instead of the configured `background` — is FIXED (Render.bg + `set_clear_color()` + a
  per-frame `glClear`) and is now in ISSUES.md "Fixed", not "Open".
- **The user-reported black window / no input is FIXED and verified live** (session 47; re-verified live
  by session 48, including a wf-recorder frame diff). Root cause and
  evidence: ISSUES.md "Fixed: Black window, no prompt, no input". Short version: `eglSwapBuffers` was
  called while a `wl_display_prepare_read()` was still outstanding, so the `wl_buffer.release` it waits
  for could never be read and the swap never returned (`wchan=futex_do_wait`) — no picture past the first
  frame, no keys, no PTY reads. The event loop now finishes the wayland read (read_events / cancel_read +
  dispatch_pending) before it handles the pty/timerfd/signalfd/paste/repeat descriptors.
  Measured: 10/10 launches exit normally after the fix vs 1/10 stuck before; a `wf-recorder` frame diff
  shows the picture updating continuously (bright text px 1438 -> 11773 over 28 sampled frames); a niri
  resize sweep logs 10 `grid resized` + 11 `configure answered` with 0 swap failures and `wchan=ep_poll`
  throughout.
- **A pre-existing segfault on heavy output is FIXED too** (session 47; the live 5000-line run re-verified
  clean by session 48): `row[rcap]` was touched because
  `scrollback_max` is not reduced when the viewport grows. `sb_limit() = min(scrollback_max, rcap - rows)`
  is now the single bound. 11 new checks (312 -> 323) + `tests/dbg.c` stages that reproduce the identical
  ASan error against the pre-fix `vt.c`; the live 5000-line run now exits cleanly 3/3, also under an ASan
  build of kai-term itself.
- **Fullscreen (`NextFeature.md` #2) is DONE — CONFIRMED BY THE USER** (2026-10-15: fullscreen refreshes
  properly). The session-46 draw/commit rules plus the session-47 event-loop fix are what make it work;
  ISSUES.md now carries it under "Fixed", not "Open".
- **The clipboard is the system clipboard, in both directions — CONFIRMED BY THE USER** (2026-10-15: text
  copied in K-Term pastes into other apps, and text copied elsewhere pastes into K-Term). A report that it
  was "K-Term-private" was investigated and NOT reproduced: no source file was changed. Measured here,
  `wl-copy` -> injected `Ctrl`+`Shift`+`V` delivers the exact bytes to the PTY. The likely cause of the
  report is the intentional `copy_on_release = 0` default (a drag-release does not copy; `Ctrl`+`Shift`+`C`
  does) — see ISSUES.md "Known limitations" and docs/ARCHITECTURE.md "Clipboard is push-based".
- Confirmed working BY THE USER in the real window (sessions 43-44, 49, their own words): transparency,
  bracketed paste, `clear` dropping the scrollback, the shell's starting directory, color emoji, text
  selection, the configurable `app_id`, several instances at once, window resizing, key-hold repeat,
  fullscreen refresh, and clipboard interop with other applications.
  Session 45's `app_id` default change to `K-Term` was accepted.
- Implemented and verified at model + live level (not every one re-checked by hand): reflow on width
  change (43), color emoji + grapheme clusters (36-38), whole-glyph selection with no band over unwritten
  space (31-32, 41), compositor transparency (41-42), CSI 3 J + startup directory (39), bracketed paste
  (40), client-side key repeat (44), the draw/commit + event-loop pacing rules (45-47).
- Two grid behaviours every newcomer must know: narrowing can evict the OLDEST scrollback rows, and the
  alternate screen is deliberately NOT re-wrapped. A third since session 47: a taller window keeps less
  scrollback than configured (`rcap` is fixed at startup). All three are in ISSUES.md "Known limitations".

## Current Task
DONE (sessions 56-62, closed 2026-10-16) — MULTIPLEXING + TAB BAR + SIDE-BY-SIDE VIEW, at the user's request
("session 56/57 needs to be finished. Then it needs to be pushed"). Every step of the session-60 plan is
carried out: `src/main.c` is wired to `layout_apply()` / `render_init(..., conf.fg)` /
`render_draw(&rend, &font, &g_layout, terms)` so `bin/kai-term` links again; the whole suite runs (`make test`
-> 501/0 + MODEL OK + CLI 86/0, `make asan` clean); the live key/pointer-injection run session 56 never got
was done in session 61 and found TWO real defects (a bar/pane click that logged a switch it never made, and
`BTN_RIGHT`/`BTN_MIDDLE` swapped), both fixed and re-verified live; the new checks are in
(`layout_grid_size`, `layout_initial_size`, `mouse_sgr_button`, the three session-57 config keys in
cli_test.sh); and README / docs/ARCHITECTURE.md / ISSUES.md / AGENT.md are synced. Session 62 found and fixed
a third defect while checking the docs against the source (`tab_bar = always` started the window one row short
of the configured `rows`). The commit + push is the last step and is recorded in the session-62 Progress entry.
The session-56 and session-57 blocks below are the COMPLETED SPECIFICATION — kept because they record the
interpretation each feature was built to, not because anything in them is still to do.

WHAT IS OPEN NOW (nothing in progress — ask the user what to build next):
* ISSUES.md Open #1 — the second-instance `atlas glTexImage2D failed: 0x505 (512x512)` + SIGSEGV, the only
  crash left. Never reproduced here; the binary links again, so two live instances plus a headless harness
  are now possible.
* ISSUES.md Open #2 — `eglSwapBuffers failed: 0x3000` on launch plus one `swap failed (1 in a row)` warning;
  the retry works and nothing looks wrong. Decide whether it is a real first-frame race or a log line to soften.
* ISSUES.md Open #4 — the intermittent fullscreen freeze, deferred by the user (session 59).
* The user has NOT yet looked at the tab bar / side-by-side view in their own window. Their verdict is what
  closes that feature visually — the log lines prove the behaviour, not the appearance.
DONE (session 59) — PROJECT RENAME `K-Term` -> `Kai-Term`, at the user's request ("We will need to
rename K-Term to Kai-Term to avoid conflict with another project I found"). The other project is not
identified; nothing about it is known here. Steps 1-6 below are all carried out; the verification limit is
stated in the Progress entry (the binary does not link, for a pre-existing reason, so nothing that needs
`bin/kai-term` could be run).
What the rename covers (every name the project owns):
* directory `K-Term/` -> `Kai-Term/`; header `include/k-term/k-term.h` -> `include/kai-term/kai-term.h`;
  binary `bin/k-term` -> `bin/kai-term`; config file `~/.config/k-term/k-term.conf` ->
  `~/.config/kai-term/kai-term.conf`.
* strings and identifiers: `DEF_APP_ID`/`DEF_TITLE` `"K-Term"` -> `"Kai-Term"`, the log prefix
  `k-term <level>:` -> `kai-term <level>:`, `k-term: <cli error>`, the `-v` line, `k-term exiting`,
  `TERM_PROGRAM=k-term` -> `kai-term`, `k_term_loglevel` -> `kai_term_loglevel`, include guard
  `K_TERM_H` -> `KAI_TERM_H`, the Makefile's `-Iinclude/k-term` and `BIN`, and the test expectations that
  pin those literals (tests/test_vt.c app_id + mux_title checks, tests/cli_test.sh messages).
Planned steps (each verified before the next):
1. Baseline measured BEFORE touching anything (the tree does not fully build — see below).
2. Text rename in `src/`, `include/`, `tests/`, `Makefile`, `.gitignore` (sed, then a diff review).
3. File/dir moves (`git mv` for the header so git records it as a rename; plain `mv` for the project dir).
4. Verify: every translation unit except `main.c` compiles with 0 warnings; `bin/test_vt` + `bin/dbg` run;
   `main.c`'s error set is IDENTICAL to the pre-rename baseline (proof the rename introduced nothing).
5. Docs: README.md + docs/ARCHITECTURE.md renamed throughout (current-state docs); ISSUES.md Open +
   Known limitations renamed; AGENT.md current-state sections renamed. Dated journal entries and the
   ISSUES.md "Fixed" root-cause entries keep the names they were written with (they quote commands that
   were really run and log lines that were really printed) and each file gets one note saying the rename
   happened on 2026-10-16 (session 59) — that is the honest form of "consistent".
6. Report the two things OUTSIDE this project that still say `K-Term` and must follow the rename:
   `~/.config/k-term/k-term.conf` (the user's real settings — copied to the new path, the old file left
   in place) and `~/.config/niri/config.kdl` line 83 `match app-id="K-Term"` (a window rule; NOT edited
   here — it is the user's compositor config, they decide).
Expected outcome: `make` builds `bin/kai-term`, the window reports `app_id = Kai-Term`, the config is read
from `~/.config/kai-term/kai-term.conf`, and no file in the project still uses the old spelling except in
dated history. NOT part of this task: the GitHub repository name (`origin` stayed
`https://github.com/K-code-AI/K-Term` until the user renamed it there — DONE since: re-checked 2026-10-16
(session 62), `git remote -v` now says `https://github.com/K-code-AI/Kai-Term` and the push of session 62
went to it).

FOLLOW-UPS THE USER HAS TO DECIDE ON (outside this project. Re-checked 2026-10-16, session 62: the niri rule
still says `K-Term`, `/usr/local/bin/k-term` is still the only installed binary (root-owned, mtime 2026-10-15
22:08) and no `kai-term` is installed — `command -v kai-term` finds nothing while `command -v k-term` finds
`/usr/local/bin/k-term`; the desktop-terminal script still execs the old name and path):
* `~/.config/niri/config.kdl` line 83 `match app-id="K-Term"` — a window rule (no focus ring, half-width
  column). It stops matching once the app id becomes `Kai-Term`. Fix: change it to `match app-id="Kai-Term"`
  (or add a second `match` line so both work). niri reloads with `niri action reload-config`.
* `/usr/local/bin/k-term` — an INSTALLED copy of the old binary (root-owned, mtime 2026-10-15 22:08, md5
  8069009ae927fcfd2046f3e2a64ed85a). There is no `install` target in the Makefile, so it was copied by hand.
  Until it is replaced by `bin/kai-term`, the user is still running the OLD name with the OLD default config
  path.
* `/usr/local/bin/desktop-terminal` is `#!/bin/sh` + `exec k-term -c
  ~/.config/k-term/desktop-terminal.conf`, and `~/.local/share/applications/desktop-terminal.desktop`
  launches it. Both the command and the config path in that script need the new spelling once the binary is
  installed as `kai-term`. (Session 59 called this file root-owned; re-checked 2026-10-16 it is `-rwxr-xr-x
  kaida kaida`, 64 bytes, mtime 2026-10-15 16:29 — so it is editable without sudo, but it is the user's file
  and they said they would handle it.)
* The GitHub repository itself: DONE — re-checked 2026-10-16 (session 62), `git remote -v` reports
  `https://github.com/K-code-AI/Kai-Term` for both fetch and push, and `git push origin main` succeeded, so
  no `git remote set-url` is needed any more.

COMPLETED SPECIFICATION (written during sessions 56-58, before the session-59 rename; finished by sessions
60-62). These blocks were written BEFORE the rename, so read their paths with that in mind: `include/k-term/k-term.h` is now
`include/kai-term/kai-term.h`, `bin/k-term` is now `bin/kai-term`, and the header include in every source is
`#include "kai-term.h"` (`-Iinclude/kai-term`).
DEFERRED BY THE USER (session 59, 2026-10-16): the fullscreen freeze below is "intermittent and doesn't
always reproduce" — the user asked to note it and look at it later. It is now ISSUES.md Open #4 (with the
intermittency, the `-d`/`wchan` levers to use, and the note that the layout rewiring means the frame-pacing
rules must be re-checked rather than assumed to have survived), and the session-45-47 "Fixed" entry carries a
note that it must not be assumed to still hold. It is not the current task.
FORMER BLOCKING BUG REPORT (session 58, from the user): "it freeze the display when going full screen" — a
REGRESSION of the session-47 fullscreen freeze (ISSUES.md "Fixed": a configure that changes the size
must be answered with a buffer of the new size, and no swap may run while a prepared read is
outstanding). Reproduced/ diagnosed under "Progress" below; the fix must survive the layout rewiring.

SPECIFICATION (session 57) — TAB BAR + SIDE-BY-SIDE VIEW (DONE: wired in session 60, verified in sessions
61-62), the user's request: "when we have tabs to have a
visual indication showing the different tabs with the ability to click on them to swap between them" and
"it would be nice to be able to swap between Tab and Side by Side view".
What is being built (interpretation recorded so a resumed session does not have to guess):
* A TAB BAR drawn inside the window (one text row at the top, inside the Config.padding margin): one item
  per session showing its number, a `*` when a hidden session has unread output, and its OSC title (or
  `shell`); the visible session's item is highlighted. A left click on an item switches to that session,
  a middle click closes it. Shown by default when there are >= 2 sessions (`tab_bar = off|auto|always`).
* A LAYOUT MODE toggle: `tab` (one session fills the window, the bar names the rest) vs `side-by-side`
  (up to `split_max` sessions drawn as equal-width panes in the same window, the active one bordered);
  `key_toggle_layout` (default Ctrl+Shift+S) swaps them. A click in a non-active pane focuses that session.
* All geometry is decided in ONE new pure module, `src/layout.c` (no Wayland, no EGL, no fd): it turns
  window px + cell px + padding + mode + the tab list into the bar strip, the item rects, the labels and
  the pane rects (each with the cols/rows that fit in it). main.c resizes each displayed session to its
  pane and maps the pointer through the pane's origin; render.c draws exactly what layout says
  (bar backgrounds, per-pane grid, labels, active-pane border) in one draw call.
Planned steps (each verified before the next):
1. Baseline measured first (below).
2. `include/k-term/k-term.h` layout types + `src/layout.c` + Makefile + layout checks in tests/test_vt.c.
3. `src/render.c`: `render_draw(Render *, Font *, const Layout *, Term *const *terms)` — bar + panes;
   `Render.gx/gy` and `render_set_origin()` go away (the layout carries the per-pane origin).
4. `src/config.c`: `tab_bar`, `split_max`, `key_toggle_layout`.
5. `src/main.c`: layout_apply() (geometry + per-pane resize), the bar/pane click handling, the toggle
   binding, hidden-pane vs hidden-tab attention, the initial window height with the bar.
6. tests (layout + cli), `make`/`make test`/`make asan`, a live key-injection run (log lines are the
   evidence, no screen recording — the user verifies anything visual themselves), then docs.
Expected outcome: with 2+ sessions the user sees a clickable bar of tab names, clicking one swaps to it,
and Ctrl+Shift+S flips the window between one-maximized-session and all-sessions-side-by-side.
FINISHED SINCE (sessions 61-62): the items session 56 left open — `make asan` after the main.c wiring, the
live end-to-end key-injection check of the tab bindings, and the README/ARCHITECTURE/ISSUES entries for
multiplexing — are all done; see the session-61 and session-62 Progress entries.

DONE ENOUGH TO BUILD ON (session 56) — MULTIPLICING / TABS, the user's request: "We are going to add multiplexing
functionality to K-Term. The key-binding should be configurable in the config file."
What is being built (interpretation recorded here so a newcomer can resume it): one window hosting
several terminal SESSIONS (tabs), each with its own `Term` + PTY, one visible at a time, switched by
keybindings that come from the config file. Pane splitting is NOT part of this task.
Planned steps (each verified before the next):
1. Baseline measured first: `make clean && make` 0 warnings, `make test` 364/0 + MODEL OK + CLI 77/0
   (`/tmp/s56_base_build.log`, `/tmp/s56_base_test.log`).
2. `src/mux.c` + the `Mux`/`Tab`/`KeyBind` types in k-term.h — the tab bookkeeping (open/close/goto/
   next/prev, the `max_tabs` limit, the active index, the window-title text) and the key-binding spec
   parser, both free of Wayland/EGL so they are unit-testable.
3. Config keys for the bindings (`key_new_tab`, `key_close_tab`, `key_next_tab`, `key_prev_tab`,
   `key_tab_prefix`, `max_tabs`) in `src/config.c`.
4. `src/main.c`: the single `term`/`pty` globals become the ACTIVE TAB's; every input, render, resize,
   clipboard and event-loop path addressed; per-tab PTY fds in epoll (fd -> tab lookup); SIGCHLD reaps
   by pid and closes that tab; closing the last tab exits; teardown closes every tab.
5. Tests: mux + keybind checks in `tests/test_vt.c` (link `src/mux.c` in the Makefile), then
   `make`/`make test`/`make asan`, then a live key-injection run proving the bindings reach the
   terminal (`.scratch/inject`, the log lines are the evidence — no screen recording).
6. Docs: README (feature, settings rows, shortcuts), docs/ARCHITECTURE.md (module + data flow),
   ISSUES.md (the no-tab-bar limitation), AGENT.md.
Expected outcome: `Ctrl+Shift+T` opens a tab, `Ctrl+Shift+W` closes it, `Ctrl+Shift+Left/Right`
switch, `Ctrl+Shift+<1-9>` jumps, all rebindable in the config file; every session keeps its own
scrollback, selection and shell.

DONE (session 55) — documentation accuracy pass + commit + push, at the user's request ("Cleanup +
commit"). Everything from sessions 52-54 is now in the repo: commit `19c8bd3`, pushed to `origin/main`
(`b451d87..19c8bd3`), tree clean and level with the remote.
What was stale and is now corrected: the padding clear-colour item sat in ISSUES.md "Open" as NOT
VERIFIED although the user had confirmed padding works in their real window; the CLI-coverage item was
still listed as "Fix in progress" although `tests/cli_test.sh` + `make test-cli` are in the tree and
running; ISSUES.md/ARCHITECTURE still quoted 323 checks; ARCHITECTURE's `-d` line list omitted the
`padding` field that `grid resized` now prints; ARCHITECTURE's Modules table had no `tests/cli_test.sh`
row; README's target table did not mention `test-cli`; and this journal had NO Progress entry for the
session-54 padding work even though Current Status pointed at one (written now, reconstructed from the
diff, not from memory).

DONE (session 54, CONFIRMED BY THE USER in session 55) — the zoom bug: it is in ISSUES.md "Fixed".
Verified here: the grid fix (src/vt.c + the k-term.h prototype), 23 regression checks in tests/test_vt.c,
the `zoom-*`/`alt-*` stages in tests/dbg.c, and the negative test (those checks against the PRE-FIX vt.c)
-> 16 failures, all of them the zoom checks. `make asan` re-run in session 55: clean.

DONE (session 54, CONFIRMED BY THE USER: "I confirm the padding to be working properly in K-Term") —
window padding, and with it the `DEF_BG` clear-colour follow-up, which moved from ISSUES.md Open #4 to
"Fixed" in session 55. The user also ruled that the blank-cell colour I found while measuring is NOT a
defect ("working as intended") — do not re-open it.

OPEN WORK (the three real bugs in ISSUES.md "Open", all user-reported):
* **#1:** launching `kai-term` while ANOTHER kai-term is still running prints `atlas glTexImage2D failed:
  0x505 (512x512)` and then `SIGSEGV`; with no leftover process it starts fine twice in a row. Not
  reproduced here yet.
* **#2:** on launch, `eglSwapBuffers failed: 0x3000` + `swap failed (1 in a row), retrying on the next
  frame`; no visible effect, the retry works. Reproduce, explain, fix if it is a real defect rather than
  a benign first-frame race.
* **#4:** the intermittent fullscreen freeze, deferred by the user (session 59).

PREVIOUS TASK (session 51) — DONE: `-C` -> `-c`, retired-flag messaging deleted, one generic unknown-option
message; committed and pushed as `b451d87`.

### Progress (Instruction.md order: oldest first, newest last)
- 2026-10-14 (sessions 33-37, CONDENSED — the durable facts live elsewhere now): session 33 audited
  every doc against the source and a fresh build; session 34 moved the Wayland/grid invariants into
  docs/ARCHITECTURE.md; session 35 added fontconfig fallback faces (`font_find_face`, a source face
  stored per glyph so `atlas_grow` re-renders from the right face) and the three back-to-front render
  passes (a wide glyph quad was being clipped by the next cell's background); session 36 planned and
  session 37 built the color-emoji pipeline end to end (header types, vt.c cluster assembly, font.c
  HarfBuzz + RGBA8 color atlas, render.c `a_glyph == 2` on texture unit 1, Makefile `harfbuzz`,
  tests 155 -> 184) and fixed the two latent bugs ASan found. Design invariants: docs/ARCHITECTURE.md
  ("Color emoji & grapheme clusters", Rendering pass order). Root causes: ISSUES.md "Fixed".

- 2026-10-14 (session 39, CONDENSED): CSI 3 J so `clear` drops the scrollback (`erase_scrollback()`, gated
  by `clear_scrollback`, deliberately a vt.c file-scope setting and NOT a `Term` field because RIS memsets
  the Term), and the shell starting directory (`-w DIR` > config `working_dir` > `$HOME`, tilde expansion,
  `stat` validation with a warning + fallback, child `chdir`), plus the config `#`-comment rule fix so paths
  containing `#` survive. Verified by having the child shell write `pwd` to a file. Tests 206 -> 217.

- 2026-10-15 (session 40, CONDENSED — root causes in ISSUES.md "Fixed"): bracketed paste (DEC mode 2004
  tracked in `Term`, `paste_wrap()` as the tested reference, `paste_flush()` buffering + <=4096 B chunks +
  8 MiB cap + 400 ms idle watchdog), proven against a real interactive bash on a throwaway PTY harness
  (before: each line executed as it arrived; after: nothing runs until Return). Also the latent `term_init`
  free-of-an-uninitialized-grid bug and the `GRID_VALID` sentinel fix, plus README/ARCHITECTURE/ISSUES
  syncs. Tests 217 -> 238.

- 2026-10-15 (sessions 41-42, CONDENSED — root causes in ISSUES.md "Fixed", design in
  docs/ARCHITECTURE.md): `app_id` config key (default `k-term`, verified through niri); `opacity` made real
  end to end (per-quad alpha, `fclamp_` not `iclamp_`, premultiplied compositing, verified by `glReadPixels`
  alpha readback + a niri screenshot A/B: `-o 0.3` shows the backdrop through the window, `-o 1.0` reads
  exactly the background colour); selection endpoints snapped to whole cells (`sel_set_endpoints`, forward
  AND backward drags) and no highlight band over never-written cells (`term_cell_highlightable`, 17 checks);
  the resize path measured through niri IPC (90x38 -> 30x11 -> 180x55 -> 180x6 -> 90x38, 0 errors) which is
  how the "not fully resizable" complaint was traced to the missing reflow; both temporary hooks
  (`KTERM_SELTEST`, the readback) removed and the tree re-verified from clean at every step. Tests
  238 -> 260. PITFALL (mine, user-visible): a niri window was closed by TITLE and it was the user's
  pcmanfm-qt — always select by app id / pid / "the id that appeared after launch".

- 2026-10-15 (session 43, audit + cleanup, CONDENSED — durable facts now in ISSUES.md /
  docs/ARCHITECTURE.md / History): audited what session 42 had left behind before touching anything:
  `src/render.c` carried an unjournaled transparency fix (pass-3 glyph background alpha scaled by
  `r->opacity`, premultiplied source-over compositing, premultiplied `glClearColor` — measured by a
  `glReadPixels` readback: an `o` counter went from `rgba(16,16,20,127)` to `rgba(8,8,10,127)` while ink
  stayed alpha 255, and `-o 1.0` stayed all-alpha-255); the `app_id` config key was implemented AND
  verified end to end (niri reported `App ID: "com.example.ktest"`) but undocumented; the session-42
  `eglSwapBuffers failed: 0x3000/0x3003` and `atlas glTexImage2D failed: 0x505` lines were confirmed by
  the user to be artifacts of the throwaway probe/timeout harnesses. The user confirmed in this session:
  transparency, bracketed paste, `clear` dropping the scrollback, the startup directory, color emoji,
  selection, the configurable app id, multiple instances and window resizing all work as desired. Also
  removed the leftover `KTERM_PIXTEST` readback from `render_draw`.

- 2026-10-15 (session 43, REFLOW implemented, and an ASan report that was a stale binary): a width change
  now re-wraps instead of truncating — `Grid.wrap` soft-wrap marks + `grid_reflow()` +
  `grid_resize_plain()` for the alternate screen; the cursor travels with its logical cell; narrowing
  evicts the oldest rows. 32 new checks (260 -> 292) + `reflow*` stages in `tests/dbg.c`; live-verified
  through a niri resize sweep (936x1002 down to 260x600 and back, a `reflow:` line at every width change,
  0 errors). The `Direct leak of 312 bytes in row_new_cols` that `make asan` then reported was NOT a reflow
  bug: `bin/dbg_asan` was older than `src/vt.c` because the rule listed only `tests/dbg.c` as a
  prerequisite, so make ran a pre-fix binary. Fixed the Makefile (both sanitizer targets now list
  `src/vt.c src/config.c $(HDRS)`), proved it with `touch src/vt.c && make asan`, and `make asan` went
  clean. Docs pass: ISSUES.md (reflow moved to Fixed, two new known limitations, the stale-binary entry),
  README (reflow bullet, `app_id` row corrected), ARCHITECTURE (Reflow paragraph, 292 checks, the
  premultiplied transparency design session 42 had left undocumented).

- 2026-10-15 (session 43, `NextFeature.md` #1 + #2 — CLI surface and copy-on-release): removed `-f -s -c
  -r -o` from argv (they are config-only now, and `retired_flags[]` prints a message naming the setting to
  use instead of a bare usage line); added `--exec/--workingdir/--config/--debug` (+ `--help`/`--version`,
  `--name=VALUE`), `-h`/`--help` to stdout exit 0, errors to stderr exit 1; `copy_on_release` became a
  config toggle defaulting to OFF (a stray drag used to overwrite the clipboard; `Ctrl+Shift+C` copies
  either way) with a shared `cfg_bool()` behind both it and `clear_scrollback`. BUG FOUND AND FIXED: `-d`
  set `conf.loglevel` BEFORE `config_load()`, so a config with `log = 1` silently disabled debug logging
  even though README promises that CLI options win — `-d` now sets a flag applied after the file. Verified
  with `.scratch/cli_test.sh` (17 cases) and a config that really applied `app_id`/`font_size` while `-d`
  still logged. Tests -> 301. NEXT ACTION was #3, key repeat.

- 2026-10-15 (session 43, final re-verification, clean tree): `make clean && make` 0 warnings; `make test`
  -> `292 checks, 0 failures` + `MODEL OK (0 problems)`; `make asan` -> same, no sanitizer report; live
  `-d -w /tmp -e 'printf …; seq 1 200; clear'` -> exit 0, 0 error lines, logging `shell starts in /tmp`,
  `app_id: K-Term`, `color emoji: …NotoColorEmoji.ttf (128px strike)`, `ED 3: dropped 152 scrollback rows`,
  `k-term exiting`. REFLOW COMPLETE AND VERIFIED (code + checks + dbg invariants + live sweep + docs).
  Journal pruned (pre-prune copy `.scratch/AGENT.md.before_prune`).

- 2026-10-15 (session 44, `NextFeature.md` #3 — key repeat, implemented): Wayland compositors never
  synthesize repeats (`wl_keyboard.key` gives one press and one release; `repeat_info` only reports the
  system setting), so K-Term generates them. `Config.repeat_delay` (0-5000 ms, 0 = off) / `repeat_rate`
  (1-200 /s); the body of `kb_key` became `send_key(key, serial)`, which RETURNS whether the press is worth
  repeating (1 = bytes went to the PTY, 0 = terminal-local one-shots: zoom, Ctrl+Shift+C/V); a
  `CLOCK_MONOTONIC` timerfd `krfd` in epoll; `repeat_start()` one-shot delay, `repeat_fire()` re-sends and
  re-arms at 1/rate (tv_sec/tv_nsec split so rate=1 cannot hand `timerfd_settime` an invalid tv_nsec==1e9),
  `repeat_stop()` disarms AND drains a fired expiry so a release racing the timer cannot deliver one extra
  keystroke; `kb_leave` and `kb_keymap` stop it; the compositor's `repeat_info` is logged but deliberately
  unused. 11 new config checks (301 -> 312). Timing proven headless by `.scratch/mk_repeat_probe.sh`, which
  extracts the three shipped functions VERBATIM from src/main.c with sed and drives a real timerfd (15
  checks: first fire at 300 ms, 36 fires in the next 1200 ms = 30/s, `rate=1` valid, `delay=0` arms
  nothing). Design note in docs/ARCHITECTURE.md, limitation in ISSUES.md (one tracked key).

- 2026-10-15 (session 44, #3 FINAL — THE USER CONFIRMED it in the real window, "key hold now works
  properly"): re-verified from a clean tree — `make clean && make` 0 warnings, `make test` 312/0 + MODEL
  OK, `make asan` 312/0 + MODEL OK clean, live run logging the new `compositor repeat_info:` line. My own
  end-to-end attempt is recorded as a FAILED EXPERIMENT: `.scratch/keyinj.c` + `.scratch/repeat_live.sh`
  produced ZERO bytes in the PTY every time, and session 44 concluded injection was blocked by the
  compositor — session 45 proved that conclusion WRONG (the injector's key table was the bug). The feature
  does not rest on that experiment: it rests on the probe, the checks and the user's confirmation.

- 2026-10-15 (session 45, #1 DONE — default app_id is now `K-Term`): added `DEF_APP_ID "K-Term"` next to
  `DEF_FG`/`DEF_BG` in k-term.h — the default had been a string literal in FOUR places (config_default, the
  empty-value fallback, two in main.c), which is exactly how a default drifts. `tests/test_vt.c` asserts
  both `DEF_APP_ID` and the literal `"K-Term"`, so renaming the macro without updating the docs fails a
  test; README's `app_id` row updated. Verified live: the niri window that appears after launch reports
  `app_id= 'K-Term'` and the log line is `app_id: K-Term`. NOTE for the user: an existing
  `app_id = k-term` in their config still wins — only the built-in default changed.

- 2026-10-15 (session 45, #2 fullscreen — root cause found by reading the draw path; my first fix attempt
  was a REGRESSION, recorded before the next experiment): the freeze was K-Term's own. After the first
  swap every draw path was gated on "no frame callback in flight", and the only thing that armed one was
  `solicit_frame()` — which arms `wl_surface_frame()` and commits WITHOUT attaching a buffer; a
  damage-driven compositor delivers frame callbacks only during a render pass, so that callback can stay
  pending forever, `frame_armed` stays 1, the timerfd branch does nothing and the screen never updates
  again while keys and PTY output keep working. Entering fullscreen exposed it: `xdg_surface_configure`
  answered the size change with `solicit_frame()`, i.e. the buffer of the OLD pixel size plus the doomed
  callback; in a tiled layout other windows repaint the output so the done still arrives, a fullscreen
  terminal is alone on its output, so the stall lasts until fullscreen is exited — exactly the report.
  The invariant that replaced it (a frame callback is armed ONLY together with a commit that attaches a NEW
  buffer; `W.frame_cb` is the state instead of a `frame_armed` flag; a configure that changed the size must
  be answered with a buffer of that size) is what session 46 implemented.
  A/B evidence that my step-2 attempt was not good enough (`.scratch/s45_ab.sh`, the old code rebuilt from
  `.scratch/s45_old/`): the OLD code also logs `eglSwapBuffers failed: 0x3000/0x3003` — those errors
  PRE-DATE my change and were wrongly dismissed in session 42 as harness artifacts — but the old code
  follows every configure (8 `grid resized` lines, exits 0) while the new one stopped after two and then
  spammed 0x3003 every frame (alive in `ep_poll`, so a failing swap, not a deadlock).
  NEW TOOLS the user provided, which changed what is verifiable here: `wf-recorder` 0.6.0 + ffmpeg/ffprobe
  (record the screen, extract PNGs, diff them) and WORKING uinput key injection (`.scratch/inject.c`;
  niri's fullscreen keybind is `Mod+Shift+M`, `~/.config/niri/config.kdl` line 628).

- 2026-10-15 (session 46, #2 — the draw/commit rewrite implemented; the environment was the confounder):
  baseline of the tree as found (0 warnings, 312 checks). Measured the freeze mechanism directly: with a
  temporary probe, swap #1 (inside the first configure handler) succeeded, its frame done arrived, then
  the timer's swap #2 NEVER RETURNED (`/proc/<pid>/wchan` = `futex_do_wait`) — a blocked swap stops PTY
  reads and keyboard handling too, which is what the user describes. The throwaway EGL policy probe
  (`.scratch/egl_seq_probe.c`, 9 policies) showed the FIRST buffer must be attached in the FIRST configure
  handler, that swapping in every configure handler also blocks, and that `eglSwapInterval(0)` does not
  prevent the block on this driver.
  WHY THOSE MEASUREMENTS WERE UNRELIABLE: the user's niri session stopped honouring the IPC actions this
  harness relies on (set-window-width/-height returned 0 but never resized, `focus-window` left
  `is_focused: false`, at one point no window was focused at all). A surface the compositor never renders
  gets its map frame and nothing after it — no frame callbacks, no buffer releases — so ANY EGL client
  blocks in its second swap. That is an environment artifact, not a K-Term bug; session 47 re-confirmed the
  compositor is cooperative again. DECISION: stop live GUI experiments, fix by protocol rules + the A/B
  evidence, verify build/test/asan only, and hand the fullscreen check to the user (their work order
  forbids self-testing it anyway).
  THE IMPLEMENTED RULES (now documented in docs/ARCHITECTURE.md "Frame pacing"): arm a callback only with
  a new buffer; never swap inside a listener except the first configure (as session 46 understood it —
  session 47 found the real constraint is "not while a prepared read is outstanding", see its entry below);
  honour `render_draw()`'s return (a
  failed swap keeps the frame dirty, arms nothing, logs with a backoff); the new `W.resized` flag makes the
  timer and `frame_done()` answer a size change with a real buffer even when nothing else is dirty.
  Verified: `make` 0 warnings, `make test`/`make asan` 312/0 + MODEL OK clean, live smoke logs
  `grid resized to 117x62` + `configure answered: buffer 936x1002 attached (grid 117x62)`. Docs synced:
  ISSUES.md Open #1 (diagnosis + "awaiting verification" + how to verify — deliberately NOT in Fixed),
  ARCHITECTURE "Frame pacing" + the new `-d` lines.

- 2026-10-15 (session 46, FINAL — clean-tree re-verification and hand-off): `make clean && make` exit 0 /
  zero warnings; `make test` -> `312 checks, 0 failures` + `MODEL OK`; `make asan` -> same, no sanitizer
  report; the tree contained no probe or experiment. NOT VERIFIED DELIBERATELY: the fullscreen behaviour
  itself, and (in this environment) whether the blocked-swap regression was gone — the new binary still
  blocked in its second swap because the compositor was not rendering the test window, while the old
  binary only avoided it by never performing a second swap. That is why the user's verification is
  required. Scratch left outside the project tree in `/home/kaida/Project/.scratch/`:
  `egl_seq_probe.c`/`egl_seq_probe` + `seq_run*.sh`, `s46_win_rec.sh` + `s46_frames/` + `s46_win.mp4`,
  `s46_alive.sh`, `s46_main_before_probe.c`, `fs2/`, `fs3/`, `fs_frames/`, `s45_fs.mp4`,
  `s45_old/` (the last user-confirmed-good tree), `s47_before/` — delete once the user confirms.
  NEXT ACTION: read the user's verdict on fullscreen; only then move ISSUES.md Open #1 to Fixed. Do NOT
  commit anything — the repo still has zero commits and that is the user's call.

- 2026-10-15 (session 47, REGRESSION reported by the user — black screen, no input; root cause measured
  and fixed): the session-46 rewrite had broken the NORMAL windowed path, so the user never got to test
  fullscreen. The compositor was cooperative again, so live measurement was valid. Measured with temporary
  PROBE lines (gated on `KTERM_S47=1`) in the draw path: swap #1 returned, its frame done arrived, then the
  SECOND `eglSwapBuffers` never returned (`wchan=futex_do_wait`) — only the first, text-free frame was ever
  attached, and with the loop parked in the driver there were no PTY reads and no keyboard handling:
  exactly "black screen, no input". The same probe on the last user-confirmed-good tree
  (`.scratch/s45_old/`) swapped inside `frame_done` and every swap returned (`wchan=ep_poll`).
  ROOT CAUSE the journal had never recorded: the event loop called `wl_display_prepare_read()` at the top
  of every iteration and only `wl_display_cancel_read()` AFTER the fd loop, so the session-46 change (the
  timerfd branch now draws) ran a swap while a prepared read was outstanding — the `wl_buffer.release` the
  swap waits for can only be read by that same parked thread. The earlier "never swap inside a listener"
  rule was therefore BACKWARDS; the real rule is "never swap while a prepared read is outstanding".
  FIX: the epoll iteration scans the returned fds for the display fd, finishes the read
  (`wl_display_read_events()`, or `cancel_read` when epoll woke for something else) and
  `wl_display_dispatch_pending()`s BEFORE the pty/timerfd/signalfd/paste/repeat branches; the old
  read/cancel block at the end is gone and the frame-pacing comment states the correct rule.
  VERIFIED (probe build still in place): 31 swaps all `swap RETURNED`, 31 `frame_done`, clean `pty closed`
  + `k-term exiting`; the timer-driven swap specifically is proven safe. 10/10 launches exit normally
  after the fix vs 1/10 stuck before; a niri resize sweep logs 10 `grid resized` + 11 `configure answered`
  with 0 swap failures and `wchan=ep_poll` throughout; a wf-recorder frame diff shows bright text px
  growing monotonically. Then the probes were removed and the tree re-verified clean.

- 2026-10-15 (session 47, NEW BUG found while verifying — the pre-existing segfault on heavy output):
  `./bin/k-term -d -e 'for n in $(seq 1 5000); …'` SEGFAULTED 3/3, and the pre-fix tree
  (`.scratch/s47_before/`) crashed the same way, so it was NOT caused by the draw-path work. An ASan build
  of k-term itself (`.scratch/kterm_asan`, run with `ASAN_OPTIONS=detect_leaks=0`) gave `heap-buffer-overflow,
  READ of size 8 at 0 bytes after a 16240-byte region` in `grid_blank_slot` <- `scroll_up` <- `linefeed`;
  16240/8 = 2030 = `rcap`, so the code touched `row[rcap]`. Root cause: `rcap` is fixed at
  `rows_at_init + scrollback_max` but `scrollback_max` is never reduced when the viewport GROWS, and
  startup is exactly that case (config `rows = 30`, a 62-row window) — `scroll_up` gated on
  `so < scrollback_max` and then blanked `row[so + rows]`, reaching index 2030 once `so` saturated at 1968.
  Full root cause, fix and verification: ISSUES.md "Fixed: Segfault after ~2000 lines of output in a
  window taller than the configured rows".

- 2026-10-15 (session 47, the `sb_limit()` fix IMPLEMENTED — this entry was never written at the time;
  session 48 reconstructed it from the tree and re-ran every claim in it): `src/vt.c` has
  `static int sb_limit(const Grid *g)` (line 243) = `min(scrollback_max, rcap - rows)` as the single bound
  on `scroll_off`/`live_off`: `grid_reflow`'s anchor (473), `grid_resize_rows`' shrink branch (506) and
  grow branch (538), and `scroll_up` (1062, saturating `live_off` at it on 1078); the grid invariant
  comment (273-276) states the corrected invariant. `tests/test_vt.c` gained the "a viewport grown past
  half of rcap" block (line 1175: rcap fixed while the viewport grows, `scrollback_max` vs `rcap - rows`,
  `used <= rcap`, `live_off <= rcap - rows`, the newest line survives, scroll-down stays in bounds, and the
  same after shrinking again) — 312 -> 323 checks. `tests/dbg.c` gained `growthen-scroll-before/-scroll/-SD/-shrink/-scroll2`
  (lines 149-157), which reproduce the identical ASan error against the pre-fix `vt.c`.

- 2026-10-15 (session 48, docs/state audit at the user's request — session 47 had been cut off mid-docs):
  FOUND: the code was ahead of the journal — `sb_limit()`, the `growthen-*` dbg stages, the new test block
  and the ISSUES.md Fixed entry were all in place, but the newest Progress entry still ended with "NEXT
  ACTION: implement it". Nothing else was half-edited; no probe or experiment in the tree; still zero git
  commits.
  DONE: wrote the missing session-47 entry above; re-verified everything this session claims —
  `make clean && make` 0 warnings (`/tmp/s48_build.log`), `make test` -> `323 checks, 0 failures` +
  `MODEL OK` (`/tmp/s48_test.log`), `make asan` -> same, no sanitizer report (`/tmp/s48_asan.log`); the
  CLI surface matches README exactly (`-h`, `--version`, the retired `-o` message, `-x`); README's 17
  settings rows == `src/config.c`'s keys; live re-runs of both session-47 fixes (`/tmp/s48_A.log`,
  `/tmp/s48_B.log` — both exit 0 with `k-term exiting`, 0 error lines) and a wf-recorder frame diff
  proving the picture keeps updating (bright px 6732 -> 12302 across frames f011..f033, 720-1270 px
  changing every frame, `.scratch/s48_frames/`). NOT tested on purpose: fullscreen.
  JOURNAL MAINTENANCE: the session 43-47 detail that duplicated ISSUES.md/ARCHITECTURE was condensed and
  the Progress list put in Instruction.md's order (oldest first, newest last); pre-prune copy kept at
  `.scratch/AGENT.md.session48_preprune`. Sessions 44-47 added to History.
  DOC DRIFT FOUND AND FIXED (no source file was touched this session): docs/ARCHITECTURE.md "Frame pacing"
  rule 2 still stated the rule session 47 had disproved ("a swap must never run inside a listener") and
  contradicted its own rule 3 — rewritten to the real constraint (never while a `wl_display_prepare_read()`
  is outstanding; swapping in a listener is fine because the read is complete there), matching the comment
  above `request_frame` in src/main.c; the same stale wording was fixed in AGENT.md's `eglSwapBuffers`
  pitfall and in ISSUES.md Open #1; ISSUES.md gained the session-43 `-d`-vs-config `log` precedence bug,
  which had only ever been recorded in this journal; ISSUES.md Open #1's "it cannot be verified here"
  reworded (the compositor IS cooperative now — the reason it is unverified is the user's work order);
  ARCHITECTURE's `-d` line list gained `zoom:`, `selection copied:` and `selection finished:`; README gained
  the Scrollback and full-screen-application feature bullets it had never listed and a "Linux on a Wayland
  desktop" requirement line.
  NEXT ACTION: hand the two session-47 fixes and `NextFeature.md` #2 to the user for verification
  (Current Task). Nothing else is pending.
  AFTER THE DOC EDITS: `make` -> exit 0, 0 warnings (`/tmp/s48_build2.log`); `make test` -> `323 checks,
  0 failures` + `MODEL OK (0 problems)` (`/tmp/s48_test2.log`). `find -newermt` confirms only AGENT.md,
  README.md, ISSUES.md and docs/ARCHITECTURE.md changed — no source, header, test or Makefile file was
  touched this session. AGENT.md 921 -> 610 lines, all four docs consistent on 323 checks.

- 2026-10-15 (session 49, the user CONFIRMED the three pending items, then reported a clipboard bug that
  did NOT reproduce — no source file was changed): confirmed working in their real window: the terminal
  opens straight to a live prompt that keeps updating, a long output run no longer crashes, and
  `NextFeature.md` #2 (fullscreen refresh) works. ISSUES.md Open #1 therefore moved to "Fixed".
  THE REPORT: "K-Term has its own clipboard — what it copies can only be pasted inside K-Term, and what
  other software copies cannot be pasted into K-Term." Then, after my measurements: "It's fixed now…
  everything works, what I copy from K-Term is pastable in other apps and what I copy from other apps can
  be pasted into K-Term." Nothing in `src/` was ever touched (`src/main.c` mtime 10:24:55 = session 47), so
  nothing is claimed as fixed — the report simply does not reproduce.
  MEASURED HERE (niri cooperative; test window = the id that appeared after launch, closed by that id):
  * IN (external -> K-Term) WORKS, proven against a FOREIGN client: `wl-copy 'OUTSIDE-TEXT-123'`, focus the
    test window, injected `Ctrl`+`Shift+V` -> the PTY file holds exactly those 16 bytes and the log shows
    `paste: 16 bytes, bracketed 0` (`/tmp/s49_in.txt`, `/tmp/s49_in.log`). niri advertises for wl-copy:
    `text/plain text/plain;charset=utf-8 TEXT STRING UTF8_STRING`.
  * OUT (K-Term -> external) is NOT proven by my harness and NOT disproven either: a selection needs a
    pointer drag, and injected RELATIVE motion goes through libinput's acceleration, so the drag lands
    wherever it wants (an intended 120 px move jumped ~478 px; other attempts produced zero `sel press`
    lines). It rests on the user's confirmation. `.scratch/pinject.c` is the pointer injector,
    `.scratch/s49_*.sh` the harnesses.
  * WHY A HARNESS AT ALL: every clipboard test ever run in this project was K-Term -> K-Term, which cannot
    distinguish a shared clipboard from a private one — that is how such a complaint could survive.
  MOST LIKELY EXPLANATION (now a Known limitation in ISSUES.md): with the session-43 default
  `copy_on_release = 0`, releasing a drag does NOT put the text on the clipboard — only `Ctrl`+`Shift`+`C`
  (or `copy_on_release = 1`) does. A drag-release followed by a paste into another app therefore pastes the
  PREVIOUS clipboard content, which reads exactly like "K-Term keeps its own clipboard". Intentional
  behaviour the user asked for in session 43; documented so the next agent does not hunt for a
  data-device bug that is not there.
  PROTOCOL OBSERVATIONS worth keeping (recorded as caveats, not bugs): K-Term offers only `text/plain`,
  and `wl_data_offer_accept()` is never called before `wl_data_offer_receive()`. niri tolerates both
  (measured); a compositor that matches mime types strictly might not. Do not "fix" this unasked — it is
  untested against a compositor that needs it.

- 2026-10-15 (session 49, DOCS PASS + build re-verified — the user asked for "update relevant docs, make
  sure nothing is stale, no make error"):
  FILES CHANGED (docs only): ISSUES.md (fullscreen Open #1 -> Fixed with the user's confirmation as the
  verification; LeakSanitizer item renumbered to Open #1; two new Known limitations — drag-alone-does-not-
  copy, and the mime/`wl_data_offer_accept()` caveat), README.md (a "System clipboard" feature bullet; the
  selection paragraph now says `Ctrl`+`Shift`+`C` copies to the system clipboard and other applications'
  text pastes in with `Ctrl`+`Shift`+`V`), docs/ARCHITECTURE.md ("Clipboard is push-based, and it is the
  SYSTEM clipboard" + the foreign-client verification + the two protocol details; the "Frame pacing"
  cross-reference repointed from the old Open #1 to the Fixed entries), AGENT.md (Current Status lists
  fullscreen and the clipboard as user-confirmed; Current Task rewritten to NOTHING OPEN; Next Steps
  rewritten — the four "ask the user to verify" items are gone, housekeeping/emoji/git kept and renumbered;
  Environment gained the `wl-copy`/`wl-paste` recipe and the pointer-acceleration caveat; History gained the
  sessions 48-49 era line; new Key Decisions pitfall: a K-Term-to-K-Term clipboard test proves nothing).
  STALE-REFERENCE SWEEP: `grep -rn "Open #1|Open #2" *.md docs/*.md` — every live cross-reference now
  points at the right place; the remaining hits are dated journal entries (history, left as written).
  `grep -rn "awaiting|not verified|unverified" README.md ISSUES.md docs/ARCHITECTURE.md` -> nothing. Test
  counts agree everywhere (323; the 292/312 mentions sit inside dated entries).
  VERIFIED AFTER THE DOC EDITS: `make clean` exit 0; `make` exit 0, ZERO warnings/errors
  (`/tmp/s49_build.log`); `make test` -> `323 checks, 0 failures` + `MODEL OK (0 problems)`
  (`/tmp/s49_test.log`); `make asan` -> `323 checks, 0 failures` + `MODEL OK (0 problems)`, no
  LeakSanitizer/UBSan report, exit 0 (`/tmp/s49_asan.log`); live smoke
  `timeout -k 2 10 ./bin/k-term -d -e 'echo smoke-ok; sleep 1'` -> exit 0, 0 error/fail lines, `pty closed`
  + `k-term exiting` (`/tmp/s49_smoke.log`).
  NEXT ACTION: nothing pending — ask the user what to build next (`NextFeature.md` is empty after #2).
  JOURNAL MAINTENANCE + FINAL RE-VERIFICATION: the four overlapping session-49 entries were condensed into
  the two above (pre-condense copy `.scratch/AGENT.md.session49_precondense`); AGENT.md 755 -> 700 lines.
  After that edit: `make clean && make` exit 0 / ZERO warnings (`/tmp/s49_final_build.log`), `make test` ->
  `323 checks, 0 failures` + `MODEL OK (0 problems)` (`/tmp/s49_final_test.log`), `make asan` -> same with
  no sanitizer report (`/tmp/s49_final_asan.log`), live `-d` run exit 0 with 0 error lines
  (`/tmp/s49_final_live.log`). Only AGENT.md, README.md, ISSUES.md and docs/ARCHITECTURE.md were modified
  this session — no source, header, test or Makefile file.

- 2026-10-15 (session 50, cleanup pass — started). BASELINE measured before touching anything:
  `make clean && make` exit 0 / 0 warnings (`/tmp/s50_base_build.log`), `make test` -> `323 checks,
  0 failures` + `MODEL OK (0 problems)` (`/tmp/s50_base_test.log`), `make asan` -> same, no sanitizer
  report (`/tmp/s50_base_asan.log`).
  GIT FACTS (the journal's "zero commits, no remote" note is now OBSOLETE): `git log --oneline` =
  `18b9732 Initial commit` (all 15 project files) + `739013c Initial commit` (.gitignore + LICENSE),
  branch `main`, remote `origin https://github.com/K-code-AI/K-Term`. Working tree had `NextFeature.md`
  DELETED (the user removed it — every item in it is done) and `bin/`+`build/` listed as UNTRACKED.
  FOUND: the committed `.gitignore` is a generic kernel-module template (`.mod*`, `Module.symvers`,
  `*.dwo`, …) that ignores NEITHER `bin/` NOR `build/` — the Key-Decisions claim "`.gitignore` already
  covers `bin/` and `build/`" was FALSE, which is why a build polluted `git status`.
  DONE (step 1): `.gitignore` rewritten to project rules — `bin/`, `build/`, `*.o`, `*.obj`, `*.d`,
  `*.so`, `*.a`, `*.lo`, `*.la`, editor leftovers. VERIFIED `git check-ignore -v` matches bin/k-term,
  build/vt.o and build/generated/xdg-shell-protocol.c, and `git status --short` now lists only
  ` M .gitignore`, ` M AGENT.md`, ` D NextFeature.md`.
  DONE (step 2a): `src/config.c` — the 6 `NextFeature #N` comment references removed (the file they
  point at no longer exists); the wording now states the behaviour itself. No code changed.
  DONE (step 2b): `src/main.c` — 7 `NextFeature #N` comment references removed, wording kept the
  reasoning. Comments only, no code changed.
  DONE (step 2c): `src/render.c` (3 refs) and `src/vt.c` (1 ref, plus "the work order asks for it"
  reworded to "the user asked for it") — comments only, no code changed.
  DONE (step 2d): `include/k-term/k-term.h` (3 refs), `tests/test_vt.c` (5 refs; the `--- ... ---`
  banner lines were re-padded so they all stay 97 columns wide), `docs/ARCHITECTURE.md` (1 ref).
  `grep -rn NextFeature src include tests docs` -> nothing (the remaining hits are dated AGENT.md/ISSUES.md
  history, which stays as written).
  DONE (step 3, dead code):
  * `term_dump()` REMOVED — src/vt.c definition + the k-term.h prototype + the ARCHITECTURE bullet.
    Verified dead first: `grep -rn term_dump src include tests` found only the definition and the
    declaration, i.e. ARCHITECTURE's "used by tests/test_vt.c" was FALSE. Debug entry points are still
    the `-d` log lines and `./bin/dbg`.
  * `csi_dispatch`: the misleading `(void)def1;` self-silencing cast REMOVED, `def1` itself KEPT.
    MY OWN MISTAKE, caught by the compiler: I first deleted the whole `def1` local as "dead" and
    `make` failed with `'def1' undeclared` at src/vt.c:1375 — it is used by `case 'J': erase_display`,
    `case 'K': erase_line` and `case 'n': if (def1 == 6)` (CPR). Restored it with a comment saying what
    it is (the raw first parameter, VT default NOT substituted, unlike p1/p2). PITFALL for the next
    agent: a `(void)x;` line does NOT mean the value is unused — grep for uses before deleting anything.
  * `osc_end` (src/vt.c) made const-correct: `char *s` split into `char *start` (the mutable buffer,
    for `strchr`) and `const char *s` (the title text), so `s = ""` no longer discards `const`. This was
    a real warning under `-Wdiscarded-qualifiers`, invisible before because the Makefile does not enable
    that flag.
  VERIFIED after all of the above: `make` exit 0 / 0 warnings (`/tmp/s50_b2.log`), `make test` ->
  `323 checks, 0 failures` + `MODEL OK` (`/tmp/s50_t2.log`), `make asan` -> `323 checks, 0 failures`,
  no sanitizer report (`/tmp/s50_a2.log`). Check count unchanged, as intended (no behaviour change).
  DONE (step 4, stricter warnings): audited every src/*.c and tests/*.c under
  `-Wall -Wextra -Wshadow -Wcast-qual -Wwrite-strings -Wformat=2 -Wdiscarded-qualifiers
  -Wmissing-prototypes -Wmissing-declarations -Wstrict-prototypes -Wold-style-definition
  -Wredundant-decls`. Only four warnings were in project code, all `-Wshadow` in tests/test_vt.c
  (inner `a`/`b` vs the outer `double a, b` at line 628; inner `int n` vs the outer `int n` at 612;
  inner `char row[64]` vs the outer one at 125 — main() of that file is one 1100-line function, which is
  why they collide). Renamed the INNER ones (`s1`/`s2`, `wlen`, `linebuf`); the outer names are used far
  too widely to touch. The tree is now clean under that whole set EXCEPT `-Wcast-qual`, which fires only
  inside the wayland-scanner-generated xdg-shell bindings (not our file) — so the Makefile's `WARN` is
  `-Wall -Wextra -Wshadow -Wwrite-strings -Wformat=2 -Wdiscarded-qualifiers` with a comment saying why
  -Wcast-qual is left out. NOTE: do not test a CFLAGS change with `make CFLAGS=...` — a command-line
  variable overrides the Makefile's `+=`, so the `-Iinclude/k-term -Ibuild/generated` flags vanish and
  every compile dies on `k-term.h: No such file`. Edit the Makefile and run plain `make`.
  VERIFIED: `make clean && make` exit 0 / 0 warnings (`/tmp/s50_b3.log`), `make test` -> `323 checks,
  0 failures` + `MODEL OK (0 problems)` (`/tmp/s50_t3.log`), `make asan` -> same, no sanitizer report
  (`/tmp/s50_a3.log`).
  DONE (step 5, docs): ISSUES.md gained the Known limitation found while cleaning — the window title is
  always the literal `"K-Term"`: OSC 0/2 IS parsed into `Term.title` (and tested), but main.c never pushes
  it to the compositor. AGENT.md: Current Status re-dated to session 50 with the new `WARN` set, Project
  Files drops the deleted `NextFeature.md` and describes `.gitignore`/`Makefile` correctly, the Git
  Key-Decision is rewritten (remote + push access + the old .gitignore did NOT cover bin/build), Next
  Steps 1 and 6 updated, and the `(void)x;` pitfall added. README needed no change (no feature, flag,
  setting or dependency changed — verified its 17 settings rows and 6 flags still match the source).
  `git rm --cached NextFeature.md` staged the user's deletion.
  VERIFIED AGAIN after the doc pass: live `timeout -k 2 15 ./bin/k-term -d -e 'echo smoke-ok; seq 1 200;
  clear; echo done'` -> exit 0, 0 error/fail lines, logging `app_id: K-Term`, `grid resized to 117x62`,
  `ED 3: dropped 144 scrollback rows`, `k-term exiting` (`/tmp/s50_live.log`).
  DONE (step 6+7, commit + push): `git add -A` + one commit. The FIRST push was REJECTED — the user had
  already deleted `NextFeature.md` on GitHub (`89ebfd4`, `b2cf45c`), so the histories had diverged
  (local 1 ahead, remote 2 ahead). `git pull --rebase origin main` replayed the cleanup on top of
  `b2cf45c` with NO conflict (both sides deleted the same file), and the tree was re-verified after the
  rebase (`make clean && make` 0 warnings `/tmp/s50_p_build.log`, `make test` and `make asan` both
  `323 checks, 0 failures` + `MODEL OK (0 problems)`). PUSHED: `b2cf45c..6b471df main -> main`, so
  `origin/main` == `main` == `6b471df "Cleanup: dead code, stale references, stricter warnings,
  .gitignore"`; `git status -sb` shows no ahead/behind and a clean tree. PITFALL: always `git fetch` +
  check `git rev-list --left-right --count origin/main...main` before pushing — the user commits on
  GitHub too.
  SESSION 50 RESULT: cleanup complete — dead code gone (`term_dump`), no dangling `NextFeature` reference
  anywhere in src/include/tests/docs, the tree is warning-free under a stricter `WARN` set now enforced by
  the Makefile, `.gitignore` actually ignores `bin/`+`build/`, docs match reality, and everything is
  committed and pushed. NO behaviour change and NO test was added or removed (323 before, 323 after).
  NEXT ACTION: nothing open — ask the user what to build next.

- 2026-10-15 (session 51, the user's CLI request: `-C` -> `-c`, and no error message about removed flags).
  REQUEST: "change the -C launch argument to -c, no need to use the capital one"; "there shouldn't be
  error message referencing Arguments we removed in the past"; "when using an invalid argument it should
  just say the same invalid argument with the help print".
  src/main.c: (a) the config flag is now `-c` (`--config` and `--config=FILE` unchanged) — note `-c` had
  been one of the RETIRED flags (it used to mean `cols`), so the two changes fit together: the name is
  free once the retirement table goes. (b) `retired_flags[]` and its lookup loop DELETED — the table that
  printed "`-o` is not a command-line option any more; set 'opacity' in the config file (-C FILE picks
  another one)". (c) ONE generic path for everything else: `k-term: unknown option '<arg>'` + `usage()`
  on stderr, exit 1. `-h`/`--help` still print the option list to stdout with exit 0, now listing
  `-c, --config FILE`.
  VERIFIED by running the binary (`.scratch`-style harness /tmp/s51_cli.sh, 31 checks, 0 failures —
  argv is NOT unit-tested, main.c is not linked into bin/test_vt, so the binary is the only honest check):
  `-h` exit 0 lists `-c` and no `-C`; `--help` identical; `-c FILE` really loads that file (`app_id:
  cli-lower-c` logged and `font_size = 30` applied, cell 18x34); `--config=FILE` and `--config FILE` both
  still work; `-C FILE` -> `k-term: unknown option '-C'` + usage, exit 1; `-f -s -r -o` each produce the
  IDENTICAL generic message and no "any more" text; `--bogus` the same; `-c` with no argument ->
  `k-term: -c needs an argument`, exit 1; and `-d` still beats `log = 0` in the config (the session-43
  precedence fix survives). Full re-verification: `make clean && make` 0 warnings (/tmp/s51_build.log),
  `make test` 323/0 + MODEL OK (/tmp/s51_test.log), `make asan` 323/0 + MODEL OK with no sanitizer report
  (/tmp/s51_asan.log), live `-d -e 'echo smoke-ok; seq 1 200; clear'` exit 0 with 0 error lines
  (/tmp/s51_live.log). Test count unchanged (323) — no test covers argv.
  DOCS: README flags table + the `-c FILE` config-location sentence + the paragraph that used to promise
  "passing the old `-f`, `-s`, `-c`, `-r` or `-o` flags prints a message naming the setting" (now: any
  option K-Term does not accept is rejected with `k-term: unknown option '<flag>'` plus the option list);
  docs/ARCHITECTURE.md's config-file section (`-c FILE`); ISSUES.md's session-43 `-d` entry annotated with
  "the config flag is `-c` since session 51" so its historical `-C <conf>` command is not misread.
  NEXT ACTION: nothing open — ask the user what to build next.

- 2026-10-15 (session 52, fix list #0/#1/#2 from the user). Recorded all three in ISSUES.md "Open" as
  #0 (no CLI coverage), #1 (second instance: `atlas glTexImage2D failed: 0x505` + SIGSEGV), #2
  (`eglSwapBuffers failed: 0x3000` on launch); the old LeakSanitizer item is now Open #3.
  DONE (#0): `tests/cli_test.sh` (POSIX sh, checked in) + a `test-cli` Makefile target that depends on
  `$(BIN)` and runs `sh tests/cli_test.sh ./bin/k-term`; `test` now depends on `test-cli`, so `make test`
  covers the flags too. The script drives the REAL binary (argv cannot be unit-tested: main.c is not
  linked into bin/test_vt), wraps every launch in `timeout -k 2 15` so a hang cannot wedge the suite,
  cleans its temp dir with a trap, and skips the window-opening checks with a printed SKIP when
  WAYLAND_DISPLAY is unset. Coverage: -h/--help identical + all six options listed + no `-C`; -v/--version;
  `-C -f -s -r -o -x --bogus` each rejected with the identical generic first line + usage + exit 1 + no
  "any more" text + nothing on stdout; -e/-w/-c and their long forms with a missing argument (the message
  echoes the flag exactly as typed — that is the real behaviour, my first expectation was wrong); -c FILE /
  --config FILE / --config=FILE really load the file (app_id + font_size 30 -> cell 18x34); -d beats
  `log = 0` in the file and without -d the file's level is kept; -w DIR logs `shell starts in DIR`;
  -e COMMAND ends by itself (`pty closed` or `child exited`) and logs `k-term exiting`; all flags combined.
  RESULT: `make test-cli` -> `CLI: 77 checks, 0 failures, 0 skipped`.
  PROVED THE SUITE ACTUALLY TESTS SOMETHING (negative test): temporarily reverted `-c` to `-C` in the
  parser AND re-added a `-o`-specific "not a command-line option any more" message -> 15 failures naming
  the help listing, the `-C` rejection, the `-o` message and every config-flag behaviour; then
  `git checkout -- src/main.c`, rebuilt, back to 77/0.
  NEXT ACTION: #1 — reproduce the second-instance crash (`atlas glTexImage2D failed: 0x505 (512x512)` +
  SIGSEGV when another k-term is left running).

- 2026-10-15 (session 53, NEW user bug #4 — zoom leaves stale prompt lines and uneven gaps; ROOT
  CAUSE FOUND AND FIXED IN THE GRID, verification in progress). REPORT: after a few Ctrl+- / Ctrl+=
  zooms the prompt spacing drifts apart and old prompt lines stay on screen; the user supplied a
  kitty screenshot as the reference (packed, consistent).
  REPRODUCED HEADLESS — no display, no key injection needed. `.scratch/s53/pty_zoom.c` runs the REAL
  fish in a REAL PTY (src/pty.c) through the REAL grid model (src/vt.c), feeds the PTY output to
  `term_parse`, hands `Term.resp` back to the PTY exactly as main.c does (without that fish stalls in
  its startup capability queries and prints nothing — my first two attempts showed an empty grid for
  that reason, one of them because `tv_sec*1000` overflowed an `int` in my own harness), then applies
  zoom-shaped resizes (cols AND rows change together, as `zoom_apply`/`term_size_changed` do). Before
  the fix: one shrink 50 -> 41 rows put the prompt at slots 1-2 in SCROLLBACK and fish's SIGWINCH
  repaint at 10-11 (8 blank rows between); after three zoom cycles: three prompts, two gaps. That is
  the user's screenshot, byte for byte. `.scratch/s53/cursor_repro.c` isolates the mechanism: the
  cursor's absolute row[] slot goes 2 -> 9 across one shrink (it leaves its own line), and 9 stays 9
  across the following grow, so the cursor drifts down ~7 rows per zoom step.
  ROOT CAUSE: `grid_resize_rows()`'s shrink loop always gave up the viewport's TOP row (it becomes the
  newest scrollback line). When the viewport shrinks by MORE rows than the cursor has rows above it —
  the normal case at a prompt, where everything below the cursor is blank — the cursor's own line was
  pushed into scrollback, and `term_resize()` then clamped `t->cy` to 0. The shell repaints its prompt
  at that clamped cursor, so every zoom step adds a fresh copy of the prompt and leaves the old one.
  NOT a renderer bug, NOT reflow, NOT the zoom code itself: `zoom_apply` and `term_size_changed` were
  read and are fine; a pure grid test with hard-broken lines (`.scratch/s53/zoom_repro.c`) showed the
  reflow itself keeps rows 1:1.
  FIX (src/vt.c + the k-term.h prototype): `grid_resize_rows(Grid *g, int rows, int cur_slot)` — the
  shrink now gives up the viewport's BOTTOM row (blanked, becomes a free slot) while the cursor still
  has rows below it, and only after that falls back to the old top-row-into-scrollback behaviour (which
  keeps the newest content and moves the cursor's viewport row with it). `grid_resize` passes the
  cursor slot AFTER `grid_reflow` has moved it; `grid_resize_plain` gained a `cur_slot` argument so the
  alternate screen keeps the same rule (vim/htop cursor), and `term_resize` passes it. -1 = no cursor
  to preserve = the old behaviour (that is what tests/dbg.c uses).
  VERIFIED SO FAR: `make` 0 warnings; the isolated repro now keeps the cursor at slot 2 / cy 2 through
  shrink AND grow; the fish harness now shows exactly ONE prompt at slots 1-2 with cursor cy=2 after
  all six zoom steps (was three prompts and two gaps); `make test` -> `323 checks, 0 failures` +
  `MODEL OK (0 problems)` + `CLI: 77 checks, 0 failures, 0 skipped` (unchanged — no new test yet).
  NEXT ACTION: add regression checks to tests/test_vt.c (cursor slot preserved across a shrink that
  removes more rows than the cursor has above it; content below the cursor dropped, not scrolled; a
  full viewport still scrolls into scrollback) and a tests/dbg.c stage for the row[] permutation
  invariant; then `make test`/`make asan`/a live zoom run, then docs.

- 2026-10-15 (session 54, RESUMED session 53's zoom fix — tests finished and negative-tested; asan and
  the live run still to do). STATE AS FOUND (re-measured rather than trusted from the journal): the grid
  fix was already in `src/vt.c` + the `k-term.h` prototype, and 23 regression checks were already in
  `tests/test_vt.c` (its mtime is newer than vt.c) — but the journal still said "no new test yet", so the
  first job was to re-verify, not re-implement. `make clean && make` exit 0 / 0 warnings
  (/tmp/s54_build.log); `make test` -> `346 checks, 0 failures` + `MODEL OK (0 problems)` + `CLI: 77
  checks, 0 failures, 0 skipped` (/tmp/s54_test.log). 323 -> 346: session 53's NEXT ACTION had in fact
  been carried out before the interruption.
  DONE (the missing test piece): `tests/dbg.c` gained the `zoom-*` / `alt-*` stages after
  `growthen-scroll2`. A cursor-aware shrink is a NEW code path (the freed bottom slot is blanked IN
  PLACE instead of the window sliding down one slot), so `row[]` must stay a NULL-free, duplicate-free
  permutation and the anchors in bounds for: cursor mid-text with rows below it (`zoom-bottom`, nothing
  enters scrollback), a shrink past the cursor (`zoom-past-cursor`, the top rows then DO go to
  scrollback: `scroll_off=1 live_off=1 rows=2 used=3`), growing back, a 3-cycle zoom sweep where cols
  AND rows change together (`zoom-sweep`), and the alternate screen, which gets the cursor slot through
  `grid_resize_plain` (`alt-shrink`/`alt-grow`/`alt-exit`). `make test` -> `346 checks, 0 failures` +
  `MODEL OK (0 problems)` (/tmp/s54_test2.log). NOTE: `chk()` validates only the permutation/anchor
  invariants, so these stages prove the new path does not BREAK them — they do NOT fail against the
  pre-fix code; the content/cursor assertions that pin the fix are the test_vt.c ones.
  NEGATIVE TEST (proves the 23 new checks are not vacuous): built the NEW tests/test_vt.c against the
  PRE-FIX sources (`git show HEAD:src/vt.c` + `HEAD:include/k-term/k-term.h` into /tmp/s54_neg/, one
  `cc` line) -> `346 checks, 16 failures`, and the 16 are exactly the new zoom checks ("shrink keeps the
  cursor on its own row[] slot", "zoom cycles keep the cursor slot", "alt screen keeps the cursor's line
  visible", …). Against the fixed vt.c: 0 failures.
  MY OWN MISTAKE THIS SESSION, recorded because it damaged this file: a python one-liner meant only to
  REORDER the Progress entries (sessions 52/53 sat at the top, which contradicts the "oldest first,
  newest last" header) reused index ranges captured BEFORE the deletions, so it deleted the 52/53
  entries and pasted a garbage slice (part of a session-43 entry + Environment bullets) in front of
  "## Next Steps". REPAIRED by deleting that inserted block and re-writing the 52/53 entries from the
  copy of this file read at the start of the session, then re-checking the whole file against
  `git show HEAD:AGENT.md` (`diff -u` shows only the intended changes: the Current Task rewrite and the
  three new Progress entries in chronological order). PITFALL for the next agent: never splice a text
  file with indices captured before an earlier splice in the same script — re-find the anchors after
  every mutation, and diff against git afterwards.
  NEXT ACTION: `make asan`; then the live check in the real window (inject `Ctrl+-`/`Ctrl+=` several
  times with `.scratch/inject` and read K-Term's own `grid resized` log lines, plus re-run
  `.scratch/s53/pty_zoom.c` as the headless ground truth); then docs (ISSUES.md #4 Open -> Fixed only
  after that evidence or the user's confirmation) and commit + push.

- 2026-10-15 (session 54, WINDOW PADDING — the user's request, CONFIRMED WORKING by the user in session
  55: "I confirm the padding to be working properly in K-Term"). This entry was never written at the time;
  session 55 reconstructed it from `git diff`, not from memory, because Current Status was already
  cross-referencing it.
  REQUEST: "add a bit of padding to the side and top, like the bottom, to make sure not to cut a glyph if
  a user is using rounded corner for his windows" (niri rounds window corners; with the grid flush to the
  edge the corner glyphs were clipped).
  IMPLEMENTED (file by file, from the diff):
  * `include/k-term/k-term.h` — new `Config.padding` (px, all four sides, 0 = the old edge-to-edge
    layout, and the note that the bottom margin is `padding` + the whole-cell remainder); new
    `term_grid_geometry(win_w, win_h, cell_w, cell_h, pad, &cols, &rows, &xoff, &yoff)` prototype with the
    three-users-must-agree rule in its comment; `Render.gx/gy` (the grid origin in buffer px), `Render.bg`,
    `render_init(..., float opacity, uint32_t bg)` and `render_set_origin()`.
  * `src/vt.c` — `term_grid_geometry()` implemented: margin on all four sides, margin capped so at least
    one whole cell always fits, `cols = (win_w - 2*xoff) / cell_w` with the leftover pixels left at the
    RIGHT/BOTTOM (that is what keeps `col = (ptr_x - xoff) / cell_w` exact).
  * `src/config.c` — `padding` default 8 in `config_default`, and the `padding` key clamped 0..128.
  * `src/main.c` — `Wayland.gx/gy`; `term_size_changed()` now makes ONE `term_grid_geometry()` call for
    cols/rows AND the origin, stores it and passes it to `render_set_origin()`; `mouse_cell()` and
    `sel_cell()` subtract `W.gx/W.gy` before dividing (and clamp, so a pointer in the margin hits the
    first/last cell); the initial window is asked for `cols*cell_w + 2*padding` x `rows*cell_h +
    2*padding`; the `grid resized` debug line gained `padding %d` and `unused %d x %d px`.
  * `src/render.c` — `set_clear_color()` (the configured background premultiplied by `Config.opacity`, the
    same rule the shader uses), called from `render_init` AND every `render_draw`; `render_draw` anchors
    the quads at `r->gx/r->gy` with the remainder pushed to the bottom/right.
  * `tests/test_vt.c` — the geometry block: the 936x1002/8x17 case with pad 0 vs pad 8, a sweep of
    window/cell/padding combinations checking that the cell the pointer formula names is the cell the
    renderer draws there, plus the degenerate cases (pad larger than the window -> still a 1x1 grid, a
    1x1 window, a negative pad).
  FOUND BY THIS WORK AND FIXED IN THE SAME SESSION: the margin was cleared with `DEF_BG`, not the
  configured `background` (measured then: a `background = 0a0a2e` window with `padding = 20` had a 20 px
  `(16,16,20)` band on all four sides). That is the `Render.bg`/`set_clear_color()`/per-frame `glClear`
  part above; session 55 re-checked a recorded frame (ZERO `(16,16,20)` px, the whole window bbox exactly
  `(10,10,46)`) and the user had already confirmed it in their window. ISSUES.md "Fixed".
  NOT DOCUMENTED AS A BUG, ON THE USER'S RULING (session 55): one measurement showed blank CELLS at
  `DEF_BG` while the margin was right (`src/vt.c` hardcodes `DEF_BG` in `row_new_cols`, `row_resize`,
  `grid_reflow`'s fresh rows and the `grid_resize_rows` blanks; only main.c paints `conf.bg`). The user
  says that is working as intended — do not re-open it, and do not go looking for it again.
  VERIFIED: `make` 0 warnings, `make test` 364/0 + MODEL OK + CLI 77/0, `make asan` 364/0 clean (session
  54 and again session 55).

- 2026-10-15 (session 55, the user CONFIRMED padding works; asked for a "what is left" assessment, then
  for "Cleanup + commit", and told me to STOP the visual/pixel testing — they verify the window themselves).
  JOURNAL GAP FOUND AND FIXED: the session-54 padding work had NO Progress entry although Current Status
  pointed at one; written above from the diff.
  BASELINE RE-MEASURED (nothing was trusted from the journal): `make clean && make` exit 0 / 0 warnings
  (/tmp/s55_build.log); `make test` -> `364 checks, 0 failures` + `MODEL OK (0 problems)` + `CLI: 77
  checks, 0 failures, 0 skipped` (/tmp/s55_test.log); `make asan` -> `364 checks, 0 failures` + `MODEL OK`,
  no LeakSanitizer/UBSan report (/tmp/s55_asan.log). Before this session's doc edits `git status` showed 12
  modified files + untracked `tests/cli_test.sh`: sessions 52-54 were NOT committed.
  DOC ACCURACY PASS (what was stale, and where it was fixed):
  * ISSUES.md — Open #4 (the `DEF_BG` band) moved to "Fixed": it had been left there as "NOT VERIFIED"
    only because session 54's compositor stopped honouring focus/raise; the user has since confirmed the
    padding in their window and a lossless frame re-check agrees. Open #0 (no CLI coverage) moved to
    "Fixed" — `tests/cli_test.sh` + `make test-cli` are in the tree and `make test` runs them, so the
    "Fix in progress" wording was simply behind reality. The remaining Open items keep their numbers 1-3.
    The "leftover pixels at the bottom/right" limitation was reworded: those pixels are the CONFIGURED
    background (they used to be `DEF_BG`), and the bottom margin is `padding` + the remainder.
  * docs/ARCHITECTURE.md — Modules table: `tests/test_vt.c` 323 -> 364 checks and the new
    viewport-height-resize coverage, `tests/dbg.c` now mentions the zoom stages, and a `tests/cli_test.sh`
    row was added (it had no entry at all). The `-d` log-line list now shows the `padding P` field that
    `grid resized` actually prints (verified against a real run's output).
  * README.md — the target table now lists `make test-cli` and describes `make test` correctly (it runs the
    unit tests, the model checker AND the CLI tests). Flags table and the 18 settings rows were re-checked
    against `src/main.c`'s `usage()` and `src/config.c`'s `apply()` — they already matched, all 18 keys.
  * AGENT.md — Current Status re-dated to session 55 with the session-55 logs and the padding item marked
    user-confirmed; Current Task rewritten (the session-53 zoom root-cause retelling and the session-52
    #0/#1/#2 plan were dropped: #0 is done, the zoom cause lives in ISSUES.md "Fixed", and duplicating it
    here is exactly what the journal rules forbid); the padding Progress entry added; Next Steps, Project
    Files and History updated; the stale "323 checks" in the Project Files tree fixed.
  ENVIRONMENT FACT (new, cost me time): the exported `NIRI_SOCKET` in this shell is STALE
  (`niri.wayland-1.2298001.sock`); the live one is `/run/user/1000/niri.wayland-1.<pid>.sock` — find it
  with `ls /run/user/1000/niri.*.sock` and export it, otherwise every `niri msg` dies with "error
  connecting to the niri socket". With that fixed the compositor is cooperative again (`set-window-height
  --id N` really resized a k-term window: `grid resized to 125x55` -> `125x38`, `configure answered` for
  both). All test windows were closed by id; no k-term process left.
  MY OWN MISTAKE THIS SESSION, recorded because it damaged this file (and it is the SECOND time the same
  class of accident has happened — see the Key Decisions pitfall): a python block-move meant to relocate
  the stranded sessions 52-55 Progress entries under `### Progress` used `s.index("## Next Steps")` and
  `s.index("## Environment (verified 2026-10-12")` as anchors, but BOTH strings also occur inside the
  session-54 entry, which quotes them while describing session 54's own splice accident. Result: the block
  was cut mid-entry and the Environment heading was glued onto the end of a quoted sentence. REPAIRED by
  moving the stranded tail back and restoring the heading (pre-repair copy kept at
  `/tmp/s55/AGENT.md.before_repair`), then verified structurally: `grep -n "^## \|^### \|^- 2026-10-15
  (session 5[2-5]" AGENT.md` now shows Progress (33-55, chronological) -> Environment -> History -> Next
  Steps -> Project Files -> Key Decisions, with the session-54 entry whole again.
  ALSO FIXED (structure, not content): the sessions 52-55 Progress entries had been sitting AFTER `##
  History` since session 48's reordering, so the journal read as if they were history entries; they now
  live under `### Progress` in chronological order, and History gained one era line for sessions 52-55.
  DONE (commit + push): `git fetch` first (the user commits on GitHub too) -> `origin/main` and `main`
  were level (0/0), so no rebase was needed. `git add -A` + one commit `19c8bd3 "Zoom fix, window padding,
  CLI test suite; docs synced"` (13 files: the session 52-54 source/test work plus this session's doc
  pass; `tests/cli_test.sh` added and made executable). PUSHED: `b451d87..19c8bd3 main -> main`, and
  `git status -sb` shows `## main...origin/main` with no ahead/behind and a clean tree.
  VERIFIED IMMEDIATELY BEFORE THE COMMIT (docs-only changes, but re-measured anyway): `make clean && make`
  exit 0 / 0 warnings (/tmp/s55_b2.log); `make test` -> `364 checks, 0 failures` + `MODEL OK (0 problems)`
  + `CLI: 77 checks, 0 failures, 0 skipped` (/tmp/s55_t2.log); `make asan` -> `364 checks, 0 failures` +
  `MODEL OK (0 problems)`, no sanitizer report (/tmp/s55_a2.log).
  NEXT ACTION: ISSUES.md Open #1 — the second-instance `atlas glTexImage2D failed: 0x505` + SIGSEGV, the
  only crash left. Work from the source and a headless harness, NOT screen recording (the user's standing
  instruction: they verify anything visual themselves). Open #2 (`eglSwapBuffers failed: 0x3000` on
  launch) is the smaller one after that.

- 2026-10-15 (session 56, MULTIPLICING started — plan recorded, baseline measured, no file changed yet).
  REQUEST: "We are going to add multiplexing functionality to K-Term. The key-binding should be
  configurable in the config file."
  READ FIRST: AGENT.md, ISSUES.md, README.md, include/k-term/k-term.h, src/config.c, src/main.c,
  src/pty.c, Makefile, tests/cli_test.sh + the head/tail of tests/test_vt.c.
  INTERPRETATION (written so a resumed session does not have to guess): multiplexing = several terminal
  sessions (tabs) in ONE K-Term window, each with its own `Term` + PTY + scrollback + selection, one
  visible at a time, switched by config-file keybindings. Splitting the window into panes is NOT in this
  task (it would need a renderer change); the session abstraction built here is what makes it possible
  later.
  WHY THE CURRENT CODE CANNOT JUST GROW A SECOND SESSION: `src/main.c` keeps exactly one `static Term
  term` and one `static Pty pty`, and ~150 places read them (input handlers, selection, scrollback,
  zoom, clipboard, the render call, the pty read branch, SIGCHLD). The plan is to make those names mean
  "the ACTIVE tab" and to give every tab its own PTY fd in epoll, with an fd -> tab lookup and a SIGCHLD
  path that reaps by pid and closes the tab that owned that child.
  FACTS CHECKED IN src/vt.c that constrain the design: `term_parse()` sets `ps->t = t` on every call
  (vt.c:1604), so a `Term` that is MOVED in memory repairs its own parser back-pointer on the next
  parse — but nothing else may read `ps->t` before that, so a tab array that shifts on close must fix
  `ps.t` explicitly. `term_init()` memsets the whole Term and `term_destroy()` frees both grids, so a
  tab must be initialized exactly once and destroyed exactly once (the `GRID_VALID` sentinel is what
  makes a double free detectable).
  BASELINE (measured, not trusted): `make clean && make` exit 0, 0 warnings (`/tmp/s56_base_build.log`);
  `make test` -> `364 checks, 0 failures` + `MODEL OK (0 problems)` + `CLI: 77 checks, 0 failures, 0
  skipped` (`/tmp/s56_base_test.log`). `git status --short` clean, `main` == `origin/main` at `0437537`.
  NEXT ACTION: write `src/mux.c` (tab bookkeeping + the key-binding spec parser) and the `Mux`/`Tab`/
  `KeyBind` types in include/k-term/k-term.h, then the config keys, then main.c, then tests.

  STEP 1 DONE — include/k-term/k-term.h + new src/mux.c (not built yet, so nothing is claimed to work):
  * Header: `DEF_TITLE "K-Term"` (single-sourced like DEF_APP_ID, because mux_title() now builds the
    title from it); a "configurable key bindings" section with the `KB_CTRL/KB_SHIFT/KB_ALT/KB_SUPER`
    modifier bits, a `KB_KEY_*` enum carrying the standard X keysym numbers for the named keys
    (arrows, Home/End/Prior/Next/Insert/Delete/BackSpace/Tab/Return/Escape/Space/F1-F12), the
    `KeyBind {sym, mods, set}` type and the `keybind_parse()` / `keymods_parse()` prototypes; new
    `Config` fields `max_tabs`, `key_new_tab`, `key_close_tab`, `key_next_tab`, `key_prev_tab`,
    `tab_prefix_mods`; and a "multiplexing" section after the pty section (it must come after both
    `Term` and `Pty` because `Tab` contains them) with `TAB_MAX` 64, `Tab {Term, Pty, attention}`,
    `Mux {tab[], n, active, max}` and the mux_* prototypes.
  * `src/mux.c` — the tab bookkeeping only, no Wayland/EGL/fd/fork, so tests/test_vt.c can link it:
    `mux_init` (clamps max_tabs to 1..TAB_MAX), `mux_open` (term_init + `pty.mfd = -1`, makes the new
    tab visible, -1 at the limit), `mux_close` (term_destroy, shifts the later tabs down to keep the
    array DENSE, zeroes the vacated slot so a grid pointer cannot be freed twice, and RE-ARMS
    `tab[i].term.ps.t` for every moved tab — vt.c's parser reads that self-pointer), `mux_goto`
    (clamped), `mux_next`/`mux_prev` (wrap-around), `mux_cur`/`mux_count`/`mux_active`, `mux_touch`
    (attention on a hidden tab) and `mux_title` (one tab -> `K-Term` exactly, i.e. no visible change
    for a single-session user; several -> `K-Term [2/3]`, with `!N` when hidden tabs have unread
    output — the title is the only place the tab state can be seen, there is no tab bar).
  DECISIONS: the tab array is dense (index == the number the user counts); `mux_open` switches to the
  new tab; a binding spec with NO modifier is refused by keybind_parse (an unmodified binding would
  swallow plain typing); `key_xxx = none` unbinds an action.
  NEXT ACTION: the config keys + `keybind_parse`/`keymods_parse` in src/config.c.

  STEP 2 DONE — src/config.c + Makefile + tests/test_vt.c, VERIFIED: `keybind_parse()` /
  `keymods_parse()` (split on '+', case-insensitive, modifier names ctrl/control/shift/alt/mod1/
  super/mod4/win/gui, named keys BackSpace Tab Return/Enter Escape/Esc space Left Right Up Down Home
  End Page_Up/PageUp/Prior Page_Down/PageDown/Next Insert Delete F1-F12, and any single printable
  character as itself with letters folded to lowercase), `set_keybind()` (`none`/`off`/`no`/`0`/empty
  unbinds; an unparseable spec keeps the current binding and warns), the six new config keys
  (`max_tabs` clamped 1..TAB_MAX, `key_new_tab`, `key_close_tab`, `key_next_tab`, `key_prev_tab`,
  `key_tab_prefix`) and the defaults (Ctrl+Shift+T / W / Right / Left, prefix Ctrl+Shift, 8 tabs).
  Makefile: `src/mux.c` added to SRCS and to all four test rules (bin/test_vt, bin/dbg and both
  sanitizer rules — the stale-binary pitfall says every source a test binary compiles must be a
  prerequisite).
  VERIFIED: `make` exit 0, 0 warnings (`/tmp/s56_b1.log`); `./bin/test_vt` -> `434 checks, 0 failures`
  (364 -> 434, 70 new: 34 mux + 16 keybind/keymods + 20 config-file). NEGATIVE TEST for the mux
  ps.t re-arm: the same tests built against a mux.c with that line deleted ->
  `434 checks, 2 failures`, one of them exactly "mux: a moved tab's parser points at its new home".
  (The sed also left the `for` without a body, so it swallowed the `n == 0` branch and produced a
  second failure — the harness was crude, the ps.t result is still valid.)
  NEXT ACTION: src/main.c — the single `term`/`pty` globals become the active tab's, per-tab PTY fds
  in epoll, SIGCHLD reaps by pid, tab_open/tab_close/tab_switch, the keybindings in send_key, the
  window title, teardown.

  STEP 3 DONE — src/main.c wired to the mux, VERIFIED (build + full test suite). The rename was done
  with sed first (`term.` -> `cur_term()->`, `&term` -> `cur_term()`, same for pty; `k-term` protected
  by a placeholder so the #include and the message strings survived — a plain `\bterm\b` sed would
  have rewritten them), then hand-edited. What changed: (continues below)
  * `static Term term; static Pty pty;` -> `static Mux mux;` plus `cur_term()`/`cur_pty()`, which
    resolve through `mux_cur()` on every call, so there is no second copy of "which tab is visible"
    to keep in sync. They return NULL only after the last tab closes; `redraw()` and
    `term_size_changed()` check for that, everything else stops with W.running.
  * `term_size_changed()` resizes EVERY tab (they share the window) and `pty_setsize`s each one so
    every shell gets SIGWINCH; the `grid resized` debug line gained `tab i/n`.
  * `scrollback_nav(Term *, int)` and `snap_to_live(Term *)` are per session — scrolling one tab must
    not move another's view. New `paint_blank_cells(Term *)` replaces the two duplicated colour loops
    (startup, zoom) and is also what initializes a new tab.
  * New section "multiplexing: the sessions of this window": `update_title()` (xdg_toplevel_set_title
    from mux_title), `norm_sym()` + `bind_match()` (letters folded to lowercase, modifier set must
    match EXACTLY), `tab_for_fd()` / `tab_for_child()` (epoll reports only an fd; SIGCHLD only a pid),
    `tab_spawn()` (pty_spawn + epoll_ctl, shared by the first tab and every later one), `tab_open()`,
    `tab_close()` (captures the Pty BEFORE mux_close shifts the array, drops the fd from epoll, closes
    it, SIGHUPs the child, and sets W.running = 0 when the last tab goes) and `tab_switch()`.
  * `send_key()` matches the five bindings FIRST (before the built-in Ctrl+Shift+C/V, because the tab
    actions are the configurable ones — documented in the comment), computes the effective modifier
    mask as the four KB_* bits, and returns 0 for all of them so a held key cannot repeat a tab
    action. `key_tab_prefix` + a digit 1-9 jumps to that tab.
  * Event loop: the pty branch is now `tab_for_fd(fd)` and works on that tab; output on a HIDDEN tab
    does not redraw and does not snap its scroll — it calls `mux_touch()` and updates the title once
    (mux_touch now returns 1 only when it newly set the flag). A pty that reaches EOF closes that tab
    instead of exiting the app. SIGCHLD reaps with `waitpid(-1, …, WNOHANG)` in a loop, matches the
    pid to a tab, zeroes `pty.child` (so tab_close does not kill a recycled pid) and closes the tab.
  * main(): `mux_init(conf.max_tabs)` + `mux_open()` before the window, `g_cmd`/`g_wdir` remembered
    for later tabs, the epoll fd created BEFORE the first `tab_spawn()` (it registers the descriptor),
    and teardown loops over every session (close master, SIGHUP + WNOHANG reap, term_destroy).
  * `-d` now also covers the diagnostics config_load() prints: `k_term_loglevel` is raised to DEBUG
    before config_load when -d was given, then settled on the final level (the CLI still beats the
    file). Found because the new CLI check for `config: key_close_tab unbound` saw nothing — the
    message is L_INFO and the level used to be applied only after the file had been read.
  * tests/cli_test.sh: the "-e COMMAND ends on its own" expectation follows the new wording
    (`mux: the pty of tab 1/1 closed` / `mux: the shell of tab 1/1 exited`), and 4 new checks cover
    the multiplexing config keys (no `unknown key`, `key_close_tab = none` logs the unbind, `mux:
    session 1/1 started` is logged, exit 0).
  VERIFIED: `make clean && make` exit 0, 0 warnings (`/tmp/s56_b2.log`, again `/tmp/s56_b3.log`);
  `make test` -> `436 checks, 0 failures` + `MODEL OK (0 problems)` + `CLI: 81 checks, 0 failures,
  0 skipped` (`/tmp/s56_t5.log`). Two test bugs of my own on the way: `scroll_apply` still called the
  old one-argument `scrollback_nav` (compiler caught it), and a mux check assumed the wrong active tab.
  NEXT ACTION: `make asan`; then the live end-to-end check with injected Ctrl+Shift+T / Right / Left /
  W against `./bin/k-term -d` (the log lines are the evidence, no screen recording).

- 2026-10-15 (session 57, TAB BAR + SIDE-BY-SIDE VIEW started — plan recorded, baseline measured, no file
  changed yet). REQUEST: "We added Multiplexing to K-Term. I'd like it that when we have tabs to have a
  visual indication showing the different tabs with the ability to click on them to swap between them.
  Also it would be nice to be able to swap between Tab and Side by Side view."
  READ FIRST: AGENT.md, ISSUES.md, include/k-term/k-term.h, src/mux.c, src/main.c (all of it),
  src/render.c (all of it), src/config.c, src/vt.c's term_grid_geometry, Makefile, tests/test_vt.c's
  mux/config blocks.
  WHY THE BAR IS NOT JUST A MAIN.C JOB: render.c draws exactly one `Term` grid per frame, anchored at
  `Render.gx/gy`, and main.c maps the pointer with the same single origin. A bar and side-by-side panes
  need N rectangles with N origins, so the geometry becomes its own concern. DECISION: a new pure module
  `src/layout.c` computes everything (bar strip, item rects + labels, pane rects + the cols/rows that fit
  in each), main.c resizes each displayed session to its pane and hit-tests the pointer through it, and
  render.c only draws what the Layout says. That keeps the whole feature unit-testable headlessly, which is
  the only kind of test this environment can trust (the user verifies anything visual themselves).
  BASELINE (measured, not trusted from the journal): `make clean && make` exit 0, 0 warnings
  (`/tmp/s57_base_build.log`); `make test` -> `436 checks, 0 failures` + `MODEL OK (0 problems)` +
  `CLI: 81 checks, 0 failures, 0 skipped` (`/tmp/s57_base_test.log`). `git status --short`: AGENT.md,
  Makefile, include/k-term/k-term.h, src/config.c, src/main.c, tests/cli_test.sh, tests/test_vt.c modified
  + `src/mux.c` untracked (session 56's multiplexing work, not committed yet); `main` == `origin/main`
  at `0437537`.
  NEXT ACTION: the layout types in k-term.h + src/layout.c, then its tests, then render.c, config.c,
  main.c.

  STEP 1 DONE — include/k-term/k-term.h (not built yet, so nothing is claimed to work): the layout
  section (LAYOUT_TABS/LAYOUT_SPLIT, BAR_OFF/BAR_AUTO/BAR_ALWAYS, BAR_LABEL_MAX 40, PaneGeom
  {x,y,w,h,cols,rows,tab}, BarItem {x,w,tab,active,attention,label}, Layout {mode,bar_on,bar_*,item[],
  npane,pane[]}, TabInfo {tab,attention,title}, LayoutReq, layout_compute + the three hit-test
  prototypes), the three new Config fields (tab_bar, split_max, key_toggle_layout), and the renderer API
  change: `render_draw(Render *, Font *, const Layout *, Term *const *terms)` replaces
  `render_draw(Render *, Term *, Font *)`, `Render.gx/gy` and `render_set_origin()` are GONE (the layout
  carries a per-pane origin, so one global offset no longer exists). `terms` is indexed by TAB index, not
  by pane index — chosen because PaneGeom.tab is the mux index, so the renderer cannot pair a pane with
  the wrong session by an off-by-one in main.c.
  NEXT ACTION: src/layout.c + the Makefile + layout checks in tests/test_vt.c.

  STEP 2a DONE — new src/layout.c (written, not compiled yet): layout_compute() + the three hit tests.
  Rules it implements, each to be pinned by a check: Config.padding is the margin around everything and
  with the bar off + one pane the numbers equal what term_grid_geometry() used to return for the whole
  window (so a single-session window cannot change shape); the bar is one cell row tall inside the top
  margin with `gap` px below it; leftover px stay at the right/bottom of their pane; if the bar would
  leave no room for one text row the BAR is dropped, not the grid; split mode shows at most split_max
  panes and the shown window of tabs slides to contain the active one (clamped at both ends); labels are
  "<n> <title>" with a leading * for unread output, `shell` when the session has no OSC title, cut to the
  item's column count at a UTF-8 codepoint edge (clip_utf8 counts lead bytes only).
  NEXT ACTION: the Makefile (src/layout.c in SRCS + all four test rules) and the layout checks.

  STEP 2b DONE — Makefile (src/layout.c in SRCS and in all four test-binary rules, per the stale-binary
  pitfall) + 38 layout checks in tests/test_vt.c (a `lreq()` builder and a `utf8_len()` helper above main).
  VERIFIED: `make bin/test_vt` 0 warnings -> `474 checks, 0 failures` (436 -> 474). Two of my own test bugs
  on the way, both caught immediately: a new `int cols, rows, xoff, yoff` shadowed the ones at line 523 of
  the same one-big-main() (-Wshadow, renamed to gcols/grows/gxoff/gyoff), and `"a\xc3\xa9b"` is ONE greedy
  hex escape (0xa9b, "hex escape sequence out of range") — C string concatenation `"a\xc3\xa9" "b"` is what
  was meant. PITFALL: in this test file every new local must be checked against the outer ones (main is one
  ~1500-line function) and any \x escape followed by a hex digit needs string splitting.
  What the checks pin: one pane with the bar off == the old term_grid_geometry numbers; BAR_AUTO shows the
  bar from the second session; the bar costs exactly one text row and keeps the bottom margin; equal-width
  items with the remainder at the right; hit tests (bar item / pane / gap / margin / bar-is-not-a-pane);
  labels ("1 shell", "1 *shell", "2 vim", UTF-8 cut at a codepoint edge, never wider than the item);
  split panes equal width, non-overlapping, each grid fitting its rect; the pane window sliding with the
  active tab (start/end clamped); tab mode giving a pane only to the visible session; the bar dropped
  rather than the text row in a 30x30 window; no sessions -> no pane and no bar; padding 0 -> the old
  edge-to-edge layout; negative pad/gap treated as 0.
  NEXT ACTION: src/render.c — draw the bar + N panes in one draw call.

  STEP 3a DONE (header follow-ups, still nothing compiled): `Layout.active_tab` added (the session the
  keyboard talks to — the renderer needs it to highlight the right item and to border the right pane;
  the bar items' `active` flag is not enough because with tab_bar = off there are no items), and
  `render_init(..., uint32_t bg, uint32_t fg)` + `Render.fg`: the bar's text/separator colours are mixed
  from the CONFIGURED palette, never from a Term's pen, because a Term's pen is the application's current
  SGR pen and an application must not be able to repaint the chrome that names its tab.
  NEXT ACTION: src/render.c — draw the bar + N panes in one draw call.

  STEP 3b DONE — src/render.c rewritten around the layout (builds clean: `make build/render.o` 0 warnings;
  the full `make` still fails because main.c has not been wired yet, which is expected at this point).
  What changed: ensure_vdata/ensure_edata now take a QUAD COUNT (the caller counts: 3 per grid cell of
  every pane + BAR_LABEL_MAX+2 per bar item + 4 for the border) instead of rows*cols; render_init takes
  the configured fg; render_draw(Render*, Font*, const Layout*, Term *const *terms) draws the whole frame:
  a PaneDraw context per pane (pane_prepare: the grid clamped to what the pane can hold, its origin, its
  selection span), then the same three back-to-front passes over EVERY pane, then the bar (item
  backgrounds + a 1 px separator per item + the label glyphs decoded from UTF-8 by a local next_cp()), then
  the 2 px focus border around the active pane (split mode only) emitted last. Chrome colours are mixed
  from Render.bg/Render.fg by mix_c() (active item = SEL_BG, inactive = bg+10% fg with 45% dimmed text,
  separator = fg-65% toward bg, an inactive pane's cursor = CURSOR_COL 55% toward bg) so an application
  cannot repaint the chrome. The selection band edges stay ABSOLUTE buffer px, which is safe across panes
  because panes never overlap in x (checked in tests) — the shader compares the fragment's absolute x.
  The whole-cell remainder is now per pane (pad_gl = vh - pane.y - nrows*ch), which is what keeps
  main.c's `row = (ptr_y - pane.y)/cell_h` exact inside every pane.
  JOURNAL PITFALL HIT AGAIN (my own, third time for this class): the anchor line "NEXT ACTION: src/render.c
  — draw the bar + N panes in one draw call." existed TWICE (end of the STEP 2b and of the STEP 3a block),
  so this entry was first inserted after STEP 2b, out of order; moved below STEP 3a. Rule from the Key
  Decisions section applies to these step blocks too: anchor on text that is unique in the file.
  NEXT ACTION: src/config.c (tab_bar / split_max / key_toggle_layout), then src/main.c.

- 2026-10-16 (session 59, RENAME K-Term -> Kai-Term — COMPLETE: code, build files, file layout and docs;
  the four things outside this project that still say `K-Term` are listed under Current Task for the user).
  Steps 1-4 are the code and build files, step 5 the docs, step 6 the user's own config; each is recorded
  below in order. WHY: the user asked for it ("rename K-Term to Kai-Term to avoid conflict with another
  project I found"); the other project is not identified.
  BASELINE MEASURED FIRST (the tree does NOT fully build — session 57 stopped with src/main.c not yet wired
  to the new render_draw/render_init signatures): `make -k` -> 5 errors, ALL in src/main.c
  (`render_set_origin` implicit at :213, `render_draw` argument 2/3 + too few arguments at :329, `render_init`
  too few arguments at :1455), 0 warnings anywhere else, 7 of 8 objects build (main.o is the only failure);
  `bin/test_vt` -> `486 checks, 0 failures`, `bin/dbg` -> `MODEL OK (0 problems)`
  (`/tmp/s59_base_build.log`, `/tmp/s59_base_errs.txt`). That baseline is what the rename is measured
  against — a rename on a tree that does not link cannot be verified by `make` alone.
  DONE (step 2, text): sed over src/*.c, include/k-term/k-term.h, tests/dbg.c, tests/test_vt.c,
  tests/cli_test.sh, Makefile, .gitignore — `K_TERM_H`->`KAI_TERM_H`, `k_term_loglevel`->`kai_term_loglevel`,
  `K-Term`->`Kai-Term`, `k-term`->`kai-term`. The user-visible strings that changed, all verified by grep
  afterwards: `DEF_APP_ID`/`DEF_TITLE` = "Kai-Term", the log prefix `kai-term <level>:` (src/vt.c:26),
  `kai-term: unknown option` / `kai-term: <flag> needs an argument` (src/main.c), the `-v` line
  `kai-term 0.1.0 (prototype)`, `kai-term exiting`, `TERM_PROGRAM=kai-term` (src/pty.c:72), the config path
  `$XDG_CONFIG_HOME/kai-term/kai-term.conf` / `~/.config/kai-term/kai-term.conf` (src/config.c:388,390),
  the `-c` usage line, and the Makefile's `-Iinclude/kai-term`, `HDRS` and `BIN := bin/kai-term`.
  LEFT ALONE ON PURPOSE: `/tmp/kterm_test_config*.conf` and the `io.github.kaida.kterm` app_id in
  tests/test_vt.c — arbitrary test fixtures, not the product name.
  DONE (step 3, files): `git mv include/k-term/k-term.h include/kai-term/kai-term.h` (git records it as
  `RM`, so the history follows the file), `rmdir include/k-term`, then `mv K-Term Kai-Term` for the project
  directory itself; `git status -sb` from the new path still shows `## main...origin/main` and the same log.
  VERIFIED (step 4): `make clean && make -k` -> the SAME 5 errors, byte-for-byte identical to the baseline
  (`diff /tmp/s59_base_errs.txt /tmp/s59_after_errs.txt` -> no output), still 0 warnings, still 7 of 8
  objects; `bin/test_vt` -> `486 checks, 0 failures` (unchanged) and `bin/dbg` -> `MODEL OK`
  (`/tmp/s59_after_build.log`, `/tmp/s59_after_errs.txt`). The renamed strings are in the compiled objects:
  `strings build/config.o` shows `%s/.config/kai-term/kai-term.conf` + `Kai-Term`, `build/vt.o` shows
  `kai-term %s: `, `build/pty.o` shows `kai-term`, `build/mux.o` shows `Kai-Term`.
  NEGATIVE TEST (proves the renamed literals are pinned, not just pasted): `DEF_APP_ID`/`DEF_TITLE` put back
  to "K-Term" -> `486 checks, 6 failures`, and the 6 are exactly the app_id-default, app_id-fallback and
  mux_title checks ("config: app_id defaults to Kai-Term", "mux: the title says which tab of how many", …);
  restored -> `486 checks, 0 failures`.
  NOT VERIFIABLE YET, and this is the honest limit of this step: `make test-cli` and any live check of the
  binary (app_id in the compositor, the config path actually being read, the `k-term:`-style CLI messages)
  need `bin/kai-term` to LINK, and it does not link for the pre-existing session-57 reason above. The
  expectations in tests/cli_test.sh were renamed (`^kai-term `, `kai-term: unknown option '<flag>'`,
  `kai-term: <flag> needs an argument`, `kai-term exiting`, default `./bin/kai-term`) but are UNTESTED until
  main.c is wired. Do not mark the CLI surface verified until someone runs `make test-cli`.
  DONE (step 5, docs): README.md and docs/ARCHITECTURE.md renamed throughout (0 old-name hits left in
  either — they are current-state user documentation). ISSUES.md: title + "Open" + "Known limitations"
  renamed, and the two verbatim user-reported output blocks under "Open" put BACK to the old spelling with a
  parenthetical, because they are what the user actually saw (`k-term error: atlas glTexImage2D failed:
  0x505 …`); its "Fixed" section is left as written. AGENT.md: title, Project Overview, the OPEN WORK block,
  Environment, Next Steps, Project Files and Key Decisions renamed; the dated Progress and History entries,
  the quoted user sentences ("I confirm the padding to be working properly in K-Term", …), the GitHub URL
  (the repo is not renamed yet) and the session-42 note that the pcmanfm-qt window's title was literally
  "K-Term" are all kept as written. Each of the three docs now carries one note saying the rename happened on
  2026-10-16 and that older entries keep the old spelling — that is what makes the set consistent without
  rewriting history.
  HAZARD CLOSED: the paused session-56/57 task blocks still name `include/k-term/k-term.h` as the file to
  edit, so a resumed session could create the old path. A translation note was added under "PAUSED"
  (`include/k-term/k-term.h` -> `include/kai-term/kai-term.h`, `bin/k-term` -> `bin/kai-term`,
  `#include "kai-term.h"`).
  FIXED WHILE IN THERE (stale, not name-related, all verified numbers): the Project Files tree said
  `src/ config.c vt.c pty.c font.c render.c main.c` (mux.c and layout.c existed but were unlisted),
  `tests/test_vt.c 364 checks` (it is 486), and `tests/cli_test.sh 77 checks` (81 since session 56). The tree
  now lists both new modules, says 486, and marks cli_test.sh as NOT RUNNABLE since session 57 with its last
  verified run named. `src/mux.c` and `src/layout.c` gained one-line descriptions.
  DONE (step 6, outside the project): `~/.config/kai-term/` created and the user's two config files COPIED
  there (`kai-term.conf`, `desktop-terminal.conf`); `~/.config/k-term/` left untouched so nothing is lost and
  the old installed binary keeps working. The three root-owned items (niri rule, /usr/local/bin/k-term,
  /usr/local/bin/desktop-terminal + its .desktop entry) are listed under Current Task for the user — `sudo`
  asks for a password here, so they were not touched.
  FINAL RE-VERIFICATION after every doc edit: `make clean && make -k` -> the SAME 5 pre-existing main.c
  errors, identical to the baseline, 0 warnings (`/tmp/s59_final_build.log`, `/tmp/s59_final_errs.txt`);
  `bin/test_vt` -> `486 checks, 0 failures`; `bin/dbg` -> `MODEL OK (0 problems)`; `make asan` -> exit 0,
  `486 checks, 0 failures` + `MODEL OK`, no LeakSanitizer/UBSan report (`/tmp/s59_final_asan.log`) — that one
  matters because the sanitizer rules compile the sources directly and depend on `$(HDRS)`, whose path the
  rename changed. `find include -type f` -> only `include/kai-term/kai-term.h`; `grep -rn
  "include/k-term|k-term\.h"` over the project -> hits ONLY in dated journal entries and in this rename
  entry. `make test-cli` fails at `build/main.o` (the pre-existing link failure), not on a path.
  STILL NOT VERIFIED (unchanged from before the rename, and it is the tree's real problem, not the rename's):
  `bin/kai-term` does not link, so `make test`, `make run`, the CLI suite and every live check (app id in the
  compositor, the config path actually being read, `TERM_PROGRAM`, the CLI messages) are blocked on session
  57's unfinished main.c wiring.
  NEXT ACTION: session 57's step 5 — wire src/main.c to `layout_apply()` + the new
  `render_init`/`render_draw`, which is what unblocks `make test`/`make run`/`make test-cli` again. The
  fullscreen freeze (ISSUES.md Open #4) is deferred by the user and is NOT the next task; it also cannot be
  investigated until that wiring lands, because the binary does not link. Then ask the user about the four
  follow-ups listed under Current Task.

- 2026-10-16 (session 59, follow-up in the same session — the fullscreen freeze reclassified, then the git
  side of the rename).
  NEW INFORMATION FROM THE USER, recorded verbatim where it matters: the fullscreen freeze is "intermittent
  and doesn't always reproduce", so "let's put a note on that and see if we can fix it later". The user also
  took back items 1-3 of the rename follow-ups (the niri `match app-id="K-Term"` rule, the installed
  `/usr/local/bin/k-term`, and `/usr/local/bin/desktop-terminal` + its .desktop entry) — they are handling
  those themselves, so do not edit them.
  DONE: ISSUES.md gained Open #4 carrying the intermittency, the fact that it cannot be reproduced at all
  while `bin/kai-term` does not link, the two mechanisms from session 46 that would each produce a
  timing-dependent freeze, the warning that the session-57/58 layout rewiring moves the draw/commit path so
  the frame-pacing rules must be re-checked rather than assumed to have survived, and the concrete levers
  (`-d`: `toplevel state: fullscreen 1` must be followed by `configure answered: buffer <output size>
  attached`; a `swap failed (N in a row)` streak; `/proc/<pid>/wchan` = `ep_poll` vs `futex_do_wait`; and
  running the fullscreen action repeatedly and RECORDING THE RATE, because one clean run proves nothing for
  an intermittent bug). The session-45-47 entry in "Fixed" now says it is a record of the root cause and the
  rules, not a claim that fullscreen is freeze-free, and points at Open #4. AGENT.md's Current Task no longer
  calls it BLOCKING.
  NOT DONE, deliberately: no investigation. Nothing was run — the binary does not link, and the user deferred
  it.

- 2026-10-16 (session 60, STEP 1 DONE — `src/main.c` wired to the layout; `bin/kai-term` LINKS again, so
  `make test`, `make test-cli`, `make run` and every live check are unblocked).
  WHAT CHANGED (src/main.c unless said otherwise):
  * `term_size_changed()` is GONE, replaced by `layout_apply()`: it fills a `LayoutReq` from the window px,
    the cell px, `Config.padding`, `PANE_GAP`, the mode, the session list (index, attention flag,
    `Term.title`) and `Config.split_max`/`tab_bar`, calls `layout_compute()`, then resizes EVERY session to
    `layout_grid_size()` — the size a pane of this layout has, drawn or not — and finally sets `W.gx/W.gy` to
    the origin of the pane the VISIBLE session is drawn in, which is what `sel_cell()`/`mouse_cell()` map the
    pointer through. Called from `apply_pending_resize()`, `zoom_apply()`, `tab_open()`, `tab_close()`,
    `tab_switch()`, `redraw()` (the bar labels and unread marks are part of the picture) and after
    `wayland_setup()`.
  * New file-scope state: `static Layout g_layout;` and `static int layout_mode` (LAYOUT_TABS/LAYOUT_SPLIT),
    plus `#define PANE_GAP 8` — the px between panes and between the bar and the grid. NOT a config key on
    purpose: `padding` already sets the margin around everything, and a gap of 0 would let one pane's focus
    border draw over its neighbour.
  * `redraw()` builds `Term *terms[TAB_MAX]` indexed by TAB index (like `PaneGeom.tab`) and calls
    `render_draw(&rend, &font, &g_layout, terms)`; `wayland_setup()` passes `conf.fg` to `render_init()`, so
    the chrome colours come from the configured palette.
  * Pointer: a press in the bar switches sessions (left button) or closes one (middle button) and never
    reaches the application or starts a selection — checked BEFORE the mouse-reporting branch, so an
    application that grabbed the mouse cannot eat the bar. A press in a pane that is not the visible session
    focuses that session first. `cur_term()` is re-checked after a middle-click close, because closing the
    last session makes it NULL inside a listener.
  * `send_key()` matches `key_toggle_layout` (default Ctrl+Shift+S) with the other tab bindings; it calls the
    new `layout_toggle()`, which flips the mode and re-applies the layout.
  * PTY branch: output on a session that is DRAWN but not visible (side-by-side) marks the frame dirty and
    sets NO unread flag; only a session with no pane at all gets `mux_touch()` + a title update, and the
    redraw is skipped when there is no bar (the picture really did not change).
  * Startup: with `tab_bar = always` the initial window height gains `cell_h + PANE_GAP`, otherwise the grid
    would come up one row short of the configured `rows`.
  * New log line, one per LAYOUT-SHAPE change (not per frame):
    `layout: tab view, 1 pane, tab bar off, grid 80x24 per session` — the client-side evidence for every
    bar/pane report, and what `tests/cli_test.sh` will grep for. The `grid resized` line gained
    `%s view, %d pane%s` and its `unused` numbers are now per pane.
  * src/layout.c + the header: new `layout_grid_size(const Layout *, int *cols, int *rows)` — the grid size
    every session of a layout gets, which is what makes a tab switch cost nothing (no resize, no SIGWINCH
    storm, no reflow on every switch). 0/0 when the layout has no pane.
  * include/kai-term/kai-term.h: the three comments that claimed "there is no tab bar" (`DEF_TITLE`,
    `Tab.attention`, `mux_title`) now say what is true since session 57's renderer work — the bar carries the
    same state inside the window, the title is what a compositor/taskbar shows.
  VERIFIED: `make clean && make` -> exit 0, 0 warnings, 0 errors, `bin/kai-term` produced
  (`/tmp/s60_build1.log`). The five baseline errors are gone; nothing has been RUN yet, so no test result, no
  live behaviour and no feature is claimed to work at this point.
  PITFALL HIT (mine, third time for this class — the Key Decisions section already warns about it): the
  anchor `## Environment (verified 2026-10-12` occurs TWICE, once inside the session-55 entry that describes
  that very accident. `grep -c "^## Environment"` said 1 and lied; the python `count()` said 2 and aborted
  before writing anything. Anchor on a line-start-anchored string (`\n## ...`) and count with python, not
  with a `^`-anchored grep.
  NEXT ACTION: `make test` and `make asan`, then the new checks (layout_grid_size in tests/test_vt.c, the
  three session-57 config keys in tests/cli_test.sh), then the live key-injection run.

- 2026-10-16 (session 60, STEP 2 DONE — the full suite runs again, and the new checks pin the wiring's
  invariants). Exact commands and results:
  * `make clean && make` -> exit 0, 0 warnings, 0 errors, `bin/kai-term` built (`/tmp/s60_build1.log`).
  * `make test` -> `486 checks, 0 failures` + `MODEL OK (0 problems)` + `CLI: 81 checks, 0 failures,
    0 skipped` (`/tmp/s60_test1.log`). `WAYLAND_DISPLAY=wayland-1` is set here, so 0 skipped means the
    CLI suite really opened windows ~25 times with the newly linked binary and every run exited by itself.
  * `make asan` -> `486 checks, 0 failures` + `MODEL OK`, no LeakSanitizer/UBSan report
    (`/tmp/s60_asan1.log`). This is the `make asan` re-run session 56 never got after its main.c wiring.
  * tests/test_vt.c: 4 new checks for `layout_grid_size()` — it names the panes' grid, EVERY pane of one
    layout has that same grid (so a tab switch never resizes anything), a session outside the visible pane
    window still gets the size its pane will have, and no pane -> 0x0 so the caller must not resize.
    `./bin/test_vt` -> `490 checks, 0 failures` (486 -> 490).
  * tests/cli_test.sh: 5 new checks over the real binary — `tab_bar`/`split_max`/`key_toggle_layout` are
    known keys, `key_toggle_layout = none` logs `config: key_toggle_layout unbound`, and the new
    `layout:` line reads `layout: tab view, 1 pane, tab bar on, grid …` with `tab_bar = always` and
    `… tab bar off …` with the default `auto`. `sh tests/cli_test.sh ./bin/kai-term` ->
    `CLI: 86 checks, 0 failures, 0 skipped`. The no-display skip list gained the two new groups.
  NOT VERIFIED YET: anything that needs a human to look (does the bar LOOK right, are the labels legible,
  is the focus border where it should be) — that is the user's, as always. And the tab bindings have still
  never been exercised end to end (session 56's live check), which is the next entry.
  NEXT ACTION: the live key-injection run — `bin/kai-term -d` with injected Ctrl+Shift+T / Right / Left /
  S / W, reading the `mux:` and `layout:` log lines as the evidence, plus an ASan build of the binary
  itself so the new main.c paths are covered by the sanitizer too.

- 2026-10-16 (session 61, RESUMED session 60 — objective and plan recorded before any change).
  OBJECTIVE: finish what session 60 left, in its recorded order — (a) the live end-to-end run of the tab
  bindings and the layout toggle, which session 56 never got and session 60 named as its own next action;
  (b) an ASan build of the `kai-term` BINARY itself so the new main.c mux/layout paths are covered by the
  sanitizer, not just the unit tests; (c) the docs pass (README, docs/ARCHITECTURE.md, ISSUES.md, AGENT.md)
  for multiplexing + the tab bar + side-by-side; (d) commit and push to `origin/main`, which the user asked
  for in session 60 ("Then it needs to be pushed").
  PLANNED STEPS: 1 re-measure the baseline; 2 the live key-injection run against `./bin/kai-term -d`
  (`.scratch/inject`), evidence = the `mux:` / `layout:` / `grid resized` log lines, no screen recording
  (the user verifies anything visual themselves); 3 an ASan build of the binary + the same injected run with
  `ASAN_OPTIONS=detect_leaks=0`; 4 fix whatever the runs expose, with checks added for it; 5 docs; 6 commit
  + push after `git fetch`.
  EXPECTED OUTCOME: the log shows a session opened by Ctrl+Shift+T, switches by Ctrl+Shift+Right/Left, a
  `layout:` line flipping between `tab view` and `split view` on Ctrl+Shift+S, and a session closed by
  Ctrl+Shift+W; the ASan run reports no Kai-Term leak or overflow; the docs describe only what was built and
  run.
  BASELINE RE-MEASURED FIRST (nothing trusted from the journal): `make clean && make` -> exit 0, 0 warnings,
  `bin/kai-term` built (`/tmp/s61_base_build.log`); `make test` -> `490 checks, 0 failures` + `MODEL OK
  (0 problems)` + `CLI: 86 checks, 0 failures, 0 skipped` (`/tmp/s61_base_test.log`). `git status -sb`:
  `main` == `origin/main` at `0437537`, 15 modified files, `RM include/k-term/k-term.h ->
  include/kai-term/kai-term.h`, `src/mux.c` + `src/layout.c` untracked — i.e. sessions 56-60 are still
  uncommitted, as session 60 left them.

  STEP 2 DONE — the live key-injection run (session 56's missing check) RAN, and it found TWO REAL DEFECTS.
  Harness `.scratch/s61/tabs.sh` (log `/tmp/s61_live.log`), `./bin/kai-term -d -e 'sleep 400 # MARK'`, keys
  injected with `.scratch/inject`, window focused by "the id that appeared after launch":
  VERIFIED WORKING (the log lines are the evidence):
  * `Ctrl+Shift+T` twice -> `mux: tab 2/2 opened (pid …, pty fd 36, grid 154x60)`, `mux: tab 3/3 opened`;
    the bar appears with the second session (`layout: tab view, 1 pane, tab bar on, grid 154x60`) and every
    session is resized to the SAME grid (154x60) — the layout_grid_size invariant holds live.
  * `Ctrl+Shift+Right` / `Left` -> `mux: tab 3/3 visible`, `mux: tab 2/3 visible` (mux_next/mux_prev really
    do move the active tab).
  * `Ctrl+Shift+S` -> `layout: side-by-side view, 3 panes, tab bar on, grid 50x60 per session` and three
    `grid resized … side-by-side view, 3 panes` lines; `Ctrl+Shift+S` again -> `tab view, 1 pane`.
  * `Ctrl+Shift+W` x3 -> `mux: tab 3 closed, 2 left`, `tab 2 closed, 1 left`, `tab 1 closed, 0 left`,
    `mux: the last session is gone, exiting`, `kai-term exiting`, exit 0. Closing the last session exits the
    window, and the bar disappears with the second-to-last close (`tab bar off`).
  * A LEFT CLICK on a bar item reaches the bar code: `bar: click selects session 2/3` (pointer aimed with the
    new absolute injector, see Environment).
  DEFECT A — a bar/pane click does NOT switch the session, although the log says it does.
  `tab_switch()` (src/main.c:576) logs `mux: tab N/M visible`, re-applies the layout and marks the frame
  dirty, but it never calls `mux_goto()` — only the KEYBOARD paths change the active tab, because
  `mux_next`/`mux_prev`/`mux_goto` are called by `send_key` (main.c:738,739,754) and their return value is
  merely passed to tab_switch as an argument. The pointer paths (main.c:1079 bar click, main.c:1092 click in
  a non-active pane) pass the index but nothing consumes it. Measured (`.scratch/s61/panemap.sh`,
  `/tmp/s61_map.log`): split view, 2 panes (pane0 x=8, pane1 x=630, cell 8 px); clicks at surface x = 20,100,
  250,400,430,500 in the LEFT pane all report `cell=(0,29)` — the value the clamp produces when the origin
  still belongs to pane 1 — while x=850 -> `cell=(27,29)` and x=880 -> `cell=(31,29)` are exact for pane 1.
  Six clicks in pane 0 logged six `mux: tab 1/2 visible` lines, i.e. the same "switch" over and over.
  FIX PLANNED: make `tab_switch()` itself call `mux_goto()` and log the ACTIVE INDEX THAT mux REPORTS, so
  the line cannot lie; the keyboard calls then become idempotent no-ops.
  DEFECT B — `map_button()` (src/main.c:918) swaps BTN_RIGHT and BTN_MIDDLE. The kernel header says
  BTN_LEFT 0x110, BTN_RIGHT 0x111, BTN_MIDDLE 0x112 (`/usr/include/linux/input-event-codes.h:357-359`), but
  the switch labels 0x111 as middle (SGR 1) and 0x112 as right (SGR 2). Consequences: an injected middle
  click on a bar item produced NO `bar: middle click closes session` line at all (measured,
  `/tmp/s61_bar.log`), and — worse — a RIGHT click on the bar would hit the `btn == 1` branch and CLOSE a
  session. It also swaps middle/right in SGR mouse reporting for vim/htop/etc.
  FIX PLANNED: correct the mapping and make it testable — move it to vt.c as a pure
  `mouse_sgr_button(uint32_t evdev)` (vt.c already owns the mouse-reporting state) and pin it with checks in
  tests/test_vt.c, because main.c is in no test binary.
  HARNESS FACTS LEARNED (recorded in Environment): the relative pointer injector cannot be aimed (its
  `--home` drives into niri's top-left overview corner — the user's warning) and `pkill -f MARK` matched the
  harness's own command line and killed the harness (exit -9). The absolute injector
  `.scratch/s61_apinject` (no homing, parks at the output centre, refuses the corner) aims exactly: probe
  click at output (700,600) -> `ptr=(49,584)` -> window origin (651,16), and every later click landed where
  aimed.
  NEXT ACTION: fix A and B, add the checks, rebuild, re-run `make test`/`make asan` and both live harnesses
  (the bar middle-click close and the pane-click focus must now be visible in the log), then the ASan build
  of the binary, then docs, then commit + push.

  STEP 3 DONE — DEFECTS A AND B FIXED, and both re-verified LIVE (not just by the unit tests).
  Fix B (the button mapping):
  * `include/kai-term/kai-term.h` — new `EVDEV_BTN_LEFT/RIGHT/MIDDLE` enum (0x110/0x111/0x112) next to the
    MOUSE_* bits, with the comment saying the kernel order is NOT the protocol order, plus the
    `mouse_sgr_button()` prototype.
  * `src/vt.c` — new "mouse button mapping" section before "terminal core": `mouse_sgr_button()` returns
    0 left / 1 middle / 2 right / 3 anything else. It lives in vt.c because that is where the mouse-reporting
    state is and because main.c is in no test binary (the same reason cli_test.sh exists).
  * `src/main.c` — `map_button()` DELETED; `ptr_button()` calls `mouse_sgr_button(button)`; a comment marks
    where the mapping went.
  * `tests/test_vt.c` — 6 new checks after the mouse-mode block, including the raw literals
    (`EVDEV_BTN_RIGHT == 0x111 && EVDEV_BTN_MIDDLE == 0x112`) so the enum cannot be re-ordered to match a
    wrong switch again.
  Fix A (the switch that did not switch): `tab_switch()` in src/main.c now calls `mux_goto(&mux, idx)` itself
  and logs `act + 1` from what mux reports rather than the requested index, so the line cannot claim a switch
  that did not happen. The keyboard paths (`tab_switch(mux_next(&mux))` etc.) are unchanged and become
  idempotent (mux_goto to the index they already moved to). Comment records why the bug was invisible on the
  keyboard path.
  VERIFIED (exact commands): `make` -> exit 0, 0 warnings (`/tmp/s61_b2.log`); `make test` -> `496 checks,
  0 failures` + `MODEL OK (0 problems)` + `CLI: 86 checks, 0 failures, 0 skipped` (`/tmp/s61_t2.log`);
  490 -> 496 (the 6 button checks).
  NEGATIVE TEST: the same tests against a vt.c with the two cases swapped back -> `496 checks, 3 failures`,
  exactly `BTN_MIDDLE -> SGR 1`, `BTN_RIGHT -> SGR 2`, `raw 0x111 is right, raw 0x112 is middle`
  (`/tmp/s61_neg/`).
  RE-RUN OF THE LIVE HARNESS `.scratch/s61/panemap.sh` (`/tmp/s61_map.log`) — the mapping is now exact in
  BOTH panes of a side-by-side window (pane0 x=8, pane1 x=630, cell 8 px): surface x=20 -> `cell=(1,29)`,
  100 -> `(11,29)`, 250 -> `(30,29)`, 400 -> `(49,29)`, 430 -> `(52,29)`, 500 -> `(61,29)` (all pane 0),
  850 -> `(27,29)`, 880 -> `(31,29)` (pane 1). Before the fix every pane-0 click reported column 0. The mux
  log also became honest: one `mux: tab 1/2 visible` for the first pane-0 click and one
  `mux: tab 2/2 visible` for the first pane-1 click, instead of the same line six times.
  RE-RUN OF `.scratch/s61/barclick.sh` (`/tmp/s61_bar.log`) — left click on bar item 2 ->
  `bar: click selects session 2/3` + `mux: tab 2/3 visible`; MIDDLE click on bar item 3 ->
  `bar: middle click closes session 3/3` + `mux: tab 3 closed, 2 left (pid …)` (before the fix: no line at
  all); `Ctrl+Shift+S` -> `layout: side-by-side view, 2 panes, tab bar on, grid 76x60 per session`; a click
  in the left pane -> `mux: tab 1/2 visible` AND `sel press cell=(30,29) … cols=76`, i.e. the click both
  focused that session and mapped through its pane.
  NOTE ON COVERAGE: these two fixes are proven by the live runs, not by bin/test_vt — main.c is in no test
  binary and no test can send a pointer button. That is stated here so nobody later "assumes" the unit
  tests cover the pointer paths.
  NEXT ACTION: an ASan build of the kai-term BINARY itself plus the same injected session run under it
  (`ASAN_OPTIONS=detect_leaks=0`), then `make asan`, then the docs pass (README multiplexing/tab bar/
  side-by-side + the nine settings + the shortcuts; ARCHITECTURE mux.c/layout.c + the new log lines;
  ISSUES.md the two fixed defects + the layout/tab-bar limitations + the now-stale "window title is always
  Kai-Term" wording, since mux_title does push a title now), then commit + push after `git fetch`.

  STEP 4 DONE — the sanitizer runs, then the docs pass (in progress, file by file).
  ASan/UBSan build of the BINARY itself (`.scratch/s61/kterm_asan`, built with the Makefile's own PKG list —
  the first attempt forgot `wayland-egl egl gl` and died on undefined `egl*` references), driven by
  `.scratch/s61/asan_tabs.sh` (`/tmp/s61_asan_live.log`, `ASAN_OPTIONS=detect_leaks=1`, run ended by closing
  every session so LeakSanitizer actually ran): 3 sessions opened, digit jump `Ctrl+Shift+1` ->
  `mux: tab 1/3 visible`, output typed into three different sessions, split view + `Ctrl+Shift+Right` sliding
  the pane window, then 3 closes -> `mux: the last session is gone, exiting` + `kai-term exiting`.
  NO AddressSanitizer memory error, NO UBSan "runtime error", 0 `kai-term error|warn` log lines, and the only
  leaks are libdbus-1 and one `<unknown module>` frame — `SUMMARY: AddressSanitizer: 1250 byte(s) leaked in 5
  allocation(s)`, no frame in Kai-Term source (consistent with ISSUES.md Open #3). Closing back to one session
  also restored the startup shape exactly: `layout: tab view, 1 pane, tab bar off, grid 154x61 per session`.
  `make asan` -> exit 0, `496 checks, 0 failures` + `MODEL OK (0 problems)`, no sanitizer report
  (`/tmp/s61_asan_tests.log`).
  DOCS (1/3) README.md — 3 new feature bullets (several sessions in one window, the tab bar, side-by-side
  view); 9 new settings rows (`max_tabs`, `tab_bar`, `split_max`, `key_new_tab`, `key_close_tab`,
  `key_next_tab`, `key_prev_tab`, `key_tab_prefix`, `key_toggle_layout`) with the defaults read from
  `config_default()` in src/config.c, so the table now matches all 27 keys; 5 new shortcut rows; and a new
  "Sessions and layouts" section (what the bar shows, click/middle-click, the view toggle, the equal-grid
  rule, closing the last session, and the binding syntax). Only behaviour that has been built and RUN is
  described.
  NEXT: docs/ARCHITECTURE.md (mux.c/layout.c in the module map, the frame path with the bar + panes, the new
  `-d` log lines, the button mapping), then ISSUES.md (the two fixed defects, the tab-bar/layout limitations,
  and the stale "the window title is always Kai-Term" limitation — `update_title()` does push `mux_title()`
  now), then commit + push.

- 2026-10-16 (session 62, RESUMED session 61 — objective and plan recorded before any change).
  OBJECTIVE: finish what session 61 left, in its recorded order — the docs pass (README was done;
  docs/ARCHITECTURE.md and ISSUES.md were not), then commit and push, which the user asked for in session 60
  ("session 56/57 needs to be finished. Then it needs to be pushed"). No source file is touched unless a doc
  pass finds a documented claim that contradicts the code.
  PLANNED STEPS: 1 re-measure the baseline; 2 docs/ARCHITECTURE.md (mux.c + layout.c in the module map, the
  data flow through layout_apply/layout_compute, the per-tab pty fds in the event loop, the bar + pane draw
  path, the evdev -> SGR button mapping, the new `-d` lines, the corrected check counts); 3 ISSUES.md (the
  two session-61 defects into "Fixed" with their root causes, the tab-bar/layout known limitations, and the
  stale "the window title is always Kai-Term" limitation — `update_title()` does push `mux_title()` now);
  4 AGENT.md Current Status / Next Steps / Project Files sync; 5 `make`/`make test`/`make asan` after the doc
  edits; 6 `git fetch` + commit + push.
  EXPECTED OUTCOME: all four docs describe only what has been built and RUN, the check counts agree
  everywhere, and `origin/main` contains the sessions 56-61 work.
  BASELINE RE-MEASURED FIRST (nothing trusted from the journal): `make clean && make` -> exit 0, 0 warnings,
  `bin/kai-term` built (`/tmp/s62_base_build.log`); `make test` -> `496 checks, 0 failures` + `MODEL OK
  (0 problems)` + `CLI: 86 checks, 0 failures, 0 skipped` (`/tmp/s62_base_test.log`); `make asan` -> `496
  checks, 0 failures` + `MODEL OK (0 problems)`, no LeakSanitizer/UBSan report (`/tmp/s62_base_asan.log`).
  `git status -sb`: `main` == `origin/main` at `0437537`, 15 modified files, `RM include/k-term/k-term.h ->
  include/kai-term/kai-term.h`, `src/mux.c` + `src/layout.c` untracked — sessions 56-61 are still
  uncommitted, as session 61 left them.
  README.md (session 61's part of the pass) CHECKED AGAINST THE SOURCE, not trusted: its settings table has
  exactly the 27 keys `src/config.c` parses (`grep -c 'strcmp(key' src/config.c` -> 27), and its flags table
  matches `usage()`.
  NEXT ACTION: docs/ARCHITECTURE.md.

  STEP 2 DONE — docs/ARCHITECTURE.md updated (data flow through Mux + layout_compute; `src/mux.c` and
  `src/layout.c` added to the module table; render.c/config.c/vt.c/main.c rows corrected; test counts 364 ->
  496 and 77 -> 86; the event loop now says one pty fd PER SESSION with the `tab_for_fd()` / `tab_for_child()`
  lookups and the `waitpid(-1, …, WNOHANG)` reap; a new "Multiplexing and window layout" section for the
  dense-Tab-array rules, the attention flag, the `layout_grid_size()` rule, the pointer-check order, the
  chrome-colours-from-the-palette rule and the evdev -> SGR button mapping; the "Window geometry" section
  rewritten for per-pane `term_grid_geometry()` calls (the `term_size_changed()` / `render_set_origin()` /
  `Render.gx,gy` wording is gone — `grep` confirms no stale reference); the `-d` line list gained the
  `layout:` line, the `mux:` lifecycle lines, the `bar:` click lines and the new `grid resized` fields).
  DEFECT C FOUND WHILE CHECKING THE DOC AGAINST THE CODE (src/main.c:1773): the startup window height for
  `tab_bar = always` is `init_h += font.cell_w + PANE_GAP` — the bar is one cell ROW tall (`out->bar_h = ch`
  in src/layout.c), so it must be `cell_h`. With a non-square cell (8x17 here) the window is asked for 9 px
  too little, so the grid comes up one row SHORT of the configured `rows` — exactly what the comment above
  that line says the addition prevents. Not cosmetic: it is the one case where the configured `rows` is
  silently not what the user gets.
  PLANNED FIX (so it cannot come back): the arithmetic does not belong in main.c, which is in no test
  binary. Move it to the pure geometry module as `layout_initial_size(cols, rows, cell_w, cell_h, pad, gap,
  bar, &win_w, &win_h)` in src/layout.c (+ the prototype in kai-term.h), call it from main.c, and pin it in
  tests/test_vt.c with a ROUND TRIP: feed the size it asks for back into `layout_compute()` and require the
  grid to come back as exactly the requested cols x rows, for `BAR_ALWAYS` and `BAR_AUTO`, with a deliberately
  non-square cell. That check fails against the `cell_w` version (29 rows instead of 30 at cell 8x17), which
  is the negative test.
  NEXT ACTION: the fix + the check, then `make test`.

  DEFECT C FIXED + VERIFIED.
  * `include/kai-term/kai-term.h` — `layout_initial_size(cols, rows, cell_w, cell_h, pad, gap, bar,
    &win_w, &win_h)` prototype, with the comment saying why the arithmetic is not in main.c.
  * `src/layout.c` — implemented next to `layout_grid_size()`: `win_w = cols*cell_w + 2*pad`,
    `win_h = rows*cell_h + 2*pad`, plus `cell_h + gap` when `bar == BAR_ALWAYS` (BAR_AUTO shows no bar for
    the single starting session, so it adds nothing). Inputs clamped like the rest of the module.
  * `src/main.c` — the three hand-written lines replaced by one `layout_initial_size()` call; the comment
    now points at the module instead of restating the arithmetic.
  * `tests/test_vt.c` — 5 new checks: the exact startup size for `BAR_ALWAYS` and for `BAR_AUTO`, a
    ROUND TRIP through `layout_compute()` + `layout_grid_size()` requiring the configured 80x30 to come back
    for both, and the padding-0 / square-cell case.
  VERIFIED (exact commands): `make` -> exit 0, 0 warnings (`/tmp/s62_b1.log`); `make test` -> `501 checks,
  0 failures` + `MODEL OK (0 problems)` + `CLI: 86 checks, 0 failures, 0 skipped` (`/tmp/s62_t1.log`);
  496 -> 501 (the 5 new checks).
  NEGATIVE TEST: the same tests against a src/layout.c with `*win_h += ch + g` changed back to `cw + g`
  (built into /tmp/s62_neg/) -> `501 checks, 2 failures`, and the two are exactly
  "layout: the startup window is the grid + the margin + one bar ROW and its gap" and
  "layout: a tab_bar = always startup really gets the configured cols x rows back". The `BAR_AUTO` checks
  still pass there, as they should — that path adds no bar row.
  NOT VERIFIED: what the user sees with `tab_bar = always` in a real window — niri tiles the window to its
  own size, so the requested startup height is not observable from the log here. The arithmetic and its
  round trip are verified; the visual result is the user's, as always.
  NEXT ACTION: ISSUES.md.

  STEP 3 DONE — ISSUES.md updated.
  * "Fixed" gained three entries at the top (newest-first, as the rest of the section is): the bar/pane click
    that logged a switch it never made (defect A, session 61 — root cause `tab_switch()` never called
    `mux_goto()`, with the live evidence and the coverage note that main.c is in no test binary), the
    `BTN_RIGHT`/`BTN_MIDDLE` swap (defect B, session 61 — kernel order is not the protocol order, the mapping
    moved to `mouse_sgr_button()` in vt.c, 6 checks + the negative test), and the `tab_bar = always`
    one-row-short startup (defect C, this session, with its round-trip checks and negative test).
  * "Known limitations": the stale "The window title is always Kai-Term" entry REPLACED — `update_title()`
    does push a title now (`Kai-Term`, or `Kai-Term [active/total]` plus `!N`); what is not in it is the
    application's OSC title, which is the tab bar label instead. Four new limitations: the bar costs one text
    row and is dropped before the grid is, labels are cut to the item width with no ellipsis, only
    `split_max` sessions are drawn in split view (the rest show as `*` / `!N`), and every session of a window
    shares one grid size so a layout change re-wraps sessions that are not on screen.
  * Stale status corrected: Open #4 said the fullscreen freeze "CANNOT" be investigated because
    `bin/kai-term` does not link — it has linked since session 60, so that sentence is rewritten (still not
    investigated: the user deferred it), and its "How to investigate once the binary links again" heading
    fixed. Open #3 notes that the hand-built ASan binary run of session 61 found no Kai-Term frame.
  Verified by grep after the edit: no "does not link" / "window title is always" claim remains anywhere in
  README.md, ISSUES.md or docs/ARCHITECTURE.md.
  NEXT ACTION: AGENT.md Current Status / Next Steps / Project Files, then the final build/test/asan run.

  STEP 4 DONE — AGENT.md synced (this file): Current Status re-dated to session 62 with the new numbers
  (501 checks / CLI 86) and a new bullet saying what IS and what is NOT proven about multiplexing, the tab bar
  and side-by-side view; Current Task rewritten from "IN PROGRESS (session 60)" to DONE with the three open
  ISSUES items and the one thing to ask the user (their eyes on the bar); the session-56/57 blocks relabelled
  as the COMPLETED SPECIFICATION; the stale "cannot be reproduced while bin/kai-term does not link" wording
  removed from the session-59/58 blocks; Next Steps now lists three open bugs, drops the
  "title never pushed to the compositor" housekeeping item (it is done) and replaces it with the OSC-title
  question, and adds the session-61 harnesses to the scratch list; Project Files corrected (test_vt.c 486 ->
  501 with the new coverage, cli_test.sh 81 -> 86 and its "NOT RUNNABLE" note deleted, vt.c/layout.c
  descriptions extended); History gained the session-59 rename era and the sessions 60-62 era; Key Decisions
  gained four entries — the log line that prints what it was GIVEN can lie, the evdev button order, geometry
  belongs in src/layout.c because main.c is in no test binary, and a Term's pen must never colour the chrome.
  Re-checked the claims this file makes: `grep -rnE "TEMPORARY|PROBE|EXPERIMENT|NEGATIVE" src/ include/
  tests/` -> nothing; README's settings table is the 27 keys `src/config.c` parses.
  STEP 5 DONE — FINAL VERIFICATION from a clean tree, exact commands and results:
  * `make clean && make` -> exit 0, 0 warnings, 0 errors, `bin/kai-term` built (`/tmp/s62_final_build.log`).
  * `make test` -> `501 checks, 0 failures` + `MODEL OK (0 problems)` + `CLI: 86 checks, 0 failures,
    0 skipped` (`/tmp/s62_final_test.log`). `WAYLAND_DISPLAY=wayland-1` is set, so 0 skipped means the CLI
    suite really opened ~25 windows with this binary and each exited by itself.
  * `make asan` -> exit 0, `501 checks, 0 failures` + `MODEL OK (0 problems)`, no LeakSanitizer/UBSan report
    (`/tmp/s62_final_asan.log`).
  * live `timeout -k 2 15 ./bin/kai-term -d -c /tmp/s62_bar.conf -e 'echo smoke-ok; sleep 1'` with
    `tab_bar = always`, `rows = 30`, `cols = 80` -> exit 0, ZERO error/warn lines, logging
    `layout: tab view, 1 pane, tab bar on, grid 154x60 per session` (`/tmp/s62_live_always.log`); the same
    run with the default `auto` -> `layout: tab view, 1 pane, tab bar off, grid 154x61 per session`
    (`/tmp/s62_live_auto.log`), i.e. the bar really costs exactly one row and only when it is shown. niri
    tiled the window to 1253x1002, so the requested startup height is still not observable here — that part
    stays verified by the unit round trip only.
  NEXT ACTION: `git fetch`, check ahead/behind, commit everything and push to `origin/main`.

  STEP 6 DONE — committed and pushed.
  * `git fetch origin` then `git rev-list --left-right --count origin/main...main` -> `0 0`, so no rebase was
    needed (the pitfall from session 50 checked, not assumed).
  * `git add -A` -> 18 paths staged: 16 modified, `src/mux.c` + `src/layout.c` added, and the header recorded
    as a RENAME `include/k-term/k-term.h -> include/kai-term/kai-term.h`. Nothing under `bin/` or `build/`
    was staged (`git diff --cached --name-only | grep -E '^(bin|build)/'` -> nothing).
  * `git commit` -> `d05c817 "Multiplexing, tab bar + side-by-side view; rename to Kai-Term"`.
  * `git push origin main` -> `0437537..d05c817 main -> main`, exit 0; afterwards `git status -sb` shows
    `## main...origin/main` with no ahead/behind and `git rev-list --left-right --count` -> `0 0`.
  FACT FOUND DURING THE PUSH: the remote URL is now `https://github.com/K-code-AI/Kai-Term` — the user renamed
  the GitHub repository and the local remote already points at it, so the "origin is still K-Term" note that
  sessions 50-59 carried is OBSOLETE. Corrected in the Git Key Decision, in Next Steps 6 and in the session-59
  follow-up list (that item is now DONE). The other three follow-ups were RE-VERIFIED as still pending: niri
  config line 83 is still `match app-id="K-Term"`, `/usr/local/bin/k-term` is still the only installed binary
  (root-owned, mtime 2026-10-15 22:08) with no `kai-term` in PATH, and `/usr/local/bin/desktop-terminal` still
  execs `k-term -c ~/.config/k-term/desktop-terminal.conf` (that file is actually user-owned, not root-owned as
  session 59 said — corrected). Not touched: they are the user's, and they took them on in session 61.
  The doc corrections above are the follow-up commit.
  SESSION 62 RESULT: sessions 56 + 57 are finished, three real defects fixed (two of them found by the live
  run, one by reading the docs against the source), all four docs consistent at 501 checks / CLI 86, and
  everything is committed and pushed. Nothing is in progress.
  NEXT ACTION: ask the user to look at the tab bar and the side-by-side view in their own window (the one
  thing no test here can verify), and ask which of ISSUES.md Open #1/#2/#4 to take next.

## Environment (verified 2026-10-12; re-confirmed live 2026-10-14 session 38; compositor state re-checked
2026-10-15 sessions 47, 48, 49 and 55)
- Gentoo Linux, kernel 6.18.54, gcc 15.3.0, pkg-config 3.0.7.
- Wayland session active: `XDG_RUNTIME_DIR=/run/user/1000`, `WAYLAND_DISPLAY=wayland-1`
  (compositor is niri; advertises wl_seat v9, wl_compositor v6, wl_data_device_manager v3, wl_shm v2).
- Libraries (pkg-config verified): wayland-client 1.26.0, wayland-cursor 1.26.0, wayland-egl,
  egl 1.5, gl 1.2, xkbcommon 1.13.2, freetype2, fontconfig. Headers verified: EGL/egl.h,
  GL/gl.h + GL/glext.h, wayland-cursor.h, xkbcommon/*.h.
- `xdg-shell-client-protocol.h` is NOT preinstalled; `/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml`
  exists and `wayland-scanner` is installed, so the Makefile generates the bindings into `build/generated/`.
- GPU: NVIDIA RTX 5070 (GL 4.6). Both `GL_ARB_draw_instanced` and `GL_ARB_instanced_arrays` are
  advertised, so the instanced path is active on this machine.
- **`NIRI_SOCKET` in the shell environment is STALE** (session 55): it points at
  `niri.wayland-1.2298001.sock` while the running compositor's is
  `/run/user/1000/niri.wayland-1.<pid>.sock`. Every `niri msg` then dies with "error connecting to the
  niri socket". Fix: `export NIRI_SOCKET=$(ls -t /run/user/1000/niri.wayland-1.*.sock | head -1)`.
- **The user verifies anything visual themselves — do not run screen-recording or pixel-diff harnesses to
  check how the window looks.** Stated directly in session 55 ("stop trying to do pointless visual test
  when I have eyes over here"). Ask them and record their verdict; that is faster and it is the better
  evidence. The tooling below exists for questions that cannot be answered any other way (and the user
  must be told before it is used again). Prefer the headless route for logic: `tests/test_vt.c`,
  `tests/dbg.c`, `tests/cli_test.sh`, and a throwaway program that links `src/vt.c` + `src/config.c`.
- **Screenshots/recording (available, use only when justified):** `wf-recorder` 0.6.0 and
  `ffmpeg`/`ffprobe` 8.1.2 are installed — `wf-recorder -o HDMI-A-1 -l -c ffv1 -f out.mkv` records
  losslessly (bgr0), which is the only form worth trusting for exact colours; H.264 shifts them enough
  that a colour match fails. Extract with `ffmpeg -i out.mkv -vf "select=eq(n\,15)" -vframes 1 f.png`
  and count pixels with PIL/numpy. `grim`/`slurp` are NOT installed; a temporary `glReadPixels`
  ASCII-art readback still works for per-cell pixel questions (see docs/ARCHITECTURE.md).
- **Keyboard/pointer injection DOES work here (session 45 — this CORRECTS the old "Headless limit").**
  `/dev/uinput` is writable; a virtual keyboard shows up as `/dev/input/eventNN`, udev classifies it
  (`ID_INPUT_KEYBOARD=1`) and **niri/libinput opens it** (check `ls -l /proc/$(pgrep -x niri)/fd`).
  `.scratch/inject.c` (binary `.scratch/inject`) types a US-layout string and presses chords
  (`@super+shift+m`, `@ctrl+c`, `@ret`); verified end to end — kai-term running `-e 'cat > FILE'` received
  exactly `hello^Mworld^M`. Session 44's "zero bytes, injection does not work" conclusion was WRONG: the
  injector's key table was the bug (evdev letter codes are NOT alphabetical — `KEY_A + (c-'a')` typed
  `kg##z` for `hello`). Ground truth for "did the key arrive" = the bytes the PTY got, not the screen
  (the screen is exactly what may be frozen).
  Pointer events can be injected the same way (`UI_SET_RELBIT`/wheel) — `.scratch/pinject.c` (binary
  `.scratch/pinject`, built 2026-10-15) does `--home`, `--to X Y`, `--click X Y`, `--mclick X Y`,
  `--drag X1 Y1 X2 Y2`.
  **THE TOP-LEFT CORNER IS A TRAP — the user's warning (session 61): when the pointer reaches the
  leftmost top corner, niri shows the whole desktop (overview), which moves every window and destroys any
  coordinate model the harness has.** So NEVER home/park the pointer at (0,0) or near it. `.scratch/pinject.c`
  `--home` sends `rel(-4000,-4000)`, i.e. it drives straight into that corner — do not use `--home` any more
  (session 61 measured it: after a `--home` the very next click landed 47 px off the intended point and the
  following clicks produced no `sel press` line at all, i.e. they went to the overview, not to the window).
  Use the ABSOLUTE injector instead: `.scratch/s61_apinject.c` (binary `.scratch/s61_apinject`) declares
  `ABS_X`/`ABS_Y` over the output size, parks at the output CENTRE (960,540) when the device is created, and
  refuses any target inside 40 px of the corner. libinput treats such a device as an absolute pointer, so
  there is no acceleration and the coordinate is used as-is — that is what makes a click aimable.
  CAVEAT measured in session 49 (and re-confirmed session 61): RELATIVE motion goes through libinput's
  pointer ACCELERATION, so an intended 120 px move can land ~480 px away, and a small intended move can land
  outside the window entirely (zero `sel press` lines). Do not aim a selection drag or a bar click by
  relative motion; use `s61_apinject`, and verify where a click really landed from Kai-Term's own
  `sel press/drag cell=(c,r) … ptr=(x,y)` log line — the window's absolute position is then
  `click_target - ptr`, measured, never guessed (niri IPC does not report window positions).
- **Clipboard testing needs a foreign client.** `wl-copy` / `wl-paste` are installed (`/usr/bin/`);
  `wl-paste --list-types` shows exactly what the compositor advertises for the current owner. A Kai-Term-to-
  Kai-Term paste test proves nothing about clipboard sharing — that is how a "private clipboard" complaint
  could survive every test ever run here. Working recipe (session 49): `wl-copy 'TEXT' </dev/null` (it
  otherwise keeps running and holds the pipe), launch kai-term with `-e 'stty raw -echo; cat > FILE'`, focus
  the new window, inject `@ctrl+shift+v`, and read FILE — the bytes the PTY got are the ground truth.
- **The compositor is cooperative again (session 47, re-confirmed by session 48) — live GUI measurement
  works again.** The session-46
  warning below is OBSOLETE: `niri msg focused-window` reports a focused window, `niri msg action
  set-window-width/-height --id …` really resizes (a kai-term window followed a 5-step sweep,
  `/tmp/s47_stress2.log`), and `wf-recorder` captures the test window (session 48: a 4 fps frame diff of a
  live kai-term shows the picture changing every frame, `.scratch/s48_frames/`). Re-verify cooperativeness
  before trusting any live number (it was broken for a while), but do not assume it is still broken.
  Session-46 observation kept for context: when the compositor does NOT render a surface, that surface
  gets its map frame and nothing after it (no frame callbacks, no buffer releases) and ANY EGL client
  blocks in its second `eglSwapBuffers` — that is an environment artifact, not a Kai-Term bug.
- **How to measure the draw path here (session 47, all proven working):**
  * `cat /proc/<pid>/wchan` — `ep_poll` = the event loop is alive; `futex_do_wait` = blocked inside the
    EGL driver (a swap that will never return). This is what separated "frozen display" from "dead loop".
  * `wf-recorder -f out.mp4` (no `-t`/`-q` options in 0.6.0, and `-c ffmpeg` fails with "Failed to find the
    given codec" — use the plain form; stop it with SIGINT; `slurp` is not installed, so `-g` cannot be fed
    an interactive region), then
    `ffmpeg -i out.mp4 -vf fps=4 f%03d.png` and count bright pixels / diff frames with numpy: a live
    terminal shows monotonically growing text pixels, a frozen one shows zero change.
  * An ASan build of the kai-term BINARY itself (not just the tests): `cc -O1 -g
    -fsanitize=address,undefined -fno-omit-frame-pointer $(CFLAGS) src/*.c
    build/generated/xdg-shell-protocol.c $(LDLIBS)`; run with `ASAN_OPTIONS=detect_leaks=0` to skip the
    third-party leak noise (ISSUES.md Open #1). That is how the `row[rcap]` overrun was found.
  * Identify a test window as "the window id that appeared after launch" and a test process by a unique
    marker inside its `-e` command (`pgrep -f MARKER`) — `pgrep -x kai-term` also matches leftovers from
    the previous trial and made an earlier A/B read the WRONG process.
- **niri IPC facts:** `niri msg --json windows` has no absolute window position (`layout.window_size`,
  `tile_pos_in_workspace_view`, `pos_in_scrolling_layout` only), windows can report `workspace: null`,
  and `niri msg action screenshot` did not produce a file (wf-recorder is the working capture path).
  niri here is 26.04 (8ed0da4), single output HDMI-A-1 1920x1080@60, scale 1.

## History (era level; root causes in ISSUES.md "Fixed")
- **2026-10-12/13, sessions 1-6** — scaffold, then first successful build. The vt.c grid model was
  rewritten as a rotation-based permutation (`row[]` is always valid), CSI parameter semantics were
  fixed, the event loop was rewritten to the canonical prepare_read -> epoll_wait -> read_events
  pattern, and the EGL/FreeType init paths were corrected. k-term builds, opens a window, renders,
  runs a shell, exits 0.
- **2026-10-13, sessions 7-10** — rendering correctness: vertex stride, MVP mapping, 6-vertex quads,
  `GL_LUMINANCE` atlas upload, first-frame bootstrap, clearing realloc-grown columns.
- **2026-10-14, session 12** — the two user-reported bugs: black window (font metrics were in font
  units, not pixels) and the 10 s startup delay (unanswered DA1/CPR/DA2 queries). Naming unified to
  `k-term` / `k_term`.
- **2026-10-14, sessions 13-15** — hardware instancing implemented correctly (see the Key Decisions
  pitfall about `GL_ARB_vertex_instanced`).
- **2026-10-14, sessions 16-17** — features: alternate screen, SGR mouse reporting, scrollback keyboard
  navigation, zoom, text selection + Wayland clipboard.
- **2026-10-14, sessions 18-19** — mouse-wheel scrolling (seat bound at v5, axis handlers, fractional
  line accumulator) and the `Grid.live_off` anchor fix that corrected wheel direction and auto-follow.
  Test count reached 124.
- **2026-10-14, session 20** — tree restructured to the project template, Makefile rewritten for it,
  .gitignore + docs/ARCHITECTURE.md added, last compiler warning silenced, all docs rewritten.
- **2026-10-14, sessions 21-22** — git repo removed at the user's request (`.gitignore` kept); LICENSE
  added, now MIT.
- **2026-10-14, session 23** — this journal pruned (session-by-session entries replaced by the era
  summary above; obsolete task blocks removed). No code touched: `make test` 124/0 + MODEL OK and
  `make asan` 124/0 + MODEL OK re-verified afterwards.
- **2026-10-14, sessions 24-32** — text-selection precision overhaul: sub-cell selection endpoints,
  per-fragment highlight compositing without GL blending, and two root-cause fixes for the
  selection-vs-pointer complaint (top-left grid anchoring, session 30; `wl_fixed_to_int` pointer
  conversion, session 31). User confirmed selection working (session 32). Root causes in ISSUES.md
  "Fixed". Test count 124 -> 155.
- **2026-10-14, session 33** — documentation audit/cleanup (no code change); see Progress.
- **2026-10-14, sessions 34-35** — anything the primary font does not cover now renders: fontconfig
  fallback faces loaded on demand (`fc_lookup`/`fb_load_face`/`font_find_face`), each Glyph remembering
  the face it came from, plus the three back-to-front render passes so a WIDE glyph is no longer clipped
  by the neighbouring cell's background.
- **2026-10-14, sessions 36-38** — COLOR emoji end to end: whole grapheme clusters assembled in vt.c,
  shaped to ONE glyph id by HarfBuzz, rasterized as premultiplied BGRA by FreeType, uploaded as an RGBA8
  GPU atlas and drawn un-tinted; wide emoji reserve two grid cells. Session 38 closed the two remaining
  width/cluster gaps (regional-indicator flag PAIRS, and U+FE0F/U+20E3 switching a narrow base to the
  wide emoji presentation) and repaired the README/ARCHITECTURE/ISSUES drift session 37 left behind.
  Tests 155 -> 184 -> 206. Root causes in ISSUES.md "Fixed", invariants in docs/ARCHITECTURE.md.
- **2026-10-14, session 39** — the user's `NextFeature.md` work order: CSI 3 J ("erase saved lines") so
  `clear` drops the scrollback (gated by `clear_scrollback`, on by default), and the shell now starts in
  `$HOME` by default with `working_dir` in the config and `-w DIR` per launch. Tests 206 -> 217.
- **2026-10-15, session 40** — the next `NextFeature.md` item: a multi-line paste no longer executes line
  by line. DEC mode 2004 is tracked and the clipboard block is sent as `ESC[200~` … `ESC[201~` (buffered,
  chunked <=4096 B, 8 MiB cap, 400 ms idle-watchdog flush). Also fixed a latent `term_init` bug (it freed
  an uninitialized grid; now guarded by the `GRID_VALID` sentinel). Tests 217 -> 238.
- **2026-10-15, sessions 41-42** — the user's next work order: `app_id` became a config key (verified end
  to end — niri reports the configured id), window `opacity` was made real (per-quad alpha + a shader that
  composites source-over in PREMULTIPLIED space, which also removed the near-black box over the counter of
  an `o`), selection endpoints snapped to whole cells, no highlight band over never-written cells, and the
  README/ARCHITECTURE scope restructure the user asked for. Tests 238 -> 260.
- **2026-10-15, session 43** — REFLOW: a width change now re-wraps instead of truncating (`Grid.wrap`
  soft-wrap marks + `grid_reflow()` + `grid_resize_plain()` for the alternate screen; the cursor travels
  with its logical cell; narrowing evicts the oldest rows). 32 new checks (260 -> 292) + `reflow*` stages
  in `tests/dbg.c`; live-verified through a niri resize sweep. Also removed the leftover `KTERM_PIXTEST`
  readback from `render_draw` and fixed the Makefile so the sanitizer binaries can no longer go stale
  (that stale binary had produced a false leak report). Docs synced across README/ISSUES/ARCHITECTURE,
  including the premultiplied-transparency design that session 42 had left undocumented.
- **2026-10-15, sessions 44-45** — `NextFeature.md` #1-#3: the default `app_id` is now `K-Term` (one
  `DEF_APP_ID` macro where the literal had been repeated four times); the CLI surface was cut back to
  `-e -w -C -d -v -h` with long forms and a message naming the config key for every retired flag
  (appearance settings are config-only; session 51 deleted that retired-flag messaging and renamed `-C`
  to `-c`); `copy_on_release` became a config toggle (default OFF);
  CLIENT-SIDE KEY REPEAT was implemented (`krfd` timerfd + `send_key`'s repeatability result +
  `repeat_delay`/`repeat_rate` in the config) and the user confirmed it in the real window. Tests
  292 -> 301 -> 312. Session 45 also found the fullscreen freeze's root cause (`solicit_frame()` arming a
  frame callback on an empty commit) and discovered the tools that make the draw path measurable here:
  `wf-recorder` + ffmpeg frame diffs and working uinput key injection.
- **2026-10-15, sessions 46-47** — the draw/commit cycle rewritten (ISSUES.md "Fixed: Fullscreen" +
  "Fixed: Black window"): a frame callback is armed only together with a commit that attaches a NEW
  buffer, a size change is answered with a buffer of the new size (`W.resized`), and no swap ever runs
  while a `wl_display_prepare_read()` is outstanding (the real rule; the earlier "never swap inside a
  listener" one was backwards). Session 47 also fixed a long-standing segfault on heavy output in a grown
  window (`sb_limit() = min(scrollback_max, rcap - rows)` as the single bound on
  `scroll_off`/`live_off`, 11 new checks -> 323). Both fixes were CONFIRMED by the user in the real window
  (session 49).
- **2026-10-15, sessions 48-49** — documentation/state audit and a bug report that did not reproduce, no
  source change in either. Session 48 found the code ahead of the journal (the `sb_limit()` work was in the
  tree and in ISSUES.md but its journal entry had never been written), restored Instruction.md's
  chronological order, condensed the session 43-47 detail that duplicated ISSUES.md/ARCHITECTURE (AGENT.md
  921 -> ~610 lines), and fixed real doc drift: ARCHITECTURE's "Frame pacing" rule 2 still stated the rule
  session 47 had disproved. Session 49 investigated a "K-Term has its own clipboard" report against a
  foreign client (`wl-copy`/`wl-paste`): the IN direction is proven byte-for-byte here, the user then
  confirmed both directions work, and the likely cause of the report is the intentional
  `copy_on_release = 0` default (a drag-release does not copy). ISSUES.md Open #1 (fullscreen) moved to
  Fixed on the user's confirmation; the clipboard facts and the mime/`accept()` observations are now
  documented in ISSUES.md, README.md and docs/ARCHITECTURE.md.
- **2026-10-15, session 50** — CLEANUP + git. The project is on GitHub (`origin
  https://github.com/K-code-AI/K-Term`, branch `main`, push access granted). Removed the only dead
  function (`term_dump`), the misleading `(void)def1;` cast, and every dangling `NextFeature #N` comment
  reference (that file is gone — the user deleted it on GitHub and the deletion is now in the tree).
  `osc_end()` made const-correct; four `-Wshadow` collisions in `tests/test_vt.c` renamed; the Makefile
  now builds with `WARN = -Wall -Wextra -Wshadow -Wwrite-strings -Wformat=2 -Wdiscarded-qualifiers`
  (the whole tree is clean under it). `.gitignore` was a kernel-module template that ignored neither
  `bin/` nor `build/` — replaced with project rules. ISSUES.md gained the "window title is never pushed
  to the compositor" limitation. Committed and pushed as `6b471df` after a `git pull --rebase` over the
  user's own GitHub commits. No behaviour change: 323 checks before and after, `make`/`make test`/
  `make asan`/live run all clean.
- **2026-10-15, session 51** — CLI surface at the user's request: the config flag is `-c` (was `-C`),
  `retired_flags[]` and its "this flag is not a command-line option any more" messaging DELETED, and every
  unknown option now prints one generic `k-term: unknown option '<arg>'` plus the option list and exits 1.
  Verified by running the binary (31 CLI checks, 0 failures — argv is not unit-tested, main.c is not
  linked into bin/test_vt). README / docs/ARCHITECTURE.md / ISSUES.md synced. 323 checks unchanged.
- **2026-10-15, sessions 52-55** — the user's three-item fix list and the padding feature. #0 (no CLI
  coverage) is DONE: `tests/cli_test.sh` + a `test-cli` Makefile target that drives the real binary
  (`CLI: 77 checks, 0 failures, 0 skipped`), negative-tested by deliberately breaking the parser. #1 (the
  second-instance `atlas glTexImage2D failed: 0x505` + SIGSEGV) and #2 (`eglSwapBuffers failed: 0x3000` on
  launch) are recorded in ISSUES.md "Open" and still unreproduced. Bug #4, zoom leaving stale prompt lines,
  was root-caused in `grid_resize_rows` and fixed (ISSUES.md "Fixed", user-confirmed); tests 323 -> 346 ->
  364. WINDOW PADDING (the `padding` config key + the single `term_grid_geometry()` helper that decides
  cols/rows AND the grid origin) implemented, and it exposed the `DEF_BG` clear-colour bug, fixed the same
  session; the user confirmed both in their real window (session 55). Session 55 was a docs/state audit:
  the padding Progress entry had never been written, ISSUES.md still carried two resolved items as Open,
  and three docs still quoted 323 checks — all corrected, and the sessions 52-55 work committed.
  USER INSTRUCTION from session 55, still in force: they verify anything visual themselves, so do not run
  screen-recording/pixel-diff harnesses to check how the window looks; use the headless tests and ask them.
- **2026-10-16, session 59** — the project RENAME `K-Term` -> `Kai-Term` at the user's request (a name
  conflict with another of their projects): directory, `include/kai-term/kai-term.h`, `bin/kai-term`,
  `~/.config/kai-term/kai-term.conf`, `DEF_APP_ID`/`DEF_TITLE`, the `kai-term <level>:` log prefix,
  `TERM_PROGRAM`, the Makefile and the tests that pin those literals. Measured against a pre-rename baseline
  (the tree did not fully build — session 57 had left main.c unwired) so the rename could be shown to
  introduce nothing; the user's own config was copied to the new path and the four items OUTSIDE the project
  (niri window rule, `/usr/local/bin/k-term`, `/usr/local/bin/desktop-terminal` + its .desktop entry, the
  GitHub repo name) were listed for the user, who took the first three. The fullscreen freeze came back
  intermittently (ISSUES.md Open #4) and the user deferred it.
- **2026-10-16, sessions 60-62** — MULTIPLEXING (session 56) and the TAB BAR + SIDE-BY-SIDE VIEW (session 57)
  finished. Session 60 wired src/main.c to `layout_apply()` + the new `render_init`/`render_draw`, which is
  what made `bin/kai-term` link again and unblocked `make test`/`make test-cli`/`make run` and every live
  check. Session 61 ran the live key/pointer-injection verification session 56 never got and found two real
  defects — a bar/pane click that logged a switch it never made (`tab_switch()` never called `mux_goto()`) and
  `BTN_RIGHT`/`BTN_MIDDLE` swapped — both fixed, unit-pinned where possible and re-verified live, plus an ASan
  build of the binary itself over a full multi-session run (clean). Session 62 finished the docs pass and,
  while checking docs/ARCHITECTURE.md against the source, found and fixed a third defect: `tab_bar = always`
  asked the compositor for `cell_w` instead of `cell_h` of extra height, so the grid came up one row short of
  the configured `rows`; the arithmetic moved into `layout_initial_size()` in src/layout.c so a check can pin
  it. Tests 436 -> 486 -> 496 -> 501; `make test` 501/0 + MODEL OK + CLI 86/0, `make asan` clean. The tab bar
  and side-by-side view are NOT yet confirmed by the user by eye.

## Next Steps
1. **Three real bugs are open, all user-reported, all in ISSUES.md "Open"** — start with #1, the only
   crash left:
   * **#1** launching `kai-term` while another kai-term is still running prints `atlas glTexImage2D failed:
     0x505 (512x512)` (GL_INVALID_VALUE on the mono atlas upload in `render_draw`) and then SIGSEGV.
     Never reproduced here yet. Work from the source and a headless harness, not from the screen.
   * **#2** `eglSwapBuffers failed: 0x3000` (EGL_BAD_ALLOC) on launch plus one `swap failed (1 in a row)`
     warning; the retry works and nothing looks wrong. Decide whether it is a real first-frame race worth
     fixing or just a log line to soften.
   * **#4** the intermittent fullscreen freeze (ISSUES.md Open #4), deferred by the user in session 59 — the
     levers and the rate-recording method are written there; the binary links, so it is investigable.
   Everything else the user has confirmed working in their own window (padding, zoom, fullscreen refresh,
   black-window fix, heavy-output segfault, clipboard both ways, transparency, key repeat, selection).
   **Not yet confirmed by eye: the tab bar, the side-by-side view and the session shortcuts** — they are
   verified by log lines and 501 checks here, but the user has not looked at them. Ask.
2. Housekeeping options (none of them a bug): make the ASan build of the `kai-term` binary a real Makefile
   target (only the tests have one; the hand-built one in `.scratch/s61/kterm_asan` is what proves the
   mux/layout paths are sanitizer-clean, and it found the `row[rcap]` overrun earlier, so covering the
   Wayland/EGL path would pay off — it would also cover the heap-grown `paste_buf`); idle-frame optimization
   (stop the frame cycle when nothing is dirty — note `redraw()` now calls `layout_apply()` every frame, so
   that path is hotter than it used to be); the color atlas is re-uploaded whole with `glTexImage2D` whenever
   a new emoji is shaped — `glTexSubImage2D` per new shelf would be cheaper; a reflow allocates a fresh row
   buffer per produced line (fine at terminal sizes, but a huge-width window with a huge scrollback churns
   the allocator); the window title is `mux_title()`'s own text and an application's OSC title only reaches
   the tab bar label (ISSUES.md "Known limitations") — showing the ACTIVE session's OSC title in the window
   title is a one-line change in `update_title()`, but it changes visible behaviour and makes the taskbar
   name follow whatever the shell prints, so ask first.
3. Clipboard hardening that niri does NOT require (do not do it unasked — it is untested against a
   compositor that needs it): call `wl_data_offer_accept()` before `wl_data_offer_receive()`, and offer /
   prefer `UTF8_STRING` + `text/plain;charset=utf-8` next to `text/plain`. Current behaviour is verified
   working on niri in both directions.
4. Scratch cleanup: the throwaway probes live OUTSIDE the project tree in `/home/kaida/Project/.scratch/`
   (~125 MB, 160+ entries) — `egl_seq_probe.*`, `egl_probe.*`, `egl_resize_probe.*`, `inject.*`, `keyinj.*`,
   `pinject.*` (pointer injector, session 49), `s61_apinject.*` (the ABSOLUTE pointer injector — the only
   aimable one, session 61), `s61/` (`tabs.sh`, `barclick.sh`, `panemap.sh`, `asan_tabs.sh`, `kterm_asan`),
   `mk_repeat_probe.sh` + `repeat_probe.c`, `reflow_probe.c`,
   `reflow_live*.sh`, `bisect/`, `paste_probe.c`, `iso*.c`, `kterm_asan`, `dbg_oldvt`, `s45_old/` (the last
   user-confirmed-good tree), `s47_before/`, `fs2/`, `fs3/`, `fs_frames/`, `s46_frames/`, `s48_frames/` +
   `s48_win.mp4`, the `s49_*.sh` clipboard harnesses, the `AGENT.md.before_prune` /
   `AGENT.md.before_restructure` / `AGENT.md.session48_preprune` journal
   copies, and the `*.log`/`*.sh` evidence the journal cites. Journal entries reference them as evidence;
   delete them once the user has confirmed the behaviour they cover (everything they covered is now
   confirmed EXCEPT the tab bar / side-by-side view, so keep `s61/` and `s61_apinject*` until the user has
   looked at that feature — keep `s45_old/` and `s47_before/` if another draw-path regression is ever
   suspected, they are the A/B trees).
5. Emoji paths that exist but have NEVER run here (verify before trusting them): the SCALABLE color
   font branch (`FT_IS_SCALABLE` -> `FT_Set_Pixel_Sizes` + `hb_ft_font_changed` on zoom) — this box only
   has the bitmap-only Noto CBDT font; and a cluster that HarfBuzz renders as more than one glyph is
   deliberately rejected (`ng == 1` check) and falls back to mono, which is the right behaviour but is
   untested against a font that does it.
6. Git: the tree is committed and pushed — the sessions 56-62 work went in as `d05c817` during session 62 and
   `origin/main` == `main` == that commit (see the session-62 Progress entry). `origin` is
   `https://github.com/K-code-AI/Kai-Term` (the user renamed the repository; verified with `git remote -v`
   and a successful push on 2026-10-16). Keep committing verified work and keep `bin/`/`build/` out of it;
   `git fetch` before pushing, the user commits on GitHub too.

## Project Files
```
Kai-Term/
├── .gitignore                 # bin/, build/, *.o, *.obj, *.d, *.so, *.a, editor leftovers
├── LICENSE                    # MIT License, "Copyright (c) 2026 Kaida"
├── AGENT.md                   # this journal
├── ISSUES.md                  # open bugs + known limitations + fixed history (root causes)
├── README.md                  # features, deps, build, flags, config, shortcuts
├── Makefile                   # src/*.c -> build/*.o -> bin/kai-term; test/test-cli/asan/run/clean; WARN set
├── docs/ARCHITECTURE.md       # module map, event loop, grid invariants, reflow, color emoji, debug entry points
├── include/kai-term/kai-term.h  # the single shared header (all types + prototypes)
├── src/                       # config.c vt.c mux.c layout.c pty.c font.c render.c main.c
├── tests/test_vt.c            # 501 checks: parser, grid, selection (incl. sub-cell), alt screen,
│                              #   live_off, emoji clusters (ZWJ / skin tone / VS16 / keycap / flag pair),
│                              #   CSI 3 J, bracketed paste, term_init guard, REFLOW (round trip, cursor,
│                              #   wide pairs, eviction, hard breaks, coloured blanks, alt screen),
│                              #   term_grid_geometry (padding + the pointer/cell sweep), the zoom/alt-screen
│                              #   row-shrink rule, a viewport grown past half of rcap (the row[] overrun),
│                              #   the evdev -> SGR button mapping, mux (tab open/close/goto/next/prev,
│                              #   max_tabs, mux_title, the ps.t re-arm), keybind/keymods parsing, the
│                              #   multiplexing config keys, and layout_compute (bar strip, item rects +
│                              #   labels, pane rects, the hit tests, layout_grid_size, and the
│                              #   layout_initial_size round trip that pins the startup height)
├── tests/dbg.c                # grid-model invariant checker (ed3*, resize, reflow, grow-then-scroll and
│                              #   zoom/alt-screen permutation stages)
├── tests/cli_test.sh          # 86 checks over the real bin/kai-term: every flag, long form, error message
│                              #   and exit code, plus the multiplexing/layout config keys and the `layout:`
│                              #   log line (main() is in no test binary, so this is the only honest way to
│                              #   cover argv); run by `make test-cli`, which `make test` needs.
├── bin/                       # kai-term, test_vt, dbg (+ *_asan)   [generated]
└── build/                     # *.o + generated/xdg-shell-*        [generated]
```
- `src/vt.c` — the unit-tested core: parser + grid + selection + alt screen + the evdev -> SGR mouse-button
  mapping (`mouse_sgr_button`) + `term_grid_geometry()`. No Wayland/GL deps.
- `src/main.c` — all Wayland input/shell/clipboard/epoll logic; look here for anything involving
  pointer, keyboard, resize, or frame pacing.
- `src/render.c` — EGL + GL 2.x, two atlases (alpha8 mono + RGBA8 color emoji), one shader, one draw
  call (instanced or expanded), three back-to-front passes.
- `src/font.c` — fontconfig + FreeType alpha8 atlas, shelf packer, hash cache, fontconfig fallback
  faces, HarfBuzz-shaped color emoji (`font_color_glyph`) + RGBA8 color atlas, `font_resize`.
- `src/pty.c` — PTY spawn, child session setup, resize, TERM/COLORTERM/TERM_PROGRAM env.
- `src/config.c` — `key = value` config parser + defaults + clamping, and the key-binding spec parser
  (`keybind_parse`/`keymods_parse`).
- `src/mux.c` — the tab bookkeeping of one window: the dense `Tab` array, active index, open/close/goto/
  next/prev, the attention flag and `mux_title()`. No Wayland, no EGL, no fd, no fork — that is what makes
  it unit-testable.
- `src/layout.c` — pure geometry: window px + cell px + padding + mode + the tab list in, the bar strip,
  item rects + labels and the pane rects (each with its cols/rows) out, plus the hit tests,
  `layout_grid_size()` (the one grid size every session of a layout gets) and `layout_initial_size()` (the
  window to ask for at startup, the inverse of `layout_compute()`). main.c resizes/hit-tests through it,
  render.c draws exactly what it says.
- `Makefile` — pkg-config-driven; `-Iinclude/kai-term -Ibuild/generated`; output dirs created by
  order-only prerequisites so they never force rebuilds; targets `all test test-cli asan run clean`.

## Key Decisions & Pitfalls
Design invariants and Wayland protocol gotchas are documented in docs/ARCHITECTURE.md — grid top-left
anchoring == pointer mapping, `wl_fixed_t` fixed-point coordinate conversion, selection compositing
without GL blending, the rotation/`live_off` grid model, `cy` as a live-view coordinate, alternate screen,
the push-based clipboard, seat-v5 listener slots, and instancing (`GL_ARB_vertex_instanced` does not
exist). Bug root causes are in ISSUES.md "Fixed". The decisions below are project-level and journal-specific:
- **Pitfall — `eglSwapBuffers` is a blocking call, and a blocked swap looks like a dead terminal.** It
  waits until the driver gets a buffer back, and only the compositor releases buffers, so a client whose
  surface the compositor is not rendering stalls inside the swap: no keyboard, no PTY reads, no redraw
  (measured directly — `/proc/<pid>/wchan` = `futex_do_wait` — with a standalone EGL client, and
  `eglSwapInterval(0)` does NOT change it on this NVIDIA driver). The rule that follows is about the
  wayland READ, not about listeners — session 47 corrected the earlier "never swap inside a listener"
  wording, which was BACKWARDS and had caused a regression: a swap must never run while a
  `wl_display_prepare_read()` is outstanding, so the event loop finishes the read and dispatches pending
  events BEFORE it handles the pty/timerfd/signalfd/paste/repeat fds. Swapping inside a listener is fine,
  because the read is already complete there. The other three consequences stand: keep at most one buffer
  in flight (`W.frame_cb`), attach the FIRST buffer in the FIRST configure handler (a window without a
  buffer never maps, so it never gets a frame callback), and answer a size change with a buffer of the new
  size — otherwise the compositor has no reason to repaint and the client waits forever (that is the
  fullscreen bug). Answering EVERY configure with a swap is still wrong (session 45's attempt did and then
  spammed `eglSwapBuffers failed: 0x3003` every frame; those errors pre-date that change and appear when
  the compositor is not showing the surface, but the swap storm itself came from ignoring the one-buffer-
  in-flight gate).
- **Pitfall — a Wayland frame callback armed on a commit that attaches no buffer may never fire.**
  Damage-driven compositors deliver frame callbacks during a render pass; an empty commit gives them no
  reason to run one. Every draw path here is gated on "no callback in flight", so such a callback freezes
  the display until something else repaints the output. `solicit_frame()` was deleted for exactly this
  reason; `redraw()` also refuses to arm when the swap failed.
- **Dependencies:** libwayland-client, wayland-cursor, wayland-egl, libxkbcommon, freetype2,
  fontconfig, harfbuzz (required since session 37 — README must keep listing it), EGL/GL only —
  deliberately no cairo/pango/Qt/GUI framework. xdg-shell bindings are generated at build time, not
  vendored. (Full list in README.md.)
- **Naming:** project/binary/identifiers use `kai-term` (`kai_term` for C identifiers), the display name is
  `Kai-Term`. RENAMED from `K-Term`/`k-term` on 2026-10-16 (session 59) at the user's request, to avoid a
  name conflict with another project of theirs — do not "restore" the short form anywhere: the directory,
  `bin/kai-term`, `include/kai-term/kai-term.h`, `DEF_APP_ID`/`DEF_TITLE` ("Kai-Term"), the log prefix
  `kai-term <level>:`, `TERM_PROGRAM=kai-term` and `~/.config/kai-term/kai-term.conf` all have to move
  together, and tests/test_vt.c pins the two `DEF_*` literals so a half-rename fails a check.
- **Layout:** sources in `src/`, public header in `include/kai-term/`, tests in `tests/`, binaries in
  `bin/`, objects + generated bindings in `build/`, extra docs in `docs/`. `make clean` deletes
  `bin/` and `build/` entirely.
- **License:** MIT (`LICENSE`, "Copyright (c) 2026 Kaida"), the user's choice. MIT requires the
  copyright + permission notice in all copies, so `LICENSE` must ship with any redistribution. No
  source-file SPDX headers exist; if added they must read `SPDX-License-Identifier: MIT`.
- **Git:** the project IS on GitHub now (the user set this up after session 49). Remote `origin =
   https://github.com/K-code-AI/Kai-Term` (renamed from `.../K-Term` by the user; verified by `git remote -v`
   and a successful push on 2026-10-16, session 62), branch `main`, and the agent has push access (the user
   said so explicitly on 2026-10-15). Before session 50 there were two commits, both named "Initial commit"
   (`739013c` = .gitignore + LICENSE, `18b9732` = the 15 source/doc files). Session 50 added the cleanup
   commit. The old "zero commits, nothing is pushed without the user" rule is OBSOLETE — but still run
   `git log --oneline` / `git status` before assuming anything is committed, and never commit `bin/` or
   `build/` (the session-50 `.gitignore` covers them; the previous one, a kernel-module template, did
   NOT, which is why a build used to show up in `git status`).
- **Pitfall — splicing this journal with computed indices has damaged it twice (sessions 54 and 55).**
  Both accidents had the same cause: an anchor string that also appears INSIDE a journal entry (entries
  quote section headings like `## Next Steps` / `## Environment` when describing earlier accidents), or an
  index captured before an earlier mutation in the same script. Rule: anchor on text that is unique in the
  file (check with `grep -c` first), re-find every anchor after each mutation, and `git diff` the result
  before moving on. Session 55's accident cut a Progress entry in half and moved it next to a heading it
  quoted; it was repaired from `/tmp/s55/AGENT.md.before_repair`, and the entry that describes it is the
  session-54 one.
- **Pitfall — a log line that prints the value it was GIVEN can lie.** `tab_switch(idx)` logged `mux: tab
  idx/n visible` while nothing had changed the active tab, so the log looked like proof that the click worked
  and the bug survived every reading of it (session 61). A log line about state must print what the owner of
  that state REPORTS (`mux_active()` after `mux_goto()`), not the requested value.
- **Pitfall — the evdev button order is not the protocol order.** `BTN_LEFT` 0x110, `BTN_RIGHT` 0x111,
  `BTN_MIDDLE` 0x112, but SGR numbers them 0 left, 1 middle, 2 right. A switch that assumes the kernel order
  is `BTN_RIGHT`/`BTN_MIDDLE` swapped — silently, in both the bar's middle-click-close and every SGR mouse
  report to vim/htop. The mapping is `mouse_sgr_button()` in src/vt.c with the raw literals pinned by checks
  (ISSUES.md "Fixed").
- **Decision — geometry lives in src/layout.c, not in main.c.** main.c is in no test binary, so any arithmetic
  left there is untestable, and the tab bar + panes need N rectangles with N origins, which one global offset
  cannot express. `layout_compute()` (bar strip, item rects, labels, pane rects, cols/rows per pane),
  `layout_grid_size()` (the one size every session of a layout gets, so a tab switch never resizes) and
  `layout_initial_size()` (the window to ask for at startup) are pure functions, and the round trip
  `layout_initial_size()` -> `layout_compute()` -> `layout_grid_size()` is what proves the startup height is
  right — that check is exactly what caught the `cell_w`/`cell_h` typo (session 62). The same reasoning put
  `mouse_sgr_button()` in vt.c.
- **Pitfall — `Term`'s pen must never colour the chrome.** A `Term`'s fg/bg are the application's CURRENT SGR
  pen, so drawing the tab bar from them would let a `printf '\e[31m'` repaint the bar that names the tab. The
  bar and the focus border are mixed from `Render.bg`/`Render.fg`, which come from the CONFIGURED palette
  (`render_init(..., uint32_t bg, uint32_t fg)`).
- **Pitfall — a `(void)x;` line does not mean `x` is unused.** `csi_dispatch` carried `(void)def1;`,
  which reads like "this local is a leftover"; deleting `def1` broke the build because it is really used
  by `CSI J` (erase display), `CSI K` (erase line) and `CSI 6 n` (CPR). Session 50 removed only the cast
  and documented the variable. Always grep for uses before deleting anything "dead", and let the compiler
  confirm it (`make` after every removal).
- **Pitfall — `Term` is not a place for settings.** `ESC c` / RIS calls `term_init`, which `memset`s the
  whole `Term` (vt.c), so anything stored there is wiped by a reset. Process-wide settings live as
  file-scope values with a setter (`kai_term_loglevel`, `g_clear_scrollback`) and are applied from the
  config in main.c. (`bracketed_paste` is deliberately the opposite case: it is application-negotiated
  state, so a reset SHOULD clear it.)
- **Pitfall — a boolean "is it initialized" flag cannot survive uninitialized memory.** `Grid.valid` is a
  `GRID_VALID` sentinel, not 0/1: on a stack `Term` the garbage in that field measured `32511`, so the free
  path was taken anyway. Any future "has this been initialized yet" guard here needs the same treatment;
  the test that proves it (`dirty_stack()` + a stack `Term`) is in tests/test_vt.c.
- **Pitfall — the tty input buffer splits big writes.** A single `write()` of a large paste to the PTY
  master is capped by the line discipline, which would separate the bracketed-paste delimiters from each
  other, so `paste_flush()` chunks at 4096 bytes. Related: a short `read()` from the clipboard pipe is NOT
  end-of-paste — only EOF (or the 400 ms idle watchdog) is.
- **Pitfall — a sanitizer target with an incomplete prerequisite list produces confident, wrong results.**
  `bin/dbg_asan` compiles `src/vt.c` directly but only declared `tests/dbg.c` as a prerequisite, so make
  kept running a stale binary and reported a 312-byte leak that the current source did not have. Both
  sanitizer rules now list `src/vt.c src/config.c $(HDRS)`. When a sanitizer finding contradicts the
  tests, check the binary's mtime against the sources BEFORE believing it.
- **Decision — reflow keeps a per-row byte array instead of restructuring the grid.** `Grid.wrap` rotates
  with `row[]`; that is the cheapest way to carry the soft-wrap mark without touching `Cell`/`Row` or the
  permutation invariant `tests/dbg.c` enforces. Consequences a future change must respect: the mark must
  be cleared by every operation that blanks or erases a slot (`grid_blank_slot`, `erase_line`), and
  `grid_resize_plain()` must clear the marks of truncated rows (a truncated wrapped line really does end
  at the new margin, so claiming it continues would invent content).
- **Pitfall — reflow's three subtle traps (each cost a real bug here).** (1) A row's trailing cells must
  be measured with `row_used()`/`cell_pristine()`, not the full width: joining full-width rows turned the
  padding a wide glyph leaves at a row end into a literal space in the middle of the text on the second
  reflow. (2) The wide-pair guard must peek the NEXT source cell before taking the chunk's last one, and
  only react to an `ATTR_WIDE` cell at the START of the next chunk — reacting to a pair that legitimately
  ENDED a chunk loses a character. (3) The eviction step must run for EVERY produced row, blank rows
  included; otherwise `kept` can exceed `rcap` and the commit drops buffers instead of installing them
  (a leak). `ulen[]` is precomputed once and used for BOTH the cursor's logical column and the chunk walk
  so the two cannot disagree.
- **Pitfall — a Kai-Term-to-Kai-Term clipboard test proves nothing.** Every paste test run before session 49
  moved text between two Kai-Term instances, which works even if the client never shares its selection with
  the rest of the desktop. The only meaningful test uses a FOREIGN client: `wl-copy` in, `wl-paste` out
  (`wl-paste --list-types` also shows what the compositor advertises for the current owner). Related: a
  Wayland clipboard is not a buffer the client can read on demand — the compositor pushes an offer
  (`wl_data_device.selection`) and the source serves the bytes on request, so "paste" only works if the
  earlier "copy" actually called `set_selection`. With `copy_on_release = 0` (the default since session 43)
  a mouse drag does NOT call it; only `Ctrl`+`Shift`+`C` does. That is the behaviour that reads like
  "Kai-Term keeps a private clipboard".
- **Pitfall — identifying a test window on the user's compositor.** Never by title (session 42 closed the
  user's pcmanfm-qt window because its title was "K-Term" (the pre-rename `DEF_TITLE`); `$!` after `timeout ./bin/kai-term &` is the
  timeout wrapper's pid, not kai-term's. Use "the window id that appeared after launch" (`.scratch/reflow_live.sh`
  does). niri's `--json windows` has no absolute screen position, so screenshots cannot be cropped to a
  window from it, and tiling/floating moves the OTHER windows too, which breaks screenshot pixel-diffs.
