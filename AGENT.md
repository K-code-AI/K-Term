# K-Term — Agent Journal

Read this (plus ISSUES.md) before touching the project. Bug root causes live in ISSUES.md,
module design in docs/ARCHITECTURE.md, user-facing behaviour in README.md. This file keeps only
the current verified state, the environment facts, the pitfalls a newcomer must know, and a
short history.

## Project Overview
K-Term is a lightweight Linux terminal emulator for Wayland written in C: native Wayland window
(xdg-shell) + PTY shell + VT/ANSI parser + EGL/OpenGL text rendering from two glyph atlases (alpha8
mono + RGBA8 color emoji) in one draw call per frame. No GUI framework.

## Current Status (build/test/asan/live re-verified 2026-10-15, session 50)
- Clean tree: `make` -> zero warnings under the Makefile's `WARN` set (`-Wall -Wextra -Wshadow
  -Wwrite-strings -Wformat=2 -Wdiscarded-qualifiers`, tightened in session 50; the tree is also clean
  under `-Wcast-qual -Wmissing-prototypes -Wstrict-prototypes -Wold-style-definition -Wredundant-decls`
  except inside the generated xdg-shell bindings, which is why `-Wcast-qual` is not in `WARN`);
  `make test` -> `323 checks, 0 failures` +
  `MODEL OK (0 problems)`; `make asan` -> same, with NO LeakSanitizer/UBSan report. No temporary probe or
  experiment remains in the tree (`grep -rn "TEMPORARY|PROBE|EXPERIMENT|KTERM_S47" src/ include/ tests/`
  -> nothing). Re-run from a clean tree by session 50 (`make clean && make` 0 warnings, `make test`,
  `make asan`, a live `-d` run — `/tmp/s50_b3.log`, `/tmp/s50_t3.log`, `/tmp/s50_a3.log`,
  `/tmp/s50_live.log`); README's flags table and
  its 17 settings rows match `src/main.c` and `src/config.c`.
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
  build of k-term itself.
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
NONE OPEN — session 50 finished the cleanup pass the user asked for (see the session-50 Progress entry for
what changed, the verification logs, and the commit/push). `NextFeature.md` no longer exists (both of its
items were done and confirmed by the user), so there is no work order on disk.

NEXT ACTION: ask the user what to build next; do not start a feature without their go-ahead. Two things
worth offering (both options, not open bugs):
  * a `k-term` ASan target (only the tests have one; the hand-built one found the `row[rcap]` overrun);
  * the `wl_data_offer_accept()` + multi-mime clipboard hardening, which niri does not need but a stricter
    compositor might (ISSUES.md "Known limitations").

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

## Environment (verified 2026-10-12; re-confirmed live 2026-10-14 session 38; compositor state re-checked
2026-10-15 sessions 47, 48 and 49)
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
- **Screenshots/recording:** `wf-recorder` 0.6.0 and `ffmpeg`/`ffprobe` 8.1.2 are installed (since
  session 45) — record the screen and measure the picture (`ffmpeg -i out.mp4 -vf fps=10 f%04d.png`,
  then diff frames with PIL/numpy). niri also has built-in screenshot actions. `grim`/`slurp`/
  `wf-recorder`-older are NOT installed; a temporary `glReadPixels` ASCII-art readback still works for
  per-cell pixel questions (see docs/ARCHITECTURE.md).
- **Keyboard/pointer injection DOES work here (session 45 — this CORRECTS the old "Headless limit").**
  `/dev/uinput` is writable; a virtual keyboard shows up as `/dev/input/eventNN`, udev classifies it
  (`ID_INPUT_KEYBOARD=1`) and **niri/libinput opens it** (check `ls -l /proc/$(pgrep -x niri)/fd`).
  `.scratch/inject.c` (binary `.scratch/inject`) types a US-layout string and presses chords
  (`@super+shift+m`, `@ctrl+c`, `@ret`); verified end to end — k-term running `-e 'cat > FILE'` received
  exactly `hello^Mworld^M`. Session 44's "zero bytes, injection does not work" conclusion was WRONG: the
  injector's key table was the bug (evdev letter codes are NOT alphabetical — `KEY_A + (c-'a')` typed
  `kg##z` for `hello`). Ground truth for "did the key arrive" = the bytes the PTY got, not the screen
  (the screen is exactly what may be frozen).
  Pointer events can be injected the same way (`UI_SET_RELBIT`/wheel) — `.scratch/pinject.c` (binary
  `.scratch/pinject`, built 2026-10-15) does `--home`, `--to X Y`, `--click X Y`, `--drag X1 Y1 X2 Y2`.
  CAVEAT measured in session 49: RELATIVE motion goes through libinput's pointer ACCELERATION, so an
  intended 120 px move can land ~480 px away, and a small intended move can land outside the window
  entirely (zero `sel press` lines). Do not try to aim a selection drag by screen coordinates; fill the
  whole viewport with text and read K-Term's own `sel press/drag cell=` log line to learn where it landed,
  or drive the keyboard path instead.
- **Clipboard testing needs a foreign client.** `wl-copy` / `wl-paste` are installed (`/usr/bin/`);
  `wl-paste --list-types` shows exactly what the compositor advertises for the current owner. A K-Term-to-
  K-Term paste test proves nothing about clipboard sharing — that is how a "private clipboard" complaint
  could survive every test ever run here. Working recipe (session 49): `wl-copy 'TEXT' </dev/null` (it
  otherwise keeps running and holds the pipe), launch k-term with `-e 'stty raw -echo; cat > FILE'`, focus
  the new window, inject `@ctrl+shift+v`, and read FILE — the bytes the PTY got are the ground truth.
- **The compositor is cooperative again (session 47, re-confirmed by session 48) — live GUI measurement
  works again.** The session-46
  warning below is OBSOLETE: `niri msg focused-window` reports a focused window, `niri msg action
  set-window-width/-height --id …` really resizes (a k-term window followed a 5-step sweep,
  `/tmp/s47_stress2.log`), and `wf-recorder` captures the test window (session 48: a 4 fps frame diff of a
  live k-term shows the picture changing every frame, `.scratch/s48_frames/`). Re-verify cooperativeness
  before trusting any live number (it was broken for a while), but do not assume it is still broken.
  Session-46 observation kept for context: when the compositor does NOT render a surface, that surface
  gets its map frame and nothing after it (no frame callbacks, no buffer releases) and ANY EGL client
  blocks in its second `eglSwapBuffers` — that is an environment artifact, not a K-Term bug.
- **How to measure the draw path here (session 47, all proven working):**
  * `cat /proc/<pid>/wchan` — `ep_poll` = the event loop is alive; `futex_do_wait` = blocked inside the
    EGL driver (a swap that will never return). This is what separated "frozen display" from "dead loop".
  * `wf-recorder -f out.mp4` (no `-t`/`-q` options in 0.6.0, and `-c ffmpeg` fails with "Failed to find the
    given codec" — use the plain form; stop it with SIGINT; `slurp` is not installed, so `-g` cannot be fed
    an interactive region), then
    `ffmpeg -i out.mp4 -vf fps=4 f%03d.png` and count bright pixels / diff frames with numpy: a live
    terminal shows monotonically growing text pixels, a frozen one shows zero change.
  * An ASan build of the k-term BINARY itself (not just the tests): `cc -O1 -g
    -fsanitize=address,undefined -fno-omit-frame-pointer $(CFLAGS) src/*.c
    build/generated/xdg-shell-protocol.c $(LDLIBS)`; run with `ASAN_OPTIONS=detect_leaks=0` to skip the
    third-party leak noise (ISSUES.md Open #1). That is how the `row[rcap]` overrun was found.
  * Identify a test window as "the window id that appeared after launch" and a test process by a unique
    marker inside its `-e` command (`pgrep -f MARKER`) — `pgrep -x k-term` also matches leftovers from
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
  (appearance settings are config-only); `copy_on_release` became a config toggle (default OFF);
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

## Next Steps
1. **Nothing is pending verification** — the user confirmed the black-window fix, the heavy-output segfault
   fix and the old `NextFeature.md` #2 (fullscreen) in their real window, and confirmed clipboard interop
   both ways. `NextFeature.md` itself is DELETED (session 50: both of its items are done and confirmed),
   so there is no work order on disk — ask the user what to build next.
2. Housekeeping options: make the ASan build of the `k-term` binary a real Makefile target (only the
   tests have one; the hand-built one in `.scratch/kterm_asan` found the `row[rcap]` overrun, so a
   `make asan` that also covers the Wayland/EGL path would pay off — it would also cover the heap-grown
   `paste_buf`); idle-frame
   optimization (stop the frame cycle when nothing is dirty); the color atlas is re-uploaded whole with
   `glTexImage2D` whenever a new emoji is shaped — `glTexSubImage2D` per new shelf would be cheaper; a
   reflow currently allocates a fresh row buffer per produced line (fine at terminal sizes, but a
   huge-width window with a huge scrollback churns the allocator).
3. Clipboard hardening that niri does NOT require (do not do it unasked — it is untested against a
   compositor that needs it): call `wl_data_offer_accept()` before `wl_data_offer_receive()`, and offer /
   prefer `UTF8_STRING` + `text/plain;charset=utf-8` next to `text/plain`. Current behaviour is verified
   working on niri in both directions.
4. Scratch cleanup: the throwaway probes live OUTSIDE the project tree in `/home/kaida/Project/.scratch/`
   (~125 MB, 160+ entries) — `egl_seq_probe.*`, `egl_probe.*`, `egl_resize_probe.*`, `inject.*`, `keyinj.*`,
   `pinject.*` (pointer injector, session 49), `mk_repeat_probe.sh` + `repeat_probe.c`, `reflow_probe.c`,
   `reflow_live*.sh`, `bisect/`, `paste_probe.c`, `iso*.c`, `kterm_asan`, `dbg_oldvt`, `s45_old/` (the last
   user-confirmed-good tree), `s47_before/`, `fs2/`, `fs3/`, `fs_frames/`, `s46_frames/`, `s48_frames/` +
   `s48_win.mp4`, the `s49_*.sh` clipboard harnesses, the `AGENT.md.before_prune` /
   `AGENT.md.before_restructure` / `AGENT.md.session48_preprune` journal
   copies, and the `*.log`/`*.sh` evidence the journal cites. Journal entries reference them as evidence;
   delete them once the user has confirmed the behaviour they cover (everything they covered is now
   confirmed, so they can go — keep `s45_old/` and `s47_before/` if another draw-path regression is ever
   suspected, they are the A/B trees).
5. Emoji paths that exist but have NEVER run here (verify before trusting them): the SCALABLE color
   font branch (`FT_IS_SCALABLE` -> `FT_Set_Pixel_Sizes` + `hb_ft_font_changed` on zoom) — this box only
   has the bitmap-only Noto CBDT font; and a cluster that HarfBuzz renders as more than one glyph is
   deliberately rejected (`ng == 1` check) and falls back to mono, which is the right behaviour but is
   untested against a font that does it.
6. Git: the tree is committed and pushed (session 50). Nothing is pending; keep committing verified
   work and keep `bin/`/`build/` out of it.

## Project Files
```
K-Term/
├── .gitignore                 # bin/, build/, *.o, *.obj, *.d, *.so, *.a, editor leftovers
├── LICENSE                    # MIT License, "Copyright (c) 2026 Kaida"
├── AGENT.md                   # this journal
├── ISSUES.md                  # open bugs + known limitations + fixed history (root causes)
├── README.md                  # features, deps, build, flags, config, shortcuts
├── Makefile                   # src/*.c -> build/*.o -> bin/k-term; test/asan/run/clean; WARN set
├── docs/ARCHITECTURE.md       # module map, event loop, grid invariants, reflow, color emoji, debug entry points
├── include/k-term/k-term.h    # the single shared header (all types + prototypes)
├── src/                       # config.c vt.c pty.c font.c render.c main.c
├── tests/test_vt.c            # 323 checks: parser, grid, selection (incl. sub-cell), alt screen,
│                              #   live_off, emoji clusters (ZWJ / skin tone / VS16 / keycap / flag pair),
│                              #   CSI 3 J, bracketed paste, term_init guard, REFLOW (round trip, cursor,
│                              #   wide pairs, eviction, hard breaks, coloured blanks, alt screen,
│                              #   a viewport grown past half of rcap (the row[] overrun)
├── tests/dbg.c                # grid-model invariant checker (ed3*, resize, reflow and grow-then-scroll
│                              #   permutation stages)
├── bin/                       # k-term, test_vt, dbg (+ *_asan)   [generated]
└── build/                     # *.o + generated/xdg-shell-*        [generated]
```
- `src/vt.c` — the unit-tested core: parser + grid + selection + alt screen. No Wayland/GL deps.
- `src/main.c` — all Wayland input/shell/clipboard/epoll logic; look here for anything involving
  pointer, keyboard, resize, or frame pacing.
- `src/render.c` — EGL + GL 2.x, two atlases (alpha8 mono + RGBA8 color emoji), one shader, one draw
  call (instanced or expanded), three back-to-front passes.
- `src/font.c` — fontconfig + FreeType alpha8 atlas, shelf packer, hash cache, fontconfig fallback
  faces, HarfBuzz-shaped color emoji (`font_color_glyph`) + RGBA8 color atlas, `font_resize`.
- `src/pty.c` — PTY spawn, child session setup, resize, TERM/COLORTERM/TERM_PROGRAM env.
- `src/config.c` — `key = value` config parser + defaults + clamping.
- `Makefile` — pkg-config-driven; `-Iinclude/k-term -Ibuild/generated`; output dirs created by
  order-only prerequisites so they never force rebuilds; targets `all test asan run clean`.

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
- **Naming:** project/binary/identifiers use `k-term` (`k_term` for C identifiers).
- **Layout:** sources in `src/`, public header in `include/k-term/`, tests in `tests/`, binaries in
  `bin/`, objects + generated bindings in `build/`, extra docs in `docs/`. `make clean` deletes
  `bin/` and `build/` entirely.
- **License:** MIT (`LICENSE`, "Copyright (c) 2026 Kaida"), the user's choice. MIT requires the
  copyright + permission notice in all copies, so `LICENSE` must ship with any redistribution. No
  source-file SPDX headers exist; if added they must read `SPDX-License-Identifier: MIT`.
- **Git:** the project IS on GitHub now (the user set this up after session 49). Remote `origin =
   https://github.com/K-code-AI/K-Term`, branch `main`, and the agent has push access (the user said so
   explicitly on 2026-10-15). Before session 50 there were two commits, both named "Initial commit"
   (`739013c` = .gitignore + LICENSE, `18b9732` = the 15 source/doc files). Session 50 added the cleanup
   commit. The old "zero commits, nothing is pushed without the user" rule is OBSOLETE — but still run
   `git log --oneline` / `git status` before assuming anything is committed, and never commit `bin/` or
   `build/` (the session-50 `.gitignore` covers them; the previous one, a kernel-module template, did
   NOT, which is why a build used to show up in `git status`).
- **Pitfall — a `(void)x;` line does not mean `x` is unused.** `csi_dispatch` carried `(void)def1;`,
  which reads like "this local is a leftover"; deleting `def1` broke the build because it is really used
  by `CSI J` (erase display), `CSI K` (erase line) and `CSI 6 n` (CPR). Session 50 removed only the cast
  and documented the variable. Always grep for uses before deleting anything "dead", and let the compiler
  confirm it (`make` after every removal).
- **Pitfall — `Term` is not a place for settings.** `ESC c` / RIS calls `term_init`, which `memset`s the
  whole `Term` (vt.c), so anything stored there is wiped by a reset. Process-wide settings live as
  file-scope values with a setter (`k_term_loglevel`, `g_clear_scrollback`) and are applied from the
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
- **Pitfall — a K-Term-to-K-Term clipboard test proves nothing.** Every paste test run before session 49
  moved text between two K-Term instances, which works even if the client never shares its selection with
  the rest of the desktop. The only meaningful test uses a FOREIGN client: `wl-copy` in, `wl-paste` out
  (`wl-paste --list-types` also shows what the compositor advertises for the current owner). Related: a
  Wayland clipboard is not a buffer the client can read on demand — the compositor pushes an offer
  (`wl_data_device.selection`) and the source serves the bytes on request, so "paste" only works if the
  earlier "copy" actually called `set_selection`. With `copy_on_release = 0` (the default since session 43)
  a mouse drag does NOT call it; only `Ctrl`+`Shift`+`C` does. That is the behaviour that reads like
  "K-Term keeps a private clipboard".
- **Pitfall — identifying a test window on the user's compositor.** Never by title (session 42 closed the
  user's pcmanfm-qt window because its title was "K-Term"); `$!` after `timeout ./bin/k-term &` is the
  timeout wrapper's pid, not k-term's. Use "the window id that appeared after launch" (`.scratch/reflow_live.sh`
  does). niri's `--json windows` has no absolute screen position, so screenshots cannot be cropped to a
  window from it, and tiling/floating moves the OTHER windows too, which breaks screenshot pixel-diffs.
