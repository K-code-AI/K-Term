# Kai-Term — Agent Journal

Read this, then ISSUES.md, before touching the project. User-facing behaviour and flags: README.md.
Bug root causes: ISSUES.md ("Fixed"). Host facts, available tools and debugging techniques:
`/home/kaida/Project/ENVIRONMENT.md` (outside this repo, deliberately — it is not project documentation).

> **Renamed 2026-10-16 (session 59):** the project is `Kai-Term` (binary `kai-term`, header
> `include/kai-term/kai-term.h`, config `~/.config/kai-term/kai-term.conf`, app id `Kai-Term`); it used to be
> `K-Term`/`k-term`. Dated journal entries and the ISSUES.md "Fixed" root causes keep the spelling they were
> written with, because they record commands that were really run and log lines that were really printed.
> Full pre-refactor journal text: `git show 1ab9dc8:AGENT.md`.

## Project Overview

Kai-Term is a lightweight Linux terminal emulator for Wayland written in C: native Wayland window
(xdg-shell) + PTY shell + VT/ANSI parser + EGL/OpenGL text rendering from two glyph atlases (alpha8 mono +
RGBA8 color emoji) in one draw call per frame, plus in-window multiplexing (several sessions per window,
a tab bar and a side-by-side pane view). No GUI framework.

## Current Status (build/test/asan re-verified 2026-10-17, session 65)

- Clean tree: `make` -> exit 0, **0 warnings** under the Makefile's `WARN` set (`-Wall -Wextra -Wshadow
  -Wwrite-strings -Wformat=2 -Wdiscarded-qualifiers`); `make test` -> **`501 checks, 0 failures`** +
  **`MODEL OK (0 problems)`** + **`CLI: 86 checks, 0 failures, 0 skipped`**; `make asan` -> `501 checks,
  0 failures` + `MODEL OK`, no LeakSanitizer/UBSan report. No temporary probe or experiment remains in the
  tree (`grep -rnE "TEMPORARY|PROBE|EXPERIMENT|NEGATIVE" src/ include/ tests/` -> nothing). README's flag
  table (`-e -w -c -d -v -h`) and its 27 settings rows match `src/main.c` and `src/config.c`.
- **No open issues.** The four items that were in ISSUES.md "Open" were cleared unproven on 2026-10-17 at the
  user's request (none ever reproduced here). They sit under "Cleared", NOT under "Fixed" — nothing was
  verified as a fix. If one recurs it gets re-filed as a new Open item with a reproduction.
- **User-confirmed in their real window:** multiplexing, the tab bar and the side-by-side view ("it works as
  expected and looks great", 2026-10-17); zoom (no stale prompt lines), window padding, fullscreen refresh,
  the black-window/no-input fix, the heavy-output segfault fix, transparency, bracketed paste, `clear`
  dropping the scrollback, the shell's starting directory, color emoji, text selection, the configurable
  `app_id`, several instances at once, window resizing, key-hold repeat, and clipboard interop with other
  applications (both directions).
- Implemented and verified at model + live level (not every one re-checked by hand): reflow on width change,
  color emoji + grapheme clusters, whole-glyph selection with no band over unwritten space, compositor
  transparency, CSI 3 J + startup directory, client-side key repeat, the draw/commit + event-loop pacing
  rules.
- Git: `origin = https://github.com/K-code-AI/Kai-Term`, branch `main`, in sync with `origin/main`
  (`d05c817`, `3351214`, `dbcc597`, `a2a5a54`, `1ab9dc8` all pushed on 2026-10-17). `git fetch` before
  pushing — the user commits on GitHub too. Never commit `bin/` or `build/`.

## Current Task

**In progress (session 65): AGENT.md refactor. Pass 1 (in-place) is done and committed — 526 -> 482 lines
before this journal entry, 45.5 -> 39.8 KB, no fact lost (see the session 65 entry for the method and the
fact check).**

**A decision is waiting on the user:** in-place condensing has plateaued. Architecture & Invariants (165
lines) + Debugging (28) are 193 lines / 15.3 KB — 40% of the file — and they are reference material, not
padding. To make AGENT.md much smaller they have to move to a separate `DESIGN.md` (AGENT.md would land near
290 lines). That reverses session 63's consolidation of `docs/ARCHITECTURE.md` into this file, so it was not
done unasked. If the answer is no, ~480 lines is roughly the honest floor for this much project.

NEXT ACTION: get that answer. If yes: create `DESIGN.md`, leave a pointer here, re-run the fact-preservation
check, re-verify build/test/asan, commit and push.

## Progress Journal (condensed; root causes live in ISSUES.md "Fixed")

- **2026-10-12/14, sessions 1-12** — scaffold, first successful build, then the first ten fixes: the vt.c
  grid model rewritten as a rotation-based permutation (`row[]` always valid), CSI parameter semantics, the
  event loop rewritten to the canonical `prepare_read -> epoll_wait -> read_events` pattern, EGL/FreeType
  init, and rendering correctness (vertex stride, MVP mapping, 6-vertex quads, atlas upload format,
  first-frame bootstrap, clearing realloc-grown columns). The first two user bugs: black window (font
  metrics in font units, not pixels) and a 10 s startup delay (unanswered DA1/CPR/DA2 queries).
- **2026-10-14, sessions 13-23** — hardware instancing done correctly; alternate screen, SGR mouse
  reporting, scrollback keyboard navigation, zoom, text selection + Wayland clipboard; mouse-wheel scrolling
  (seat v5) and the `Grid.live_off` anchor that fixed wheel direction and auto-follow. Then the tree
  restructured to the project template, Makefile rewritten, `.gitignore` + stricter warnings, docs
  rewritten, journal pruned to era level.
- **2026-10-14, sessions 24-32** — text-selection precision overhaul: sub-cell endpoints, per-fragment
  highlight compositing without GL blending, then the two root causes of the selection-vs-pointer complaint
  (grid anchoring, `wl_fixed_to_int`). User confirmed selection working.
- **2026-10-14, sessions 33-38** — doc audit; fontconfig fallback faces with the source face stored per glyph
  so `atlas_grow` re-renders from the right one; the three back-to-front render passes; COLOR emoji end to end
  — clusters assembled in vt.c, ONE glyph id from HarfBuzz, FreeType color bitmap, RGBA8 atlas, un-tinted in
  the shader, wide emoji reserve two cells, flag pairs and U+FE0F/U+20E3 presentation.
- **2026-10-14, session 39** — CSI 3 J so `clear` drops the scrollback (gated by `clear_scrollback`), and
  the shell starting directory (`-w DIR` > config `working_dir` > `$HOME`, tilde expansion, `stat` check).
- **2026-10-15, session 40** — bracketed paste (DEC 2004, `paste_wrap()` as the tested reference, buffered,
  <=4096 B chunks, 8 MiB cap, 400 ms idle watchdog); the latent `term_init` free-of-uninitialized-grid bug
  and the `GRID_VALID` sentinel.
- **2026-10-15, sessions 41-43** — `app_id` config key; real window `opacity` (premultiplied source-over in
  the shader); selection endpoints snapped to whole cells; no band over never-written cells; **REFLOW** on
  width change (`Grid.wrap` soft-wrap marks, `grid_reflow()`, `grid_resize_plain()` for the alternate screen,
  cursor travels with its logical cell, narrowing evicts the oldest rows).
- **2026-10-15, sessions 44-45** — CLIENT-SIDE KEY REPEAT (`krfd` timerfd + `send_key`'s repeatability
  result + `repeat_delay`/`repeat_rate`), user-confirmed; CLI surface cut back; `copy_on_release` toggle
  (default off); the fullscreen freeze's root cause found (a frame callback armed on an empty commit).
- **2026-10-15, sessions 46-47** — the draw/commit cycle rewritten into the three frame-pacing rules now
  under "Architecture & Invariants". Also the pre-existing segfault on heavy output in a grown window:
  `sb_limit() = min(scrollback_max, rcap - rows)` as the single bound. Both user-confirmed.
- **2026-10-15, sessions 48-49** — docs/state audit; a "K-Term has its own clipboard" report investigated
  against a foreign client and NOT reproduced (likely the intentional `copy_on_release = 0` default). No
  source change.
- **2026-10-15, session 50** — cleanup + git: project put on GitHub, dead code removed, dangling
  `NextFeature #N` references deleted, `WARN` set tightened (tree clean under it), `.gitignore` replaced
  (it had ignored neither `bin/` nor `build/`).
- **2026-10-15, session 51** — CLI: config flag is `-c` (was `-C`), retired-flag messaging deleted, one
  generic unknown-option message.
- **2026-10-15, sessions 52-55** — `tests/cli_test.sh` + `make test-cli` (drives the real binary, because
  `main()` is in no linked test); the zoom bug (stale prompt lines) root-caused in `grid_resize_rows`;
  WINDOW PADDING (`padding` key + the single `term_grid_geometry()` helper that decides cols/rows AND the
  grid origin), which exposed the `DEF_BG` clear-colour bug, fixed the same session. Padding and zoom
  user-confirmed.
- **2026-10-15/16, sessions 56-58** — MULTIPLICING (`src/mux.c`: one window, several `{Term, Pty}` sessions,
  config-driven key bindings) and TAB BAR + SIDE-BY-SIDE VIEW (`src/layout.c`: pure geometry) designed and
  built; session 58 reported a fullscreen-freeze regression.
- **2026-10-16, session 59** — the RENAME `K-Term` -> `Kai-Term` (directory, header, binary, config path,
  `DEF_APP_ID`/`DEF_TITLE`, log prefix, `TERM_PROGRAM`, Makefile, the tests that pin those literals),
  measured against a pre-rename baseline. The four items OUTSIDE the project (niri window rule,
  `/usr/local/bin/k-term`, `/usr/local/bin/desktop-terminal` + its .desktop entry, the GitHub repo name)
  were listed for the user; the repo rename is done, the other three were still pending when last checked.
  The fullscreen freeze came back intermittently and the user deferred it (then ISSUES.md Open #4).
- **2026-10-16, sessions 60-62** — sessions 56/57 finished: `src/main.c` wired to `layout_apply()` +
  `render_init(..., conf.fg)` + `render_draw(&rend, &font, &g_layout, terms)` (which is what made the
  binary link again), the live key/pointer-injection verification run, and three real defects fixed —
  a bar/pane click that logged a switch it never made (`tab_switch()` never called `mux_goto()`),
  `BTN_RIGHT`/`BTN_MIDDLE` swapped in the SGR mapping, and `tab_bar = always` asking the compositor for
  `cell_w` instead of `cell_h` of extra height (startup grid one row short; the arithmetic moved into
  `layout_initial_size()` so a check can pin it). Tests 436 -> 501; committed and pushed as `d05c817`.
- **2026-10-16, session 63 — DOCS CLEANUP.** AGENT.md had grown to ~2070 lines, mostly per-session detail
  duplicating the era-level journal and the ISSUES.md root causes, and `docs/ARCHITECTURE.md` mixed project
  design with facts about this machine that must not ship inside the project. Created
  `/home/kaida/Project/ENVIRONMENT.md` outside the repo — host/toolchain/library/tool inventory plus every
  debugging technique that works here and the traps found with them; project-agnostic by design, and named
  ENVIRONMENT.md rather than ARCHITECTURE.md because the name has to match the content.
  `docs/ARCHITECTURE.md` removed (`git rm`, path git-ignored so it cannot come back) with its design
  invariants condensed into "Architecture & Invariants" below and its debug entry points into "Debugging
  this project" below. This file rewritten 2067 -> 491 lines (189 KB -> 42 KB): the ~1300-line per-session
  journal and the Environment section went, the era-level journal, invariants, pitfalls, Next Steps, Project
  Files and the issue pointers stayed. README/ISSUES references and `.gitignore` updated.
  Committed `3351214`; pushed in session 64.
- **2026-10-17, session 64 — USER CONFIRMATION + ISSUE CLEAR-OUT + PUSH.** The user looked at the tab bar and
  the side-by-side view in their own window: "it works as expected and looks great" — multiplexing is now
  confirmed by eye, not only by log lines. They also asked to clear the four ISSUES.md "Open" items: never
  reproduced here, and not established Kai-Term defects. ISSUES.md `## Open` (62 lines) became
  `## Cleared (2026-10-17)` — one line each, kept as unproven rather than moved to "Fixed", with the
  `Open #1`-`#4` labels still findable so dated entries resolve; 705 -> 673 lines. Cross-references that
  would otherwise dangle were fixed (the "Fixed: Fullscreen" entry, Current Status, Next Steps, the Project
  Files tree, README's Documentation line). Committed `dbcc597`, `a2a5a54`, `1ab9dc8`; `git fetch` first
  (0 behind, so a fast-forward push).
- **2026-10-17, session 65 — AGENT.md REFACTOR (pass 1, in-place).** Measured before editing: 526 lines /
  45.8 KB, Progress Journal 28.8%, Architecture & Invariants 27.0%, Key Decisions 13.1%; a sentence-level
  diff against ISSUES.md found only 3 near-duplicate pairs, so the size was density, not copy-paste.
  DONE: session 63/64 entries cut to era level (their host inventory — gcc/kernel/niri/GPU versions —
  dropped only after `pkg-config --modversion` re-checks moved the harfbuzz/freetype2/fontconfig versions
  into ENVIRONMENT.md); Architecture & Invariants restructured from 10 prose walls into bulleted invariants
  per module; the project facts that sat in Key Decisions (dependencies, naming, layout, license, git) moved
  into "Project Files & Facts", leaving Key Decisions as bug traps and working rules; the deep journal
  (sessions 1-55) merged into fewer era entries with their mechanism detail cut, since those mechanisms are
  now stated as invariants; the test-coverage enumeration shortened (the test source is the authority).
  INTERNAL DUPLICATION KILLED — three rules were stated twice, once as an invariant and once as a pitfall:
  the reflow traps (folded into the reflow invariant, including the two failure modes the invariant had
  omitted), the `Grid.valid`/`GRID_VALID` sentinel (the `32511` measurement moved into the invariant), and
  the empty-commit frame callback (`solicit_frame()` deletion note moved into frame-pacing rule 1).
  RESULT: 526 -> 482 lines, 45.5 -> 39.8 KB (278 insertions, 323 deletions). Fact check against the
  pre-refactor file: 148 identifiers, 5 absent — 3 in ISSUES.md/ENVIRONMENT.md, 2 `/tmp` log paths. Docs
  only; no source, header, Makefile or test file touched. Re-verified from a clean tree afterwards:
  `make clean && make` exit 0 / 0 warnings, `make test` 501/0 + MODEL OK + CLI 86/0/0, `make asan` 501/0 +
  MODEL OK with no sanitizer report. Pre-refactor text: `git show 1ab9dc8:AGENT.md`.
  NOT ACHIEVED: the ~300-line target. Restructuring alone INCREASED the line count (526 -> 490) because
  bullets need more lines than paragraphs; only deletion moved the number. See Current Task for the
  structural question this raises.

## Next Steps

1. **No open bugs.** ISSUES.md has no "Open" section (see "Cleared"). If the user hits one of those reports
   again, re-file it with a reproduction and start from the measurements already recorded there — for the
   second-instance crash that means the source plus a headless harness, not the screen.
2. Housekeeping options, none of them a bug:
   - Make the ASan build of the `kai-term` binary itself a real Makefile target (only the tests have one).
     The hand-built one covered the mux/layout paths and found the `row[rcap]` overrun, so covering the
     Wayland/EGL path would pay off (it would also cover the heap-grown `paste_buf`).
   - Idle-frame optimization: stop the frame cycle when nothing is dirty — note `redraw()` now calls
     `layout_apply()` every frame, so that path is hotter than it used to be.
   - The color atlas is re-uploaded whole with `glTexImage2D` whenever a new emoji is shaped;
     `glTexSubImage2D` per new shelf would be cheaper.
   - A reflow allocates a fresh row buffer per produced line — fine at terminal sizes, but a huge-width
     window with a huge scrollback churns the allocator.
   - The window title is `mux_title()`'s own text; an application's OSC title only reaches the tab bar
     label. Showing the active session's OSC title in the window title is a one-line change in
     `update_title()`, but it changes visible behaviour, so ask first.
3. Clipboard hardening that niri does NOT require (do not do it unasked — untested against a compositor
   that needs it): `wl_data_offer_accept()` before `wl_data_offer_receive()`, and offering `UTF8_STRING` +
   `text/plain;charset=utf-8` next to `text/plain`.
4. Emoji paths that exist but have NEVER run here: the SCALABLE color font branch (`FT_IS_SCALABLE` ->
   `FT_Set_Pixel_Sizes` + `hb_ft_font_changed` on zoom) — this box only has a bitmap-only Noto CBDT font;
   and a cluster HarfBuzz renders as more than one glyph, which is deliberately rejected (`ng == 1`) and
   falls back to mono.
5. Scratch cleanup: the throwaway probes live in `/home/kaida/Project/.scratch/` (outside the project).
   The tab bar / side-by-side view is now confirmed, so `s61/` and `s61_apinject*` no longer have to be kept;
   keep `s45_old/` + `s47_before/` as the A/B trees in case another draw-path regression is suspected. Ask
   before deleting anything outside the repo — not done.
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

- `src/vt.c` — the unit-tested core: VT/ANSI/UTF-8 parser, grid model, selection text extraction,
  alternate screen, `term_grid_geometry()`, and the evdev -> SGR button mapping (`mouse_sgr_button`).
  No Wayland/GL dependency.
- `src/main.c` — all Wayland shell/input/clipboard/epoll logic: xkb keyboard, client-side key repeat, the
  session shortcuts, pointer (bar clicks, pane focus, selection, mouse reporting, wheel), `layout_apply()`,
  frame-paced redraw, one epoll loop over every session's pty fd, SIGCHLD reaping by pid, CLI flags,
  data-device clipboard. Look here for anything involving pointer, keyboard, resize or frame pacing.
- `src/render.c` — EGL (platform Wayland) + GL 2.x: two atlases, one shader, one draw call (instanced or
  expanded), three back-to-front passes; draws exactly what `src/layout.c` says and owns no geometry.
- `src/font.c` — fontconfig pattern -> FreeType face, alpha8 glyph atlas (shelf packer + hash by
  codepoint), fontconfig fallback faces, HarfBuzz-shaped color emoji (`font_color_glyph`) + RGBA8 color
  atlas, `font_resize()` re-rasterizing at a new pixel size.
- `src/pty.c` — `posix_openpt`/`grantpt`/`unlockpt`; child does `setsid` + `TIOCSCTTY` + `chdir` + exec
  `$SHELL` (or `-e COMMAND`); sets `TERM`/`COLORTERM`/`TERM_PROGRAM`; `pty_setsize`.
- `src/config.c` — `key = value` config file, defaults, clamping, and the key-binding spec parser
  (`keybind_parse`/`keymods_parse`).
- `src/mux.c` — the sessions of one window: the dense `Tab` array (`{Term, Pty, attention}`), the active
  index, open/close/goto/next/prev, `max_tabs`, the unread-output flag, `mux_title()`. No Wayland, no EGL,
  no fd, no fork — that is what makes it unit-testable.
- `src/layout.c` — pure geometry: window px + cell px + padding + gap + mode + the session list in; the bar
  strip, `BarItem[]` rects + labels and `PaneGeom[]` rects (each with its cols/rows) out, plus the three hit
  tests, `layout_grid_size()` and `layout_initial_size()`.
- `tests/test_vt.c` — 501 checks: parser, grid, selection, alt screen, `live_off`, emoji clusters, CSI 3 J,
  bracketed paste, the `term_init` guard, reflow, `term_grid_geometry`, the zoom/alt-screen row-shrink rule,
  `mouse_sgr_button`, mux, keybind/keymods parsing, the multiplexing config keys, and `layout_compute` /
  `layout_grid_size` / `layout_initial_size`. The source is the list of what is covered; the two invariants
  with dedicated checkers are the `row[]` permutation (also `tests/dbg.c`) and the startup-size round trip.
- `Makefile` — pkg-config-driven; `-Iinclude/kai-term -Ibuild/generated`; output dirs created by order-only
  prerequisites so they never force rebuilds; generates the xdg-shell bindings with `wayland-scanner`.

**Facts that are not in the code:**

- **Dependencies:** libwayland-client, wayland-cursor, wayland-egl, libxkbcommon, freetype2, fontconfig,
  harfbuzz (required since session 37 — README must keep listing it), EGL/GL only. Deliberately no
  cairo/pango/Qt/GUI framework. xdg-shell bindings are generated at build time, not vendored.
- **Naming:** project/binary/identifiers use `kai-term` (`kai_term` in C), display name `Kai-Term`. Renamed
  from `K-Term`/`k-term` on 2026-10-16 (session 59) because of a name conflict with another of the user's
  projects — do not "restore" the short form anywhere: directory, binary, header, `DEF_APP_ID`/`DEF_TITLE`,
  log prefix, `TERM_PROGRAM` and the config path all move together, and tests/test_vt.c pins the two `DEF_*`
  literals so a half-rename fails a check.
- **Layout:** sources in `src/`, public header in `include/kai-term/`, tests in `tests/`, binaries in `bin/`,
  objects + generated bindings in `build/`. `make clean` deletes `bin/` and `build/` entirely. There is no
  `docs/` any more: the architecture doc was removed on 2026-10-16 (session 63) because it carried
  host-system facts; its invariants are in "Architecture & Invariants" below and its tooling parts are in
  `/home/kaida/Project/ENVIRONMENT.md`.
- **License:** MIT (`LICENSE`, "Copyright (c) 2026 Kaida"), the user's choice. It must ship with any
  redistribution. No source-file SPDX headers exist; if added they must read
  `SPDX-License-Identifier: MIT`.
- **Git:** the project is on GitHub (`origin = https://github.com/K-code-AI/Kai-Term`, branch `main`) and the
  agent has push access (the user said so on 2026-10-15). Still run `git log --oneline` / `git status` before
  assuming anything is committed, and never commit `bin/` or `build/`.
- **Known limitations a newcomer must not "fix" by accident:** narrowing can evict the OLDEST scrollback
  rows; the alternate screen is deliberately NOT re-wrapped; a taller window keeps less scrollback than
  configured (`rcap` is fixed at startup); the window title is Kai-Term's own text, not the application's OSC
  title. Full list in ISSUES.md "Known limitations".

## Architecture & Invariants

The rules a change must not break. (Full history of why each exists: ISSUES.md "Fixed".)

**Data flow.** PTY bytes -> `term_parse()` (vt.c) -> one `Grid` per session -> `Mux.tab[]` owns one
`{Term, Pty}` per session, one ACTIVE -> `layout_compute()` turns window px + cell px + padding + mode +
the session list into bar strip, item rects and pane rects -> `render_draw(&rend, &font, &g_layout, terms)`
draws the bar, one grid per pane and the focus border -> EGL -> `wl_surface`. Input goes
`kb_key`/`ptr_*` -> pty_write to the ACTIVE session, after `ptr_*` has hit-tested the bar and the panes.

**Event loop (main.c).**

- One epoll over: the `wl_display` fd, one pty master fd per session (mapped back with `tab_for_fd()`; pty
  EOF closes that session, it does not end the program), an 8 ms timerfd (`FRAME_INTERVAL_NS`),
  `signalfd(SIGCHLD)` (SIGCHLD blocked so only signalfd reads it; reap with `waitpid(-1,…,WNOHANG)` and match
  pids with `tab_for_child()`, then zero `pty.child`), the paste idle timerfd, and the key-repeat timerfd;
  the clipboard paste pipe read end joins transiently.
- **The wayland read is finished before ANY other descriptor is handled** (`read_events`/`cancel_read`, then
  `dispatch_pending`): `eglSwapBuffers` waits for a `wl_buffer.release`, and while a
  `wl_display_prepare_read()` is outstanding that release can never be read — the swap blocks forever and the
  terminal looks dead. Calling `read_events()` inside a drain loop instead blocks on the display fd and
  starves the timerfd/signalfd.

**Frame pacing — three rules, each the result of a real freeze.**

1. A frame callback is armed only together with a commit that attaches a NEW buffer (from `redraw()` after a
   successful swap). A damage-driven compositor runs a render pass only when something changed, so a callback
   armed on an EMPTY commit can stay pending forever, and every draw path is gated on "no callback in
   flight" -> frozen display. `solicit_frame()` was deleted for exactly this — do not re-add it. A failed
   swap keeps `W.dirty` for the timer to retry and arms nothing.
2. `eglSwapBuffers` is not guaranteed to return: it waits for the compositor to release a buffer. The
   constraint is on the wayland READ, not on listeners — swapping inside a listener is fine once the read is
   complete. Keep at most one buffer in flight (`W.frame_cb`).
3. The FIRST buffer must be attached by the FIRST `xdg_surface_configure` handler (`!W.swapped_once`): a
   window without a buffer never maps, so it never gets a frame callback either. A configure that changes the
   pixel size sets `W.resized`; both the timerfd branch and `frame_done()` draw when `dirty || resized` —
   waiting for `dirty` alone is what left the screen frozen in fullscreen.

**Wayland/input.**

- Pointer coordinates are fixed point: `wl_fixed_t` is `int32_t` encoding px*256, so `wl_fixed_to_int` must be
  applied before storing (storing the raw int compiles warning-free and is 256x too large).
- Key repeat is the client's job — `wl_keyboard.key` delivers one press and one release per physical
  keypress and `repeat_info` only reports the system setting. `send_key()` returns whether the press is
  repeatable, `krfd` arms for `repeat_delay` then re-arms at `1/repeat_rate`, and release / focus loss /
  keymap change call `repeat_stop()`, which DRAINS an expiry that already fired. `it_interval.tv_nsec` must
  stay < 1e9. The key is re-TRANSLATED on every repeat, so Shift while holding `a` starts producing `A`.
- Seat bound at v5 for `axis_discrete`/`frame`; every listener slot the bound version can dispatch must be
  non-NULL or libwayland aborts.
- The clipboard is push-based (no `request_selection`): copy = `set_selection`, paste = receive the offered
  `text/plain` into a pipe whose read end joins epoll. A paste is ONE bracketed block: the text is
  accumulated and written only at EOF (or the 400 ms watchdog), in <=4096-byte pieces, wrapped in
  `ESC[200~` … `ESC[201~` when DEC mode 2004 is on; over 8 MiB it is sent unwrapped with a warning.

**Multiplexing/layout.**

- `Mux` owns a dense `Tab tab[TAB_MAX]` plus `n`/`active`; `cur_term()`/`cur_pty()` resolve through
  `mux_cur()` so there is no second copy of "which session is visible".
- `term_init()` memsets the whole `Term` and `term_destroy()` frees both grids, so a session is initialized
  and destroyed exactly once, and `mux_close()` zeroes the vacated slot. Because `mux_close()` shifts later
  tabs down, it must RE-ARM `tab[i].term.ps.t` for every moved tab and must capture the `Pty` BEFORE the
  shift.
- Output on a session with no pane sets `Tab.attention` and does NOT mark the frame dirty; output on a
  drawn-but-inactive session marks it dirty and sets no flag.
- `layout_apply()` is the only caller of `layout_compute()`; it resizes EVERY session to `layout_grid_size()`
  (so a tab switch costs no resize or reflow) and sets `W.gx/W.gy` to the active session's pane origin, which
  is what `sel_cell()`/`mouse_cell()` map through. It runs from every geometry change AND from `redraw()`,
  because bar labels are part of the picture.
- `ptr_button()` checks `layout_hit_bar()` BEFORE the mouse-reporting branch, so an app that grabbed the
  mouse still cannot eat the bar; `cur_term()` is re-checked after a middle click (closing the last session
  makes it NULL inside a listener).
- Chrome colours come from the CONFIGURED palette (`Render.fg`/`Render.bg`), never from a `Term`'s pen — that
  is the application's current SGR state and would let `printf '\e[31m'` repaint the bar that names its tab.

**Grid model (vt.c).**

- `Grid.row` has fixed capacity `rcap = rows + scrollback_max` and is always a PERMUTATION of the allocated
  buffers: scrolling is rotation (`rot_left`/`rot_right`), never `memmove` of pointers, so no slot is NULLed,
  duplicated or double-freed — `tests/dbg.c` asserts it. `grid_vprow(g,y) == row[scroll_off + y]`.
- Anchor invariant: `0 <= scroll_off <= live_off <= sb_limit = min(scrollback_max, rcap - rows)` and
  `live_off + rows <= rcap`. `live_off` is the `scroll_off` showing the newest output; user navigation moves
  `scroll_off` below it; `snap_to_live()` restores it before parsing so new output lands at the live bottom.
- `cy` is a LIVE-view coordinate (the parser addresses `row[live_off + cy]`), so `term_resize` re-anchors `cy`
  with `live_off`. The cursor is drawn only when `scroll_off == live_off`. Whole-viewport scrolls snap
  `scroll_off` back to `live_off` first.
- `Grid.valid == GRID_VALID` between init and destroy — a plain 0/1 flag is not enough because uninitialized
  stack memory is routinely non-zero (stack garbage measured `32511`).
- Alternate screen: `Term.g` stays the ACTIVE grid and the main grid is parked in `Term.alt` (alt has
  scrollback 0); `?1049` saves/restores the cursor, `?47`/`?1047` switch without saving, `?1048` is
  cursor-only; `term_resize` resizes the parked grid too.
- CSI 3 J: `erase_scrollback()` moves the viewport rows to slots `0..rows-1` through a validated scratch
  array, blanks the rest and sets `scroll_off = live_off = 0`; gated by a process-wide setting, deliberately
  NOT a `Term` field because RIS memsets the whole `Term`.
- Reflow: `Grid.wrap[i] != 0` means the line in `row[i]` was broken by the RIGHT MARGIN and continues in
  `row[i+1]`; it is a byte array parallel to `row[]` that rotates with it and is cleared by every operation
  that blanks or erases a slot. `grid_reflow()` measures each row with `row_used()`/`cell_pristine()`, not the
  full width (never-written cells and wide-glyph padding are dropped, never turned into spaces, and the
  tracked cursor cell is never trimmed), joins and re-breaks at the new width pulling a boundary back only
  when the cell that would START the next chunk carries `ATTR_WIDE` — peek the NEXT source cell, because
  reacting to a wide pair that legitimately ENDED a chunk loses a character — builds output oldest-first in a
  fixed `2*rcap+2` array evicting the oldest rows once `rcap` exist, and commits by installing the new
  buffers (permutation kept). The eviction step must run for EVERY produced row, blank rows included, or
  `kept` can exceed `rcap` and the commit drops buffers instead of installing them (a leak). Those three
  traps each cost a real bug. `grid_resize_plain()` is the no-reflow variant for the ALTERNATE screen (a
  full-screen app repaints itself; re-wrapping would scramble it) — the parked main grid is still reflowed.

**Geometry: one helper decides a grid's size AND its origin.**

- `term_grid_geometry()` (vt.c) is called once per pane from `layout_compute()` with `pad = 0` (the window's
  `Config.padding` margin and the pane gap are already in the pane rect). THREE users must agree:
  `layout_apply()` (grid + PTY size), `sel_cell()`/`mouse_cell()` (pointer -> cell), and `render_draw()`
  (where the quads go).
- Padding is applied on all four sides and capped at `(window - one cell)/2` per side;
  `cols = (win_w - 2*xoff)/cell_w`; the whole-cell remainder stays at the RIGHT and BOTTOM, never the
  top/left, so `row = (ptr_y - gy)/cell_h`, `col = (ptr_x - gx)/cell_w` is exact. Anchor anywhere else and
  every click, drag and SGR report lands on the wrong row.
- The margin is painted by `glClear`, so the clear colour must be the CONFIGURED background (`Render.bg`
  premultiplied by `Config.opacity`), not a hardcoded default, and `render_draw` clears every frame.

**Rendering.**

- Two textures (alpha8 mono, RGBA8 color emoji), each uploaded only when dirty, AFTER the emit passes and
  BEFORE the draw — the glyph lookups run inside pass 3, so uploading at the top of `render_draw` shows every
  new glyph one frame late (and a first emoji as a black box).
- One shader; `a_glyph` selects the path: 0 background quad, 1 mono glyph (tinted by `v_fg`), 2 color emoji
  (premultiplied, not tinted).
- Pass order back to front: all cell backgrounds, then the selection bands, then the glyph quads — a WIDE
  glyph quad spans into the next cell and the neighbour's background must not be emitted after it.
  Continuation cells (`ATTR_WIDE`) are skipped in pass 3.
- Instanced path when both `GL_ARB_draw_instanced` and `GL_ARB_instanced_arrays` are advertised; otherwise
  the same 19-float records are expanded to 21 floats per vertex and drawn with `glDrawArrays`.
- No GL blending is ever enabled: the fragment shader composites `premul + bg*(1-a)`, and a Wayland EGL ARGB
  buffer is PREMULTIPLIED, so writing raw colour with partial alpha reads washed out; `max(a, bg.a)` is wrong
  too (it painted the glyph's bounding box opaque). `Config.opacity` must be clamped with `fclamp_`, never
  `iclamp_` (an int clamp turns 0.5 into 0).
- Color emoji: `putch` folds ZWJ, variation selectors, skin tones, keycap, tag chars and the second regional
  indicator into the ANCHOR cell's `extra[]` (`CLUSTER_MAX` 8) so a `Cell` stays copyable by value;
  `font_color_glyph()` hands the whole sequence to `hb_shape`, which collapses it to one glyph id (anything
  else falls back to mono). FreeType loads it with `FT_LOAD_COLOR | FT_LOAD_NO_SCALE`; the renderer scales by
  `cell_h / color_px`, so zoom never re-rasterizes an emoji. A wide cluster reserves TWO grid cells
  (`cluster_is_wide()` is the single source of truth, also true when U+FE0F/U+20E3 switch the base to emoji
  presentation), and `render_draw` sizes the quad with the same predicate. Cluster state is invalidated on
  every row rotation, cursor jump, BS/HT/CR, newline and resize. Misses are cached; the color atlas starts at
  1024x512 and doubles up to 8192, and a glyph that cannot fit falls back to mono rather than drawing with
  stale texcoords.

**Selection.**

- A drag only ever produces CELL edges: `sel_cell()` maps the pointer to a cell and `sel_set_endpoints()`
  gives the earlier cell its LEFT edge and the later one its RIGHT edge; the direction is recomputed on every
  motion event so dragging left past the anchor SWAPS the pair.
- Pass 2 skips a cell that was never written (`ch == 0`) so a drag past the end of a line stops at the last
  glyph, while real spaces and `ATTR_WIDE` continuation cells keep their band; pass 1's cursor block uses the
  SAME predicate.
- Band edges stay ABSOLUTE buffer pixels, which is correct across panes because panes never overlap in x.

**Config.**

- `key = value`, read once at startup from `-c FILE`, else `$XDG_CONFIG_HOME/kai-term/kai-term.conf`, else
  `~/.config/kai-term/kai-term.conf`; CLI flags are applied AFTER the file so they win and never write back.
- `#` starts a comment only at line start or after a space/tab, so a value may contain `#`. Unknown keys log
  and are ignored; a missing file is not an error. Colours accept `#RRGGBB`, `0xRRGGBB`, `AARRGGBB`. Ranges
  are clamped at parse time.
- Defaults live in `config_default()` AND in the `DEF_*` macros — change both or the README drifts.

## Debugging this project

- `./bin/kai-term -d` (log level 3) prints: the font chosen and `color emoji: <path> (<n>px strike)`, GL
  version and instancing state, `shell starts in <dir>`, `grid resized to CxR (window WxH px, cell wxh,
  padding P, unused ux x uy px, tab i/n, tab view|side-by-side view, N panes)`, one
  `layout: tab view|side-by-side view, N pane(s), tab bar on|off, grid CxR per session` line per LAYOUT
  SHAPE change (the client-side evidence for any bar/pane report, and what `tests/cli_test.sh` greps for),
  the session lifecycle (`mux: session 1/N started`, `tab i/n opened (pid P, pty fd F, grid CxR)`,
  `tab i/n visible`, `tab i closed, N left`, `the pty of tab i/n closed`, `the shell of tab i/n exited`,
  `the last session is gone, exiting`), the bar clicks (`bar: click selects session i/n`,
  `bar: middle click closes session i/n`), `reflow: OLD -> NEW rows at N cols (K evicted)`,
  `compositor repeat_info: rate R/s delay D ms (kai-term uses …)`, `toplevel state: fullscreen F maximized M
  activated A (configure WxH px)` (the only client-side evidence of entering fullscreen),
  `configure answered: buffer WxH attached (grid CxR)`, `zoom: font Npx, grid CxR`,
  `selection copied: N bytes` / `selection finished: ax,ay -> bx,by (copy_on_release off — Ctrl+Shift+C to
  copy)`, and while a drag is live `sel press/drag cell=(col,row) frac=… ptr=(x,y) …` — that line is how you
  check a selection-vs-pointer complaint without a screenshot, and it gives the window's absolute position
  as `click_target - ptr`. A `swap failed (N in a row), retrying on the next frame` warning means
  `eglSwapBuffers` returned failure; a few are self-healing, a long streak means the compositor is not
  showing this window.
- `./bin/dbg` — scripted scroll/resize/reflow/zoom stages; each stage prints `scroll_off/live_off/rows/rcap/
  used` plus one two-character token per slot (`v` viewport / `s` scrollback, then the row's first cell), and
  asserts `row[]` is a NULL-free, duplicate-free permutation.
- `tests/cli_test.sh` drives the real binary — `main()` is in no linked test, so this is the only honest way
  to cover argv and the config keys.
- Headless harness: link `src/vt.c` + `src/config.c` (+ `mux.c`, `layout.c`) into a throwaway program in
  `/home/kaida/Project/.scratch/` to exercise logic Wayland input events cannot reach.

## Key Decisions & Pitfalls

- **`eglSwapBuffers` blocks, and a blocked swap looks like a dead terminal.** It waits until the driver gets
  a buffer back and only the compositor releases buffers, so a client whose surface is not being rendered
  stalls inside the swap: no keyboard, no PTY reads, no redraw (measured: `wchan = futex_do_wait`;
  `eglSwapInterval(0)` does not change it on this NVIDIA driver). The rule is about the wayland READ — the
  earlier "never swap inside a listener" wording was BACKWARDS and caused a regression. Answering EVERY
  configure with a swap is also wrong (it spammed `eglSwapBuffers failed: 0x3003` every frame).
- **Geometry lives in `src/layout.c`, not in main.c.** main.c is in no test binary, so arithmetic left there
  is untestable, and N panes need N origins, which one global offset cannot express. The round trip
  `layout_initial_size()` -> `layout_compute()` -> `layout_grid_size()` is what proves the startup height is
  right — that check caught the `cell_w`/`cell_h` typo (session 62). Same reasoning put
  `mouse_sgr_button()` in vt.c.
- **The evdev button order is not the protocol order.** `BTN_LEFT` 0x110, `BTN_RIGHT` 0x111,
  `BTN_MIDDLE` 0x112, but SGR numbers them 0 left, 1 middle, 2 right. A switch written in kernel order
  silently swaps RIGHT and MIDDLE — in the bar's middle-click-close AND in every SGR report to vim/htop.
- **A log line that prints the value it was GIVEN can lie.** `tab_switch(idx)` logged
  `mux: tab idx/n visible` while nothing had changed, so the log looked like proof the click worked and the
  bug survived every reading of it. Log what the owner of the state REPORTS (`mux_active()`).
- **`Term` is not a place for settings.** `ESC c`/RIS calls `term_init`, which memsets the whole `Term`, so
  process-wide settings are file-scope values with a setter (`kai_term_loglevel`, `g_clear_scrollback`)
  applied from the config in main.c. `bracketed_paste` is deliberately the opposite: it is
  application-negotiated, so a reset SHOULD clear it.
- **A boolean "is it initialized" flag cannot survive uninitialized memory** — see the `Grid.valid` /
  `GRID_VALID` invariant under "Grid model": the sentinel exists because stack garbage measured `32511`.
- **A Kai-Term-to-Kai-Term clipboard test proves nothing** — it works even if the client never shares its
  selection. Test against a foreign client (`wl-copy` in, `wl-paste` out). With `copy_on_release = 0` (the
  default) a mouse drag does NOT call `set_selection`; only `Ctrl`+`Shift`+`C` does — that is what reads like
  "Kai-Term keeps a private clipboard".
- **A sanitizer target with an incomplete prerequisite list produces confident, wrong results.**
  `bin/dbg_asan` once kept running a stale binary and reported a 312-byte leak the current source did not
  have. When a sanitizer finding contradicts the tests, check the binary's mtime against the sources first.
- **A `(void)x;` line does not mean `x` is unused.** `(void)def1;` in `csi_dispatch` read like a leftover,
  but `def1` is used by `CSI J`, `CSI K` and `CSI 6 n`. Grep for uses, then let the compiler confirm.
