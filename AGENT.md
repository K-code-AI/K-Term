# K-Term — Agent Journal

Read this (plus ISSUES.md) before touching the project. Bug root causes live in ISSUES.md,
module design in docs/ARCHITECTURE.md, user-facing behaviour in README.md. This file keeps only
the current verified state, the environment facts, the pitfalls a newcomer must know, and a
short history.

## Project Overview
K-Term is a lightweight Linux terminal emulator for Wayland written in C: native Wayland window
(xdg-shell) + PTY shell + VT/ANSI parser + EGL/OpenGL text rendering from two glyph atlases (alpha8
mono + RGBA8 color emoji) in one draw call per frame. No GUI framework.

## Current Status (build/test/asan re-verified 2026-10-15, session 55)
- Clean tree: `make` -> zero warnings under the Makefile's `WARN` set (`-Wall -Wextra -Wshadow
  -Wwrite-strings -Wformat=2 -Wdiscarded-qualifiers`, tightened in session 50; the tree is also clean
  under `-Wcast-qual -Wmissing-prototypes -Wstrict-prototypes -Wold-style-definition -Wredundant-decls`
  except inside the generated xdg-shell bindings, which is why `-Wcast-qual` is not in `WARN`);
  `make test` -> `364 checks, 0 failures` + `MODEL OK (0 problems)` + `CLI: 77 checks, 0 failures, 0
  skipped`; `make asan` -> `364 checks, 0 failures` + `MODEL OK`, with NO LeakSanitizer/UBSan report.
  No temporary probe or experiment remains in the tree (`grep -rnE "TEMPORARY|PROBE|EXPERIMENT|NEGATIVE"
  src/ include/ tests/` -> nothing). Re-run from a clean tree by session 55 (`make clean && make` 0
  warnings `/tmp/s55_build.log`, `make test` `/tmp/s55_test.log`, `make asan` `/tmp/s55_asan.log`);
  README's flags table (`-e -w -c -d -v -h`) and its 18 settings rows match `src/main.c` and
  `src/config.c`.
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
DONE (session 55) — documentation accuracy pass + commit, at the user's request ("Cleanup + commit").
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

OPEN WORK (the only two real bugs left, both user-reported, both in ISSUES.md "Open"):
* **#1:** launching `k-term` while ANOTHER k-term is still running prints `atlas glTexImage2D failed:
  0x505 (512x512)` and then `SIGSEGV`; with no leftover process it starts fine twice in a row. Not
  reproduced here yet.
* **#2:** on launch, `eglSwapBuffers failed: 0x3000` + `swap failed (1 in a row), retrying on the next
  frame`; no visible effect, the retry works. Reproduce, explain, fix if it is a real defect rather than
  a benign first-frame race.

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
  NEXT ACTION: commit + push this state, then take on ISSUES.md Open #1 (the second-instance
  `glTexImage2D 0x505` + SIGSEGV) — the only crash left — using the source and the tests, NOT screen
  recording (the user's standing instruction: they verify anything visual themselves).

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

## Next Steps
1. **Two real bugs are open, both user-reported, both in ISSUES.md "Open"** — start with #1, the only
   crash left:
   * **#1** launching `k-term` while another k-term is still running prints `atlas glTexImage2D failed:
     0x505 (512x512)` (GL_INVALID_VALUE on the mono atlas upload in `render_draw`) and then SIGSEGV.
     Never reproduced here yet. Work from the source and a headless harness, not from the screen.
   * **#2** `eglSwapBuffers failed: 0x3000` (EGL_BAD_ALLOC) on launch plus one `swap failed (1 in a row)`
     warning; the retry works and nothing looks wrong. Decide whether it is a real first-frame race worth
     fixing or just a log line to soften.
   Everything else the user has confirmed working in their own window (padding, zoom, fullscreen,
   black-window fix, heavy-output segfault, clipboard both ways, transparency, key repeat, selection).
2. Housekeeping options (none of them a bug): make the ASan build of the `k-term` binary a real Makefile
   target (only the tests have one; the hand-built one in `.scratch/kterm_asan` found the `row[rcap]`
   overrun, so covering the Wayland/EGL path would pay off — it would also cover the heap-grown
   `paste_buf`); idle-frame optimization (stop the frame cycle when nothing is dirty); the color atlas is
   re-uploaded whole with `glTexImage2D` whenever a new emoji is shaped — `glTexSubImage2D` per new shelf
   would be cheaper; a reflow allocates a fresh row buffer per produced line (fine at terminal sizes, but
   a huge-width window with a huge scrollback churns the allocator); the window title is parsed into
   `Term.title` but never pushed to the compositor (ISSUES.md "Known limitations" — wiring it up is a
   one-liner in `xdg_toplevel_set_title`, but it changes visible behaviour, so ask first).
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
├── Makefile                   # src/*.c -> build/*.o -> bin/k-term; test/test-cli/asan/run/clean; WARN set
├── docs/ARCHITECTURE.md       # module map, event loop, grid invariants, reflow, color emoji, debug entry points
├── include/k-term/k-term.h    # the single shared header (all types + prototypes)
├── src/                       # config.c vt.c pty.c font.c render.c main.c
├── tests/test_vt.c            # 364 checks: parser, grid, selection (incl. sub-cell), alt screen,
│                              #   live_off, emoji clusters (ZWJ / skin tone / VS16 / keycap / flag pair),
│                              #   CSI 3 J, bracketed paste, term_init guard, REFLOW (round trip, cursor,
│                              #   wide pairs, eviction, hard breaks, coloured blanks, alt screen),
│                              #   term_grid_geometry (padding + the pointer/cell sweep), the zoom/alt-screen
│                              #   row-shrink rule, a viewport grown past half of rcap (the row[] overrun)
├── tests/dbg.c                # grid-model invariant checker (ed3*, resize, reflow, grow-then-scroll and
│                              #   zoom/alt-screen permutation stages)
├── tests/cli_test.sh          # 77 checks over the real bin/k-term: every flag, long form, error message
│                              #   and exit code (main() is in no test binary, so this is the only honest
│                              #   way to cover argv); run by `make test-cli`, which `make test` needs
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
- **Pitfall — splicing this journal with computed indices has damaged it twice (sessions 54 and 55).**
  Both accidents had the same cause: an anchor string that also appears INSIDE a journal entry (entries
  quote section headings like `## Next Steps` / `## Environment` when describing earlier accidents), or an
  index captured before an earlier mutation in the same script. Rule: anchor on text that is unique in the
  file (check with `grep -c` first), re-find every anchor after each mutation, and `git diff` the result
  before moving on. Session 55's accident cut a Progress entry in half and moved it next to a heading it
  quoted; it was repaired from `/tmp/s55/AGENT.md.before_repair`, and the entry that describes it is the
  session-54 one.
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
