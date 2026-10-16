# Kai-Term — Agent Journal

Read this + ISSUES.md before touching project. User-facing docs: README.md. Bug root causes: ISSUES.md
"Fixed". Host/tool facts: `/home/kaida/Project/ENVIRONMENT.md` (outside repo on purpose — not project
documentation).

> Renamed 2026-10-16 (session 59): `K-Term`/`k-term` -> `Kai-Term`/`kai-term` (dir, header
> `include/kai-term/kai-term.h`, config `~/.config/kai-term/kai-term.conf`, app id `Kai-Term`). Old journal
> entries and ISSUES.md "Fixed" keep old spelling — they record commands really run and log lines really
> printed. Pre-refactor journal: `git show 1ab9dc8:AGENT.md`.

## Project Overview

Wayland terminal emulator in C. xdg-shell window + PTY shell + VT/ANSI parser + EGL/OpenGL text rendering, 2
glyph atlases (alpha8 mono, RGBA8 color emoji), 1 draw call/frame. In-window multiplexing: N sessions per
window, tab bar, side-by-side panes. No GUI framework.

## Current Status (build/test/asan re-verified 2026-10-17, session 65)

- Clean tree: `make` exit 0, **0 warnings** under `WARN` (`-Wall -Wextra -Wshadow -Wwrite-strings
  -Wformat=2 -Wdiscarded-qualifiers`). `make test` -> **501 checks, 0 failures** + **MODEL OK (0 problems)** +
  **CLI: 86 checks, 0 failures, 0 skipped**. `make asan` -> 501/0 + MODEL OK, no LeakSanitizer/UBSan report.
- No probe/experiment left in tree: `grep -rnE "TEMPORARY|PROBE|EXPERIMENT|NEGATIVE" src/ include/ tests/` ->
  empty.
- README flag table (`-e -w -c -d -v -h`) + 27 settings rows match `src/main.c` / `src/config.c`.
- **No open issues.** The 4 ex-"Open" items cleared unproven 2026-10-17 (user request; none ever reproduced
  here). Under "Cleared", NOT "Fixed" — no fix was verified. Recurrence -> new Open item with reproduction.
- **User-confirmed by eye in their window:** multiplexing + tab bar + side-by-side ("works as expected and
  looks great", 2026-10-17); zoom (no stale prompt lines), padding, fullscreen refresh, black-window/no-input
  fix, heavy-output segfault fix, transparency, bracketed paste, `clear` drops scrollback, shell start dir,
  color emoji, text selection, `app_id`, several instances at once, window resize, key-hold repeat, clipboard
  interop both directions.
- Verified at model+live level (not all re-checked by hand): reflow on width change, color emoji + grapheme
  clusters, whole-glyph selection with no band over unwritten space, compositor transparency, CSI 3 J +
  startup dir, client-side key repeat, draw/commit + event-loop pacing.
- Git: `origin = https://github.com/K-code-AI/Kai-Term`, `main` in sync with `origin/main` (`d05c817`,
  `3351214`, `dbcc597`, `a2a5a54`, `1ab9dc8` pushed 2026-10-17). `git fetch` before push — user commits on
  GitHub too. Never commit `bin/` or `build/`.

## Current Task

**Nothing in progress** (s65 closed: AGENT.md rewritten in telegraphic agent-facing style, 4 passes — see the
s65 journal entry). Now 469 lines / 37.2 KB / 5232 words, from 526 / 45.8 KB / 6679.

Remaining size is content, not English: Architecture & Invariants 35.7% of words, Progress Journal 20.8%,
Project Files & Facts 13.4%. Going smaller needs a structural choice, not more word-cutting — move
Architecture + Debugging into a separate `DESIGN.md` (AGENT.md would drop to ~250 lines) or accept losing
invariant detail. Ask before either.

NEXT ACTION: pick from "Next Steps" (housekeeping — no bug is open), or re-file a cleared report with a
reproduction if user hits one again.

## Progress Journal (root causes: ISSUES.md "Fixed")

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

## Next Steps

1. **No open bugs.** ISSUES.md has no "Open" section (see "Cleared"). If a cleared report recurs, re-file it
   with a reproduction and start from measurements already there — for second-instance crash: source +
   headless harness, not screen.
2. Housekeeping, none a bug:
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
3. Clipboard hardening niri does NOT require (do not do unasked — untested against a compositor that needs
   it): `wl_data_offer_accept()` before `wl_data_offer_receive()`; offer `UTF8_STRING` +
   `text/plain;charset=utf-8` next to `text/plain`.
4. Emoji paths that exist but have NEVER run here: SCALABLE color font branch (`FT_IS_SCALABLE` ->
   `FT_Set_Pixel_Sizes` + `hb_ft_font_changed` on zoom) — this box has only a bitmap-only Noto CBDT font; and a
   cluster HarfBuzz renders as >1 glyph, deliberately rejected (`ng == 1`), falls back to mono.
5. Scratch cleanup: throwaway probes live in `/home/kaida/Project/.scratch/` (outside project). Tab bar /
   side-by-side is confirmed, so `s61/` + `s61_apinject*` need no longer be kept; keep `s45_old/` +
   `s47_before/` as A/B trees in case another draw-path regression is suspected. Ask before deleting anything
   outside repo — not done.
6. Git: keep committing verified work; `git fetch` before pushing.

## Project Files & Facts

```
Kai-Term/
├── .gitignore                   # bin/, build/, *.o/*.obj/*.d/*.so/*.a, editor leftovers, docs/ARCHITECTURE.md
├── LICENSE                      # MIT, "Copyright (c) 2026 Kaida"
├── AGENT.md                     # this journal
├── ISSUES.md                    # cleared reports + known limitations + fixed history (root causes)
├── README.md                    # features, deps, build, flags, config keys, shortcuts
├── Makefile                     # src/*.c -> build/*.o -> bin/kai-term; all/test/test-cli/asan/run/clean
├── include/kai-term/kai-term.h  # the single shared header: all types, prototypes, log levels, helpers
├── src/                         # config.c vt.c mux.c layout.c pty.c font.c render.c main.c
├── tests/test_vt.c              # 501 checks (see below)
├── tests/dbg.c                  # grid-model invariant checker (permutation + scroll_off/live_off bounds)
├── tests/cli_test.sh            # 86 checks driving the REAL bin/kai-term (argv + config keys + log lines)
├── bin/                         # kai-term, test_vt, dbg (+ *_asan)          [generated, git-ignored]
└── build/                       # *.o + generated/xdg-shell-*                [generated, git-ignored]
```

- `src/vt.c` — unit-tested core: VT/ANSI/UTF-8 parser, grid model, selection text extraction, alt screen,
  `term_grid_geometry()`, evdev -> SGR button mapping (`mouse_sgr_button`). No Wayland/GL dependency.
- `src/main.c` — all Wayland shell/input/clipboard/epoll logic: xkb keyboard, client-side key repeat, session
  shortcuts, pointer (bar clicks, pane focus, selection, mouse reporting, wheel), `layout_apply()`,
  frame-paced redraw, one epoll loop over every session's pty fd, SIGCHLD reaping by pid, CLI flags,
  data-device clipboard. Look here for pointer, keyboard, resize, frame pacing.
- `src/render.c` — EGL (platform Wayland) + GL 2.x: 2 atlases, 1 shader, 1 draw call (instanced or expanded),
  3 back-to-front passes; draws what `src/layout.c` says, owns no geometry.
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
- `tests/test_vt.c` — 501 checks: parser, grid, selection, alt screen, `live_off`, emoji clusters, CSI 3 J,
  bracketed paste, `term_init` guard, reflow, `term_grid_geometry`, zoom/alt-screen row-shrink,
  `mouse_sgr_button`, mux, keybind/keymods parsing, multiplexing config keys, `layout_compute` /
  `layout_grid_size` / `layout_initial_size`. The source is list of what is covered; 2 invariants with
  dedicated checkers are `row[]` permutation (also `tests/dbg.c`) and startup-size round trip.
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

Rules a change must not break. Why each exists: ISSUES.md "Fixed".

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

- 2 textures (alpha8 mono, RGBA8 color emoji), each uploaded only when dirty, AFTER emit passes and BEFORE
  draw — glyph lookups run inside pass 3, so uploading at top of `render_draw` shows every new
  glyph one frame late (and a first emoji as a black box).
- One shader; `a_glyph` selects path: 0 background quad, 1 mono glyph (tinted by `v_fg`), 2 color emoji
  (premultiplied, not tinted).
- Pass order back to front: all cell backgrounds, then selection bands, then glyph quads — a WIDE glyph
  quad spans into next cell and neighbour's background must not be emitted after it. Continuation
  cells (`ATTR_WIDE`) are skipped in pass 3.
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
