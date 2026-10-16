# Kai-Term — Known Issues

Read this before starting work. Root causes are kept short — enough to reproduce and to avoid
re-introducing the bug. Module design and the invariants a change must not break are in AGENT.md
("Architecture & Invariants").

> **Docs moved 2026-10-16 (session 63):** `docs/ARCHITECTURE.md` no longer exists — it carried facts about
> the host machine, so it was removed from the project. Its invariants are now in AGENT.md
> "Architecture & Invariants", its tooling/host parts are in `/home/kaida/Project/ENVIRONMENT.md` (outside
> this repo). Entries under "Fixed" still name the old file where they were written against it; that is the
> record of what was really done, not a pointer to read.

> **Renamed 2026-10-16 (session 59):** the project, binary, config file and app id are now `Kai-Term` /
> `kai-term` (was `K-Term` / `k-term`). Everything below is written with the new names EXCEPT verbatim
> quoted output and the entries under "Fixed", which keep the spelling they were recorded with — those
> lines were really printed by the old binary.

## Cleared (2026-10-17, session 64)

The four items that used to be numbered **Open #1-#4** are cleared at the user's request: none of them was
ever reproduced here, and the user is not convinced they are Kai-Term defects rather than
environment/compositor behaviour. Cleared is NOT fixed - nothing was verified as a fix, so they do not belong
under "Fixed". Anything seen again should be re-filed as a new Open item with a reproduction. One line each,
kept so a re-report starts from the measurements that already exist. (The session-42 dismissal of the
`0x505`/`0x3000` lines as probe-harness artifacts was withdrawn in session 52 - the user did see them in
normal use - so "cleared" here means unproven, not disproven.)

- **Open #1 - second instance: `atlas glTexImage2D failed: 0x505 (512x512)`, then SIGSEGV** (user report,
  2026-10-15, with a `kai-term` left running in the background). `0x505` is `GL_INVALID_VALUE` at the mono
  atlas upload in `render_draw` (src/render.c): the first frame that has glyphs to draw. Never reproduced
  here - two launches in a row both work when no instance is left over.
- **Open #2 - `eglSwapBuffers failed: 0x3000` on launch** plus one `swap failed (1 in a row)` warning (user
  report, 2026-10-15). `0x3000` is `EGL_BAD_ALLOC`. No visible effect: the 8 ms timer retries, and the
  frame-pacing code already keeps the frame dirty and arms no frame callback after a failed swap.
- **Open #3 - LeakSanitizer reports only third-party frames** (libdbus-1, libwayland-client, dynamically
  loaded driver modules). A hand-built ASan `kai-term` over a full multi-session run (session 61): no
  AddressSanitizer error, no UBSan runtime error, 1250 bytes leaked in 5 allocations, none from Kai-Term
  source. Not a Kai-Term bug.
- **Open #4 - fullscreen freezes the display, INTERMITTENT** (user report, session 58; "doesn't always
  reproduce", deferred by the user on 2026-10-16). Never reproduced here. The mechanism behind the earlier,
  confirmed version of this bug and the levers for detecting it (`toplevel state: fullscreen 1 …` with no
  matching `configure answered:` line, a `swap failed (N in a row)` streak, `cat /proc/<pid>/wchan` =
  `ep_poll` vs `futex_do_wait`) are recorded under "Fixed: Fullscreen: the display stopped refreshing" and in
  AGENT.md "Key Decisions & Pitfalls".


## Known limitations
Not bugs — current behaviour the user has to know about. (Root causes of actual bugs are in "Fixed".)

* **A taller window keeps less scrollback than configured.** The grid allocates `rows + scrollback`
  row buffers once, at startup (`rows` = the configured initial height), and that capacity never changes.
  Every row the viewport gains by growing is a row the scrollback loses: with `rows = 30`,
  `scrollback = 2000` and a 62-row window, 1968 history lines fit. Shrinking the window back does not
  bring the capacity back either — the allocation is fixed for the process.
* **Narrowing a window can drop the OLDEST scrollback lines.** Reflow re-wraps every retained line at
  the new width; if the re-wrapped content needs more rows than the grid holds (`rows + scrollback`),
  the oldest lines are evicted, exactly like ordinary scrollback overflow, and the newest content always
  survives. `-d` logs it: `reflow: 55 -> 58 rows at 26 cols (3 evicted)`. Widening again cannot bring
  evicted lines back.
* **Reflow applies to the normal screen only.** While an application owns the alternate screen
  (`?1049h`: vim, htop, less) its rows are still truncated/grown in place (`grid_resize_plain`) — a
  full-screen app repaints itself after SIGWINCH, and re-wrapping its output would scramble it. The
  parked main screen IS reflowed, so the text you return to is intact.
* **Selection is viewport-relative.** You cannot select text that is only visible after scrolling up:
  navigating scrollback, resizing the window or zooming clears the selection (`term_select_clear`).
* **Selection highlights whole cells only** (by design since session 41): a highlighted range always
  starts and ends on a cell edge, so a partial glyph is impossible. The band is skipped over cells that
  were never written (`ch == 0`), so a selection dragged past the end of a line stops at the last
  glyph — while real spaces (`' '`) and the continuation cell of a wide glyph stay highlighted. The
  copied text is unaffected (blank cells copy as spaces, trailing spaces per line are trimmed).
* **A few leftover pixels at the bottom/right of the window.** The grid is sized with floor division
  (`cols = w / cell_w`), so unless the window is an exact multiple of the cell, `w % cell_w` px and
  `h % cell_h` px are left over (e.g. 3 x 8 px on a 1253x1002 window with a 10x18 cell). They are painted
  with the configured `background`, exactly like the `padding` margin, and stay at the bottom/right:
  the grid is anchored at the top-left so `ptr / cell` names the right cell (see AGENT.md,
  "Architecture & Invariants — Geometry"). The bottom margin is therefore `padding` + the vertical remainder.
* **Color emoji come from whatever color font fontconfig finds.** With a bitmap-only font
  (Noto Color Emoji on this box) there is a single 128px strike, so zoomed emoji are scaled from that
  one strike instead of being re-rendered. At most `CLUSTER_MAX` = 8 modifier codepoints fold into one
  anchor cell (longer sequences split into an extra cell). Emoji that Unicode defines as
  text-presentation (heart, keycap bases, ...) stay monochrome unless the sequence carries U+FE0F.
  Some glyphs are grayscale in the installed font (e.g. the family glyph in the current
  NotoColorEmoji.ttf) — font data, not a rendering bug. The scalable-color-font branch
  (`FT_IS_SCALABLE` -> `FT_Set_Pixel_Sizes` + `hb_ft_font_changed`) has never run on this machine.
* **A mouse drag alone does not copy.** With the default `copy_on_release = 0` (session 43) releasing the
  drag only leaves the selection visible — the text is NOT on the clipboard, so pasting into another
  application gives whatever was there before. `Ctrl`+`Shift`+`C` (or `copy_on_release = 1`) is what puts
  it on the system clipboard. This is the most likely cause of a "Kai-Term has its own clipboard" report:
  the copy never left Kai-Term because it was never offered. The clipboard itself is the ordinary Wayland
  selection (`wl_data_device`), shared with every other client — verified in both directions (see
  AGENT.md, "Architecture & Invariants — Wayland/input invariants").
* **Kai-Term offers only `text/plain` when copying** (and asks for one mime type when pasting). Real clients
  usually advertise `UTF8_STRING`, `text/plain;charset=utf-8`, `TEXT` and `STRING` as well; niri hands
  those over to a `text/plain` request without trouble, so this works here, but a compositor that matches
  mime types strictly could refuse the offer. Related protocol detail: Kai-Term never calls
  `wl_data_offer_accept()` before `wl_data_offer_receive()` — niri tolerates it (measured: `wl-copy` text
  reaches Kai-Term and lands in the PTY byte-for-byte), but the protocol says a receiver should accept a
  type first, so that is the first thing to check if pasting from another application ever breaks.
* **The window title is Kai-Term's own, not the application's.** The title IS pushed to the compositor
  (`update_title()` -> `mux_title()` -> `xdg_toplevel_set_title()`): `Kai-Term` with one session,
  `Kai-Term [active/total]` with several, plus `!N` when N sessions you are not looking at have produced
  output. What is NOT in it is an application's OSC 0/2 title — that goes into the session's tab bar label
  (or `shell` until the session sets one). A taskbar, a window rule or a script that matches on the window
  title therefore sees Kai-Term's text, not the shell's.
* **The tab bar costs one text row, and the grid wins if there is not room for both.** With `tab_bar =
  always`, or from the second session on with the default `auto`, one cell row at the top of the window is
  chrome. If the window is too small to hold that row, its gap and one text row, the bar is dropped for that
  frame rather than shrinking the grid to nothing.
* **Bar labels are cut to their item's width, with no ellipsis.** Items have equal width (the grid width
  divided by the number of sessions), so with many sessions a label can be only a few characters. The cut
  happens at a UTF-8 codepoint edge, never inside a multi-byte sequence, and the `*` unread mark and the
  session number are part of the label, so a long OSC title is what gets truncated.
* **Only `split_max` sessions are drawn in side-by-side view.** The others stay open, keep running and stay
  listed in the bar; their output reaches you only as the `*` unread mark and the `!N` in the window title.
  The shown group of panes slides to contain the active session, so the first and last sessions really are
  reachable.
* **All sessions of a window share one grid size.** A layout change — a window resize, a zoom, the
  tab/side-by-side toggle, opening or closing a session — resizes EVERY session to the same cols x rows, so
  every shell gets `SIGWINCH` and every session's scrollback is re-wrapped at the new width. That is exactly
  what makes switching to a hidden session free (no resize, no re-wrap on every switch), and it is why a
  toggle touches sessions you are not looking at.
* **A clipboard paste larger than `PASTE_BUF_MAX` (8 MiB) is sent unwrapped** — no `ESC[200~` /
  `ESC[201~` delimiters — with a warning in the log, because the whole block has to be buffered before
  the delimiters can surround it.
* **Transparency needs a compositor that blends ARGB surfaces.** `opacity` is verified working on niri
  (session 41, screenshot A/B); on a non-compositing or RGB-only path the window simply looks opaque.
* **Minimum window size 200x100 px** (`xdg_toplevel_set_min_size`); a compositor may still send a
  smaller configure and Kai-Term handles it (110 px high -> 6 rows), but interactive resizing stops there.
* **Bracketed paste is per application.** Kai-Term sends the delimiters only when the application has
  DEC mode 2004 on; a shell/readline build with bracketed paste disabled correctly keeps getting raw
  (line-by-line) pastes.
* **Key repeat tracks one key at a time.** A Wayland compositor never sends repeat events, so Kai-Term
  generates them itself (`repeat_delay` ms, then `repeat_rate` per second). Only the most recently
  pressed repeatable key repeats: holding `a`, then pressing and releasing `b`, stops `a`'s repeat even
  though `a` is still held. The compositor's own `wl_keyboard.repeat_info` (niri here: 25/s, 600 ms) is
  logged under `-d` but deliberately NOT used, so the timings come from the config file and are the same
  on every compositor. `repeat_delay = 0` turns Kai-Term's repeat off (nothing repeats — a compositor
  never repeats for you).

## Fixed (root cause -> fix)
- **A click on the tab bar, or on a pane that was not the visible one, logged a switch that never
  happened** (session 61, found by the live key/pointer-injection run that session 56 had never got; fixed
  the same session). Symptom: the log said `bar: click selects session 2/3` and `mux: tab 1/2 visible`, but
  the visible session did not change — and in side-by-side view every click in the LEFT pane reported
  `cell=(0,29)`, the value the clamp produces when the grid origin still belongs to the other pane (pane 0
  x=8, pane 1 x=630, cell 8 px: surface x=20,100,250,400,430,500 all reported column 0).
  Root cause: `tab_switch(idx)` in src/main.c re-applied the layout and marked the frame dirty but never
  called `mux_goto()` — only the keyboard paths ever changed the active tab, because `mux_next`/`mux_prev`/
  `mux_goto` are called from `send_key()` and their return value was merely passed to `tab_switch()` as an
  argument. The pointer paths passed an index that nothing consumed, and the log line printed the requested
  index, so it claimed a switch that had not happened.
  Fix: `tab_switch()` calls `mux_goto(&mux, idx)` itself and logs `act + 1` from what mux REPORTS, so the
  line cannot lie; the keyboard calls become idempotent no-ops.
  Verified LIVE (`.scratch/s61/barclick.sh`, `/tmp/s61_bar.log`): left click on bar item 2 -> `bar: click
  selects session 2/3` + `mux: tab 2/3 visible`; a click in the left pane of a 2-pane window -> `mux: tab
  1/2 visible` AND `sel press cell=(30,29) … cols=76`, i.e. it both focused that session and mapped through
  its pane. `.scratch/s61/panemap.sh` (`/tmp/s61_map.log`): surface x=20/100/250/400/430/500 -> columns
  1/11/30/49/52/61 in pane 0, x=850/880 -> columns 27/31 in pane 1. Coverage note: main.c is in no test
  binary and no test can send a pointer button, so this rests on the live runs, not on `bin/test_vt`.
- **`BTN_RIGHT` and `BTN_MIDDLE` were swapped** (session 61, found while testing the bar middle-click; fixed
  the same session). `map_button()` in src/main.c labelled raw 0x111 as middle (SGR button 1) and 0x112 as
  right (SGR 2), but the kernel defines `BTN_LEFT` 0x110, `BTN_RIGHT` 0x111, `BTN_MIDDLE` 0x112
  (`/usr/include/linux/input-event-codes.h:357-359`) — the kernel order is NOT the protocol order.
  Consequences: an injected middle click on a bar item produced NO `bar: middle click closes session` line at
  all; a RIGHT click on the bar hit the `btn == 1` branch and would CLOSE a session; and middle/right were
  swapped in SGR mouse reporting for vim/htop/etc.
  Fix: `map_button()` deleted. The mapping is now `mouse_sgr_button(uint32_t evdev)` in src/vt.c (0 left, 1
  middle, 2 right, 3 anything else) with an `EVDEV_BTN_*` enum in the header — in vt.c because that is where
  the mouse-reporting state lives and because main.c is in no test binary. 6 new checks pin it, including the
  raw literals (`EVDEV_BTN_RIGHT == 0x111 && EVDEV_BTN_MIDDLE == 0x112`) so the enum cannot be re-ordered to
  match a wrong switch again. Negative test: the two cases swapped back -> `496 checks, 3 failures`, exactly
  `BTN_MIDDLE -> SGR 1`, `BTN_RIGHT -> SGR 2`, `raw 0x111 is right, raw 0x112 is middle`. Verified live:
  middle click on bar item 3 -> `bar: middle click closes session 3/3` + `mux: tab 3 closed, 2 left`.
- **`tab_bar = always` started the window one row short of the configured `rows`** (session 62, found while
  checking docs/ARCHITECTURE.md against the source; fixed the same session). src/main.c asked the compositor
  for `rows*cell_h + 2*padding`, plus `cell_w + PANE_GAP` for the bar — but the bar is one cell ROW tall
  (`out->bar_h = ch` in src/layout.c), so the addition has to use `cell_h`. With a non-square cell (8x17 on
  this box) the window is asked for 9 px too little and `layout_compute()` hands back `rows - 1`: the one
  case where the configured `rows` is silently not what the user gets.
  Fix: the arithmetic moved out of main.c (which is in no test binary) into `layout_initial_size()` in
  src/layout.c — the inverse of `layout_compute()` for the startup case — and main.c calls it.
  Verified: 5 new checks (496 -> 501) that feed the requested size straight back through `layout_compute()` +
  `layout_grid_size()` and require the configured 80x30 to come out again, for `BAR_ALWAYS` and `BAR_AUTO`.
  Negative test: `*win_h += ch + g` changed back to `cw + g` -> `501 checks, 2 failures`, exactly the two
  `BAR_ALWAYS` checks (the `BAR_AUTO` ones still pass, correctly — that path adds no bar row). NOT verified:
  what this looks like in a real window — niri tiles the window to its own size, so the requested startup
  height is not observable from the log here.
- **The band around the grid was painted with `DEF_BG`, not the configured `background`** (found by the
  session-54 padding work; the user CONFIRMED it correct in their real window on 2026-10-15, and a
  lossless `wf-recorder -c ffv1` frame re-check agrees). Symptom before the fix, with `background =
  0a0a2e` and `padding = 20`: the configured colour's bounding box was exactly the grid and the 20 px
  band around it was `(16,16,20)` = `DEF_BG`. Root cause: `render_init` hardcoded `DEF_BG` in
  `glClearColor`, and nothing else ever writes the pixels outside the cell quads — the padding margin,
  and before that the whole-cell remainder at the bottom/right of any window whose size is not an exact
  multiple of the cell. It also cleared only once, in `render_init`, so only one of the driver's buffers
  was ever cleared.
  Fix (session 54): `Render.bg` is set from `conf.bg` through the new
  `render_init(..., float opacity, uint32_t bg)` parameter; `set_clear_color()` premultiplies it by
  `Config.opacity` (the buffer is premultiplied — same rule as the shader); `render_draw` clears every
  frame. Verified: a recorded frame with a custom `background` + `padding = 20` has ZERO `(16,16,20)`
  pixels and the whole window bbox reads exactly `(10,10,46)`.
- **The command-line surface had no automated coverage** (session 52). `make test` ran `tests/test_vt.c`
  and `tests/dbg.c`, which link `src/vt.c` + `src/config.c` only, so `src/main.c` — and with it `main()`'s
  argument parser — was in no test binary: renaming a flag, breaking `--name=VALUE` or reviving a
  special-case error message left `make test` green. The session-51 flag change had been verified only by
  a throwaway script in `/tmp`.
  Fix: `tests/cli_test.sh` (POSIX sh, checked in) + a `test-cli` Makefile target that depends on
  `bin/k-term` and runs it; `test` depends on `test-cli`, so `make test` covers the flags. The script
  drives the REAL binary (argv cannot be unit-tested: main.c is not linked into `bin/test_vt`), wraps every
  launch in `timeout -k 2 15`, cleans its temp dir with a trap, and prints a SKIP for the window-opening
  checks when `WAYLAND_DISPLAY` is unset. Coverage: `-h`/`--help` identical and listing all six options
  with no `-C`; `-v`/`--version`; `-C -f -s -r -o -x --bogus` each rejected with the identical generic
  first line + usage + exit 1 + nothing on stdout; every option's missing-argument message; `-c FILE` /
  `--config FILE` / `--config=FILE` really loading the file; `-d` beating `log = 0` in the file; `-w DIR`
  logging `shell starts in DIR`; `-e COMMAND` exiting by itself; all flags combined.
  `make test-cli` -> `CLI: 77 checks, 0 failures, 0 skipped`. Negative test: reverting `-c` to `-C` and
  re-adding a `-o`-specific message produced 15 failures naming exactly those behaviours.
- **Zooming in and out left stale prompt lines and uneven gaps** (user report, 2026-10-15, sessions
  53-54, the user CONFIRMED the fix in their real window: "Zoom is fixed and now works as expected").
  Screenshots compared K-Term with kitty: kitty's prompts stay packed, K-Term's drifted apart and old
  prompt lines stayed on screen. Reproduced headless (`.scratch/s53/pty_zoom.c`: real fish in a real
  PTY, real vt.c model, zoom-style resizes where cols AND rows change together):
  ```
  == startup (30x100):      prompt at slots 1-2, cursor cy=2
  == zoom in 2 (41x78):     prompt at slots 1-2 (now scrollback) AND a new one at 10-11  -> 8 blank rows
  == zoom out 3 (62x117):   prompts at 1-2, 10-11, 13-14                                 -> two gaps
  ```
  Root cause: `grid_resize_rows()`'s shrink loop always gave up the viewport's TOP row (it became the
  newest scrollback line). When the viewport shrank by MORE rows than the cursor had rows above it — the
  normal case at a prompt, where everything below the cursor is blank — the cursor's own line was pushed
  into scrollback and `term_resize()` then clamped `t->cy` to 0. The cursor was no longer on its content
  line, so the shell's SIGWINCH prompt repaint drew a SECOND prompt at the clamped cursor and the
  original stayed behind as stale text; each zoom step added one more copy. Not a renderer bug, not
  reflow, not the zoom code (`zoom_apply`/`term_size_changed` are fine).
  Fix (src/vt.c + the k-term.h prototype): `grid_resize_rows(Grid *, int rows, int cur_slot)` gives up
  the viewport's BOTTOM row (blanked, becomes a free slot) while the cursor still has rows below it, and
  only after that falls back to the old top-row-into-scrollback behaviour (which keeps the newest content
  visible and moves the cursor's viewport row with it). `grid_resize` passes the cursor slot AFTER
  `grid_reflow` has moved it; `grid_resize_plain` gained a `cur_slot` argument so the alternate screen
  (vim/htop) keeps the same rule; -1 means "no cursor to preserve" = the old behaviour.
  Verified: 23 new checks in tests/test_vt.c (cursor slot preserved across a shrink that removes more
  rows than the cursor has above it, content below the cursor dropped rather than scrolled, a full
  viewport still scrolls into scrollback, mid-text shrink, alt-screen shrink, three zoom cycles leave
  exactly the two prompt lines) + `zoom-*`/`alt-*` stages in tests/dbg.c for the row[] permutation
  invariant; 323 -> 346 checks, `MODEL OK`. Negative test: the new checks against the pre-fix vt.c give
  16 failures, all of them the zoom checks. The headless fish harness now shows exactly ONE prompt after
  six zoom steps (was three prompts and two gaps).
- **Fullscreen: the display stopped refreshing** (user report, `NextFeature.md` #2; sessions 45-47, the
  user CONFIRMED the fix in their real window on 2026-10-15). NOTE (session 59): the user has since reported
  the freeze coming back, intermittently — that was tracked as Open #4 and cleared unproven on 2026-10-17
  (see "Cleared" above), and this entry stands as the record of the root cause and of the rules that fixed
  it, not as a claim that fullscreen is freeze-free. Symptom was: enter fullscreen (niri
  `Mod+Shift+M`) and the terminal stops updating — no typed characters and no command output appear —
  until fullscreen is exited, then everything resumes at once. The user had confirmed it was a K-Term bug,
  not a niri one.
  Diagnosis (measured with a standalone EGL client, `.scratch/egl_seq_probe.c`, plus a probe in the
  draw path): the freeze was the client's draw/commit cycle, and it had two halves.
  * `eglSwapBuffers` does not return until the driver gets a buffer back, and only the compositor
    releases buffers. A swap that blocks takes the whole event loop with it — no keyboard, no PTY reads,
    no redraw — which is exactly "no input nor output appear", and it unblocks the moment the compositor
    renders the surface again (i.e. when the layout changes = when fullscreen is exited).
  * What made the compositor stop rendering the surface: a configure that changed the size was answered
    with a commit that attached NO buffer (the old `solicit_frame()` path armed a `wl_surface_frame`
    callback on that empty commit; a damage-driven compositor only delivers frame callbacks during a
    render pass, so the callback could stay pending forever, and every draw path is gated on "no
    callback in flight"). In a tiled layout other windows keep the output repainting so the done still
    arrives; a fullscreen terminal is the only thing on its output, so the stall is permanent.
  Fix (src/main.c): `solicit_frame()` is gone; `redraw()` honours `render_draw()`'s success/failure and
  arms a callback only after a swap that really attached a buffer; a size change sets `W.resized`, and
  both the 8 ms timer and `frame_done()` draw when `dirty || resized`, so a configure is always answered
  with a buffer of the new size; and (session 47) no swap ever runs while a `wl_display_prepare_read()` is
  outstanding — the loop finishes the wayland read and dispatches pending events before it handles any
  other descriptor. The first configure is the one place a swap is mandatory inside a listener, because
  the window cannot map without a buffer. Session 47 additionally made the timer-driven swap SAFE (it used
  to deadlock — see "Fixed: Black window, no prompt, no input"), which is what makes the `resized` answer
  actually reach the screen in fullscreen.
  VERIFIED: by the user in the real window (fullscreen refreshes, output keeps appearing) — that is the
  verification the work order required and forbade self-testing. Supporting evidence measured here:
  `make` 0 warnings, `make test` 323/0 + MODEL OK, `make asan` clean, a live `-d` run logging
  `configure answered: buffer 936x1002 attached (grid 117x62)`, and a niri resize sweep with 10
  `grid resized` + 11 `configure answered` and 0 swap failures. The `-d` lines to look for in future
  reports are `toplevel state: fullscreen 1 …` followed by `configure answered: buffer <output size>
  attached (grid …)` (and the same pair on exiting); a long streak of `swap failed (N in a row)` means the
  compositor is not showing the window at all.
- **Black window, no prompt, no input (the whole terminal frozen)** (session 47, user-reported regression
  of the session-46 draw/commit rewrite). Reproduce with the session-46 tree: `./bin/k-term -d -e 'for i
  in $(seq 1 40); do echo $i; sleep 0.2; done'` — about 1 run in 10 (measured 1/10 and 2/10 in a loop, and
  consistently for the user) shows only the first, text-free frame and never responds again; the process
  stays alive with `/proc/<pid>/wchan` = `futex_do_wait`.
  Root cause: `eglSwapBuffers` does not return until the compositor gives a `wl_buffer` back, and that
  release can only be read by this same thread. The event loop called `wl_display_prepare_read()` at the
  top of every iteration and only `wl_display_cancel_read()` AFTER the fd loop, so the session-46 change
  (the timerfd branch now draws instead of calling `solicit_frame()`) ran a swap while a prepared read was
  still outstanding: the release could never be read, the wait was permanent, and with the loop blocked
  there were no PTY reads, no keys and no redraw. Measured with temporary probe lines in the draw path
  (`/tmp/s47_probe2.log`: `pre-swap nquads=7259` and no `swap RETURNED`) and A/B against the last
  user-confirmed-good tree, which swapped only inside listeners — i.e. after the read was already complete
  (`/tmp/s47_oldprobe.log`: every `SWAP RETURNED`). The earlier "never swap inside a listener" rule was
  therefore BACKWARDS; the real rule is "never swap while a prepared read is outstanding".
  Fix (src/main.c): the epoll iteration now scans the returned fds for the display fd, finishes the read
  (`wl_display_read_events()`, or `wl_display_cancel_read()` when epoll woke for something else) and
  `wl_display_dispatch_pending()`s BEFORE the pty/timerfd/signalfd/paste/repeat branches run; the old
  read/cancel block at the end of the iteration is gone. The frame-pacing comment now states the correct
  rule. Verified: probe build `/tmp/s47_fix1.log` — 31 swaps, all `swap RETURNED`, 31 `frame_done`, clean
  `pty closed` + `k-term exiting` (before: swap #2 never returned); the timer-driven swap specifically is
  proven safe (`tick 26 dirty=1 cb=(nil)` -> `redraw enter` -> `swap RETURNED`). 10/10 launches exit
  normally after the fix (`.scratch` harness `/tmp/s47_rate2.sh`) versus 1/10 stuck before. Live resize
  sweep (`/tmp/s47_stress2.log`): 10 `grid resized` + 11 `configure answered`, 0 swap failures, `wchan`
  stays `ep_poll` throughout. Visual proof (`wf-recorder` + frame diff, `/tmp/s47_vis/`): bright text
  pixels grow monotonically 1971 -> 17272 across 28 sampled frames, i.e. the picture keeps updating.
  `make` 0 warnings, `make test` 323/0 + MODEL OK, `make asan` clean.
- **Segfault after ~2000 lines of output in a window taller than the configured rows** (session 47, found
  while verifying the fix above; PRE-EXISTING, unrelated to the draw path). Reproduce with the pre-fix
  tree: `./bin/k-term -d -e 'for n in $(seq 1 5000); do echo $n; done'` -> `Segmentation fault (core
  dumped)`, 3/3 runs. ASan build of k-term itself: `heap-buffer-overflow, READ of size 8 at 0 bytes after a
  16240-byte region` in `grid_blank_slot` (src/vt.c) <- `scroll_up` <- `linefeed` <- `term_parse`; the
  region is `row[]` (16240/8 = 2030 = `rcap`), so the code touched `row[rcap]`.
  Root cause: `rcap` is fixed at `rows_at_init + scrollback_max`, but `scrollback_max` is never reduced
  when the viewport GROWS, so after a grow the usable scrollback is `rcap - rows` while the code still
  bounds `scroll_off`/`live_off` by `scrollback_max`. Startup is exactly that case (`rows = 30` from the
  config, a 62-row window, `scrollback_max = 2000`): `scroll_up` gated on `so < scrollback_max` and then
  blanked `row[so + rows]`, which reaches index 2030 once `so` saturates at 1968.
  Fix (src/vt.c): `sb_limit(g) = imin_(scrollback_max, rcap - rows)` is now the single bound for
  `scroll_off`/`live_off` — used by `grid_reflow`'s anchor, both `grid_resize_rows` branches and both
  whole-viewport branches of `scroll_up`; the grid invariant comment states it. Verified: 11 new
  `tests/test_vt.c` checks (312 -> 323: rcap fixed across a grow, `used <= rcap`, `live_off <= rcap - rows`,
  newest line survives, scroll-down stays in bounds, and the same after shrinking again) plus new
  `tests/dbg.c` stages `growthen-scroll-before/-scroll/-SD/-shrink/-scroll2` (init 4 rows + 20 scrollback,
  grow to 12, print 60 lines). Those stages reproduce the identical ASan error against the pre-fix vt.c
  (`SUMMARY: heap-buffer-overflow ... grid_blank_slot`) and pass on the fixed one: `make test` 323/0 +
  MODEL OK, `make asan` clean, and the live 5000-line run now ends with `k-term exiting` 3/3 (also clean
  under the ASan build of k-term).

- **No reflow when the width changed** (session 43; the "fully resizable" complaint's real cause). Was:
  `grid_resize`/`row_resize` realloced every row (viewport *and* scrollback) to the new column count, so
  narrowing permanently DELETED the cells past the new width and widening left blanks — text never
  re-wrapped and never came back. Reproduce the old behaviour: `seq 1 20 | awk '{print $0"  a-long-tail
  -of-text-here"}'`, shrink the window, grow it back — the tails were gone.
  Root cause: the grid had no way to tell a RIGHT-MARGIN soft wrap from a line the application ended
  with CR/LF, so a physical row was the only unit it could resize.
  Fix: `Grid.wrap` (one byte per `row[]` slot, set when the right margin breaks a line, rotated with
  `row[]` by `rot_left`/`rot_right`, cleared whenever a slot is blanked or its line erased) +
  `grid_reflow()`: join the physical rows of each logical line, drop the never-written tail
  (`row_used`/`cell_pristine` — a cell counts only when ch, nextra, attr and BOTH colours are default,
  so a coloured background run survives and the padding a wide glyph leaves at a row end is not turned
  into a literal space), re-break into chunks of the new width with the boundary pulled back so a wide
  glyph is never split from its `ATTR_WIDE` continuation, allocate every output row fresh, free every
  old one, and evict from the OLDEST end when the re-wrapped content exceeds `rcap`. The cursor travels
  with its cell (logical line + column). `grid_resize_plain()` keeps the old in-place behaviour for the
  alternate screen. Verified: 32 new `tests/test_vt.c` checks (260 -> 292) — marks, narrow/widen round
  trip byte-for-byte, cursor mapping, wide-pair integrity (CJK 10 -> 4 -> 10 cols keeps exactly 1
  continuation cell and 0 orphans), eviction invariants (`used <= rcap`, `live_off <= scrollback_max`,
  newest line survives), hard breaks never joined, coloured blanks preserved, short lines not multiplied,
  scrollback lines reflow too — plus `tests/dbg.c` reflow stages asserting `row[]` stays a NULL-free,
  duplicate-free permutation of exactly `rcap` buffers after every step. `make test`/`make asan` ->
  `292 checks, 0 failures` + `MODEL OK (0 problems)`, no sanitizer report. Live (niri IPC resize,
  `.scratch/reflow_live.sh`, log `.scratch/s43_reflow_live.log`): 936x1002 -> 1400x1002 -> 936x800 ->
  500x800 -> 500x600 -> 260x600 -> 500x400 -> 1600x400 -> 1600x900, `reflow:` lines at every width
  change, 0 error lines, no crash.
- **`make asan` reported a leak that did not exist (stale sanitizer binary)** (session 43). Symptom:
  `Direct leak of 312 byte(s) in 2 object(s)` from `row_new_cols` <- `grid_reflow` <- `term_resize` at
  `tests/dbg.c:129`, on a tree whose tests passed. Root cause: the Makefile rule was
  `bin/dbg_asan: tests/dbg.c | bin` — it compiles `src/vt.c` and `src/config.c` directly but declared
  neither as a prerequisite, so make never rebuilt `bin/dbg_asan` after `src/vt.c` changed and the report
  came from a pre-fix binary. Diagnosed by compiling an unmodified copy of `tests/dbg.c` by hand with the
  same flags (no leak) and comparing mtimes (`bin/dbg_asan` 03:05 vs `src/vt.c` 03:06). Fix: both
  sanitizer targets now list `src/vt.c src/config.c $(HDRS)`; verified with `touch src/vt.c && make asan`
  (both binaries recompile) and a clean `make asan` (no leak). Pitfall: a sanitizer target with an
  incomplete prerequisite list produces confident, wrong results.
- **`-d` did nothing when the config file set `log`** (session 43, found while reworking the CLI; README
  promises that command-line options take precedence). Pre-fix: with `log = 1` in the config,
  `./bin/k-term -d -e true` printed no debug lines at all. Root cause: the `-d` handler set
  `conf.loglevel` BEFORE `config_load()`, so the file's value overwrote it. Fix: `-d`/`--debug` sets a flag
  that is applied AFTER `config_load()`. Verified by session 43 with `.scratch/cli_test.sh` (17 cases):
  `-C <conf> -e true -d` logs `app_id: cli-test-id` and really applies `font_size = 30` (cell 18x34) while
  the config's `log = 1` no longer suppresses the debug lines, and the same config without `-d` produces 0
  log lines. (The config flag is `-c` since session 51; `-C` was its spelling at the time.)
- **`opacity` made the window fully transparent instead of partly** (session 41, found while making the
  documented transparency feature actually work). Reproduce: `./bin/k-term -d -o 0.5` -> the window is
  completely see-through; only `-o 1` looked right. Root cause: `render_init` clamped the value with
  `iclamp_(opacity, 0.0f, 1.0f)`, but `iclamp_` is a `static inline int` helper (k-term.h) — the float
  argument is truncated to int, so every opacity in (0,1) became 0. Fix: added `fclamp_()` (float clamp)
  and used it; the cell-background / selection / cursor alphas are scaled by `Render.opacity` in
  `render_draw` and carried to the shader in `a_bg.a`, and the three fragment-shader output branches now
  emit `alpha = max(ink, bg.a)` instead of forcing 1, so the background is see-through while glyph ink
  stays opaque. Verified by a `glReadPixels` alpha readback placed BEFORE `eglSwapBuffers` (after a swap
  the back buffer is undefined — an earlier placement read alpha 0 everywhere and proved nothing):
  `-o 0.5` -> `alpha[min=127 max=127]` on the empty frame, then `min=127 max=255` with 504 opaque ink px
  once text is drawn; `-o 1.0` -> `alpha[min=255 max=255]`, `translucent_px=0`. End-to-end compositor
  check (niri screenshot A/B, same window position): overlay `-o 0.3` shows the backdrop through it
  (pixels (45,38,52), (193,192,199)…), overlay `-o 1.0` reads exactly (16,16,20).
- **A multi-line paste executed line by line instead of being inserted** (session 40, user-reported).
  Reproduce: copy three lines (`echo "foo"` / `echo "bar"` / `echo "foobar"`) and `Ctrl`+`Shift`+`V` —
  each command ran as it arrived. Root cause: the paste pipe was streamed straight to the PTY, so the
  embedded CRs reached the shell as Return presses; K-Term never tracked DEC private mode 2004, which is
  how an application asks for pasted text to be distinguishable from typed text. Fix: `Term.bracketed_paste`
  set/cleared by `ESC[?2004h` / `ESC[?2004l` in `csi_dispatch`; the paste is buffered and written as
  `ESC[200~` + text + `ESC[201~` (in <=4096-byte pieces, so the tty input buffer cannot split the block
  and un-pair the delimiters); `paste_wrap()` in vt.c is the tested reference for the exact bytes. Verified
  with a throwaway PTY harness driving a real interactive bash: raw -> `foo`, `bar`, `foobar` executed
  immediately; bracketed -> nothing runs, readline shows the three lines, and one Return afterwards
  produces all three results together. `make test` 238/0 (mode on/off/RIS, `?2004;1000h` neighbours,
  `paste_wrap` byte-for-byte). Note: bash toggles the mode itself (`?2004l` while a command runs), so the
  CURRENT mode must be read at paste time — always bracketing would corrupt pastes into `less`/`vim`
  states that turned it off.
- **`term_init` freed an uninitialized grid** (session 40, latent since the RIS-leak fix). Reproduce:
  `Term t;` on a stack with dirty frames, then `term_init(&t, 24, 80, 1000)` -> `free(): invalid pointer`
  / `double free or corruption (out)` / segfault depending on `-O`. Root cause: `term_init` begins with
  `grid_destroy(&t->g)` (to free the buffers of the grid it is rebuilding) BEFORE `memset(t, 0, ...)`, so
  on a never-initialized `Term` it frees garbage pointers. The shipped code survived only because its
  `Term`s are `static` (zero-filled); ASan never saw it because the freed pointer is not a heap block.
  Fix: `Grid.valid` sentinel `GRID_VALID` ('grid'), set by `grid_init`, cleared by `grid_destroy`; both
  `term_init` and `grid_destroy` free only when it matches. A plain 0/1 flag is NOT enough — the stack
  garbage in that field was `32511`, which still took the free path. Verified: new test block declares a
  stack `Term`, dirties the dead frame and runs init/parse/re-init (RIS path)/destroy/init/destroy —
  `make test` 238/0, `make asan` 238/0, and the throwaway dirty-stack probe that used to abort now exits 0.
- **A config value containing `#` was silently truncated** (session 39, found while adding
  `working_dir`). Root cause: `config_load` cut the line at the FIRST `#` anywhere, so
  `working_dir = /home/me/my#project` lost everything from `#project` on (and the resulting path then
  failed the directory check). Fix: `#` starts a comment only at the start of the line or after a
  space/tab, so `value # note` is still stripped but `my#project` survives. Verified live: with
  `working_dir = /tmp/ktp/my#hash dir   # trailing comment`, the shell started in
  `/tmp/ktp/my#hash dir` (proved by `pwd` written to a file from inside the terminal).
- **Color emoji appeared as a black box (and a newly rasterized mono glyph appeared one frame late)**
  (session 37). Root cause: both atlases were uploaded at the TOP of `render_draw`, i.e. one frame
  BEFORE the glyphs they hold — the glyph lookups (`font_glyph` / `font_color_glyph`) run inside pass 3
  of the same function, so a glyph first shaped in frame N was sampled in frame N with an unbound /
  stale texture. Because the terminal only redraws when dirty, the frame that shapes an emoji is often
  the LAST frame, so the black box never got corrected. Fix: both `glTexImage2D` uploads run after the
  three emit passes and before the draw call. Verified with a temporary `glReadPixels` readback: the
  first frame containing an emoji already shows its color texels, `glerr=0x0`.

- **ASan heap-buffer-overflow in the atlas shelf packers** (session 37, found by a throwaway harness
  that shaped 1024 codepoints). Root cause: the fit check (`atlas_fits` / `catlas_fits`) compared the
  glyph against the CURRENT shelf only, so a glyph that wrapped to a NEW shelf was placed past the end
  of the atlas buffer (`catlas_place_bits`, same latent bug in the mono `atlas_grow`). Emoji bitmaps are
  ~136px wide, so a narrow atlas wraps shelves constantly and the bug was easy to hit. Fix: the fit check
  accounts for the wrap (`y + h + GLYPH_PAD <= atlas_h`), both grow functions loop until everything fits,
  and a glyph that cannot fit is invalidated (`w = h = 0` -> mono fallback) instead of keeping stale
  texcoords. The color atlas is 1024 wide with its height capped at `CATLAS_H_MAX` 8192 (32 MB, ~441
  distinct emoji on this box) so an emoji-heavy `cat` cannot exhaust memory. Verified: the stress harness
  is ASan/UBSan clean (ok=441 null=583, catlas 1024x8192) and `make asan` is clean.
- **Country-flag emoji rendered as two separate letter glyphs over four cells** (session 38).
  Root cause: a flag is a PAIR of regional indicators (U+1F1FA U+1F1F8 = US) and Unicode makes the pair
  ONE grapheme cluster, but `put_cluster_join` only folded ZWJ / variation selectors / skin tones /
  keycaps / tag chars, so each RI became its own anchor cell (each wide -> 4 cells) and HarfBuzz shaped
  each RI ALONE: Noto Color Emoji does have single-RI glyphs, but they are the LETTERS (U+1F1FA ->
  gid 225), while the actual flag is keyed on the pair (gid 1772, 136x128, color). Fix: `join_ri` in
  `put_cluster_join` folds a regional indicator into an RI anchor, gated on `a->nextra == 0` so only the
  second RI joins and a third RI starts a new cluster (Unicode pairs them up).
  Verified: `make test` 206/0 (new checks: anchor = first RI, nextra=1 = second RI, one ATTR_WIDE
  continuation, exactly 2 cells, third RI gets its own cell) and a live `glReadPixels` A/B — pre-fix
  7 color quads with the emoji run spanning 11 cells, fixed 6 quads spanning 12 cells (flag = 1 quad).

- **Emoji whose wide presentation comes from a selector were squished into one cell** (session 38).
  Root cause: the 2-cell reservation used `cp_is_wide(base)` only, i.e. the width of the BASE codepoint.
  U+2764 (heart) and U+0031 (keycap base) are text-presentation, so they are narrow; the emoji
  presentation is requested by a modifier that arrives LATER (U+FE0F, or U+20E3 for keycaps), by which
  time the cell had already been reserved as 1 cell. HarfBuzz still shaped the cluster correctly
  (heart -> gid 168, keycap -> gid 1485, both color) but the renderer shrank the 136x128 bitmap into a
  single 8px cell. Fix: new `cluster_is_wide(seq, n)` (base wide OR the cluster holds U+FE0F on an
  emoji-capable base OR U+20E3 on a digit/`#`/`*`) is the single width predicate; `put_cluster_join`
  UPGRADES a 1-cell anchor to a 2-cell pair when the modifier it just folded makes the cluster wide
  (writes the ATTR_WIDE continuation cell, steps the cursor over it, sets `cl_w = 2`), guarded on the
  cursor still sitting directly after the anchor and the next column existing (at the right margin the
  emoji keeps one cell); `render_draw` sizes the color quad with the same predicate so a quad can never
  exceed its reservation.
  Verified: `make test` 206/0 (heart+VS16 widens in place, `a`+VS16 does NOT widen, keycap widens,
  right-margin guard, selection still copies the full 7-byte cluster) and the same live readback A/B
  (heart / keycap / rainbow flag each went from 1 colorful cell to 2).

- **README.md and docs/ARCHITECTURE.md did not document the color-emoji feature** (session 38,
  documentation only). AGENT.md (session 37) claimed those sections existed; neither file contained the
  string "emoji", README's dependency list omitted `harfbuzz` (which the Makefile has required since
  session 37, so a fresh clone could not be built from the README), and ARCHITECTURE still said "One
  texture" and "155 checks". Fix: README gained the color-emoji feature bullet, the `harfbuzz`
  dependency and the emoji limits; ARCHITECTURE gained the "Color emoji & grapheme clusters" section
  plus corrected module/data-flow/rendering text. No code touched.
- **Selection did not start at the clicked glyph / jumped in large blocky increments** (session 31,
  user report; user visually confirmed the fix working). This was the real root cause of the long-running
  selection complaint, distinct from the grid-anchoring entry below.
  Root cause: `wl_fixed_t` is `typedef int32_t` — a fixed-point number (`wl_fixed_to_int(f)=f/256`).
  `wl_pointer.enter`/`motion` deliver `surface_x`/`surface_y` as `wl_fixed_t`, but `ptr_enter`/`ptr_motion`
  declared them `int32_t` and stored them RAW into `W.ptr_x`/`W.ptr_y` (compiles warning-free because
  `wl_fixed_t == int32_t`), so the stored value was ~256x the true pixel coordinate. `sel_cell`/`mouse_cell`
  (`col = ptr_x / cell_w`) then clamped to the last column/row for any pointer more than a few px from the
  top-left -> selection snapped to the bottom-right corner and each motion jumped by thousands of cells
  (clamped); the sub-cell frac saturated to 1000 (blocky). The same bug mis-aimed SGR mouse reporting.
  Fix: `ptr_enter`/`ptr_motion` now take `wl_fixed_t` params and store `wl_fixed_to_int(x/y)` (src/main.c).
  Verified numerically with a temporary standalone harness (since removed) replicating `sel_cell`'s math on
  the live geometry (cell 8x16, grid 156x62): OLD raw-int mapped ptr=(2,1)->col 64 and every real position
  clamped to the corner; NEW converted mapped (100.5,300)->(12,18) frac500, (400,500)->(50,31),
  (2,1)->(0,0) frac250, (1200,900)->(150,56) — each maps to the correct cell and tracks the pointer.
  Re-verified after the edit: `make` clean (zero warnings), `make test` 155/0 + MODEL OK, `make asan` 155/0
  + MODEL OK, live exit 0. **Pitfall:** any Wayland coordinate arriving as `wl_fixed_t` must be converted
  with `wl_fixed_to_int`/`wl_fixed_to_double` before use — the raw int is 256x too large.

- **Selection did not match what the pointer hovered** (session 30, user report: "the mouse pointer
  selection doesn't accurately reflect what is being selected").
  Root cause: two different origins for the grid. `render_draw` placed viewport row y at
  `py = (rows-1-y)*ch` (BOTTOM-LEFT anchored) while `sel_cell()`/`mouse_cell()` computed
  `row = ptr_y / font.cell_h`, which is only correct for a TOP-LEFT anchored grid. `rows` is
  `cur_h / cell_h` (floor), so after a compositor configure the grid leaves `cur_h - rows*cell_h`
  unused pixels — and bottom-left anchoring put them at the TOP of the window. Measured live: window
  1253x1002 px, cell 8x16, grid 156x62 -> 10 unused px. Visual row r then occupied window y
  [10+16r, 10+16(r+1)) while the code named `floor(y/16)`, so for 10 of every 16 px (62% of the row
  height) the selection — and SGR mouse reporting for vim/htop/etc. — named the row BELOW the pointer.
  Fix: `render_draw` computes `pad = vh - rows*ch` (clamped >= 0) once per frame and draws
  `py = (rows-1-y)*ch + pad`, i.e. the grid is TOP-LEFT aligned and the leftover pixels belong at the
  bottom/right (a pointer there clamps to the last row/column). Horizontal mapping was already exact.
  Verified with a temporary `glReadPixels` per-cell scan (since removed): after the fix grid row 0 reads
  back at window y 0..15 and row 1 at 16..31 with the band cut mid-glyph at both horizontal edges
  (`.-.-sISISISISIS-…` / `S-S-S-S-S-S-s-…`); with `pad` forced back to 0 the same scan shows every cell
  only partially covered and the ink landing in the next row's window (`.-.-sIsIsIsIsIs-s-…` /
  `s-s-sISISISIsIs-…`), i.e. the bug reproduced and the fix proven against it.
  **Pitfall:** any future change to the grid's pixel origin must keep viewport row y at window pixels
  `[y*cell_h, (y+1)*cell_h)` or `sel_cell`/`mouse_cell` break again.

- **Copied text contained a glyph that was never highlighted** (session 30). `render_draw` skipped the
  selection band on the cursor cell (`band_on = hl_on && !is_cursor`) while `term_selection_text()`
  copied it, so Ctrl+Shift+C produced text the user had not seen selected. Fix: the band is drawn for
  every covered cell; a selected cursor cell keeps the band and its glyph is tinted `CURSOR_COL` (light
  ink on the selection colour — the cursor stays locatable), an unselected cursor cell is unchanged
  (solid `CURSOR_COL` block with inverted ink). Verified with the same readback scan: the injected cursor
  cell inside the span reads back fully selected (`SI`).

- **Selection smoothness / precision, sub-cell edges** (sessions 24-27). Three separate rendering bugs
  made the highlight ignore the pointer: (a) the band origin was double-counted (`px + hl0` where `hl0`
  was already absolute) -> only the left-most column highlighted; (b) with no GL blending the glyph quad
  repainted the normal background over the band -> selection invisible / "glues whole rows"; (c) a glyph
  quad carries ONE `v_bg`, so a partially covered cell snapped to whole-cell selection. Fixes: band
  geometry moved into `vt.c` as testable helpers (`sel_span_cover`, `sel_band_pixels`); the per-instance
  record gained `band0/band1` (the band's absolute pixel edges) and the fragment shader picks the
  background behind a glyph per-fragment (`bg = (v_glyph>0.5 && band0<=v_px<band1) ? u_sel : v_bg`),
  giving true sub-glyph partial fill WITHOUT GL blending (transparency untouched). Verified with the
  temporary `glReadPixels` readback (span 2.3..6.7 cut MID-GLYPH at both edges).

- **Scrollback direction reversed + new output truncated once the screen fills** (session 19).
  One root cause: `scroll_off` is an absolute index into `row[]` where `row[0]` is the OLDEST retained
  line, so the live view sits at `scroll_off == scrollback_max` once scrollback fills — but every
  consumer assumed `scroll_off == 0` meant live. `snap_to_live()` therefore jumped to the oldest lines
  (it runs on every key press and every PTY read), and `scrollback_nav()` moved the window toward newer
  content for `+delta`. Fix: `Grid.live_off` is the anchor (`0 <= scroll_off <= live_off <=
  scrollback_max`, `live_off + rows <= rcap`); `scroll_up`/`scroll_down` advance it with `scroll_off`
  (snapping `scroll_off` back to `live_off` first for whole-viewport scrolls so slots holding the newest
  lines are never blanked); `grid_resize` snaps to the anchor before its row math; `term_resize`
  re-anchors `cy` with `live_off` (`cy` is a live-view coordinate — the old clamp stranded the cursor
  mid-screen after a grow); `snap_to_live()` restores `live_off`; the PTY read path snaps when
  `scroll_off != live_off`; render.c draws the cursor only at the anchor. Wheel sign was already correct.
  Verified: test_vt 124/0 (19 new anchor checks), dbg asserts the invariants on every stage, asan clean,
  headless pipeline harness 20/0, live glReadPixels showed the newest line after `seq 1 300`.

- **Mouse wheel did not scroll at all** (session 18). `ptr_axis` sent Up/Down **arrow keys** to the
  shell (shell history, not scrollback), and the seat was bound at v1 so `wl_pointer` had no
  `axis_discrete`/`axis_source`/`frame`. Fix: bind `wl_seat` at `imin_(version,5)` (runtime-verified:
  logs "wl_pointer interface version 5"); add the axis_source/axis_stop/axis_discrete handlers (a NULL
  slot aborts libwayland; note `axis_discrete` has **no** `time` argument in the installed protocol);
  accumulate per frame, convert a frame to lines (3 lines/notch preferred, else axis px / cell_h) through
  `scroll_apply`, which carries fractional remainders for smooth high-res scrolling and resets the
  accumulator at a clamp so reversing is immediate. Apps that grab the mouse still get wheel-as-buttons
  4/5. New PTY output snaps to the live bottom first so it is not written into the rows being viewed.

- **No text selection or clipboard** (sessions 16e/17). Fix: viewport-relative selection rectangle in
  `Term` with `term_selected()` / `term_selection_text()` / `term_select_clear()`; render.c highlights by
  swapping fg/bg; main.c drives it with a left-button drag when no app has grabbed the mouse and clears
  it on nav/resize; Wayland data-device clipboard (copy = offer the selection as `text/plain` via
  `set_selection`; paste = receive the compositor-pushed offer into a pipe drained to the PTY);
  Ctrl+Shift+C / Ctrl+Shift+V. Protocol gotcha: the installed `wl_data_*` API has **no**
  `wl_data_device_request_selection`, and `wl_data_source_listener.send` takes
  `(data, source, const char *mime, int32_t fd)`. Limitation: selection is viewport-relative (cleared on
  scrollback navigation/resize).

- **Zoom not implemented** (session 16d). `Font.scale` was only ever 1.0 and the atlas was baked at a
  fixed pixel size. Fix: `font_resize()` re-rasterizes (FT_Set_Pixel_Sizes + recomputed metrics + glyph
  table/atlas rebuild + `atlas_dirty`); `zoom_apply()` clamps 6..256, recomputes cols/rows for the
  unchanged window pixels, and re-fills blank cells with the configured colours. Verified display-free:
  14px -> 28px doubles cell/glyph metrics, 28px -> 14px restores them exactly (crisp, not stretched).

- **Scrollback had no UI navigation** (session 16c). Fix: `scrollback_nav()` + Shift+PageUp/Down
  (one page) and Shift+Home/End; any other key snaps back to live. (Bounds later corrected by the
  session 19 `live_off` fix.)

- **Mouse reporting documented but non-functional** (session 16b). README claimed SGR reporting but
  `term.mouse_mode` was never set (the CSI private branch only handled `?25`), and `ptr_button` reported
  the text cursor instead of the pointer cell. Fix: `mouse_mode` bitmask set by ?1000/?1002/?1006;
  pointer coords + held button tracked; SGR (`\e[<b;x;yM/m`) or legacy X10 emitted at the pointer's cell;
  drag reports on ?1002.

- **No alternate screen buffer** (session 16). Full-screen apps (vim/htop/less/nano) drew into the main
  grid, polluting scrollback, and their output was not discarded on exit. Fix: `Term.alt_screen` +
  parked `alt` grid + `alt_saved_cx/cy`; `?1049` saves/restores the cursor, `?47`/`?1047` switch without
  saving, `?1048` is cursor-only; `term_resize` resizes the parked main grid too; `term_init`/
  `term_destroy` free both grids (this also fixed a pre-existing leak on RIS / `ESC c`).

- **Hardware instancing was never actually enabled** (sessions 14-15). The instanced path was gated on
  `GL_ARB_vertex_instanced`, a non-existent extension, so it never activated; the draw code was also
  incomplete (divisor on one attribute, `glDrawElementsInstancedARB` with no element buffer). Fix: static
  unit-quad corner VBO (`a_pos`, divisor 0) + per-instance records (divisor 1; 17 floats at the time of
  this fix, now 19 after the session-27 selection-band `band0`/`band1` attrs) +
  `glDrawArraysInstancedARB`, fallback expands to per-vertex data; enabled only when both real extensions
  are in `GL_EXTENSIONS`. Verified by a glReadPixels A/B: identical ink counts (128/440) and identical
  ASCII art.

- **Black window / no glyphs** (sessions 11-12). Font ascent/descent were in font units (1705/615), not
  pixels, so glyph quads landed ~700 px below the window. Fix: use `face->size->metrics` (26.6 fixed /64),
  corrected baseline formula, `glClear` per frame.

- **10 s startup delay** (session 12). fish sends DA1/CPR/DA2 queries and blocks waiting for a reply.
  Fix: `csi_dispatch` answers `ESC[6n`, `ESC[c` / `ESC[?c`, `ESC[>c`; `>` accepted as a CSI parameter
  byte; main.c drains the response buffer to the PTY. `-e` run now exits in ~0.7 s.

- **Colored-rectangle garbage** (session 10). `row_resize` realloc-grew columns but only fixed `bg`,
  leaving `ch`/`attr`/`fg` as heap garbage. Fix: clear grown columns; main.c applies the configured
  fg+bg after resize.

- **Garbled glyph rectangles** (session 9). Atlas uploaded with `GL_R8`/`GL_RED`, invalid in a GL 2.1
  context (`GL_INVALID_ENUM`, texture never populated). Fix: `GL_LUMINANCE` + `glGetError` check.

- **Triangular grid pattern** (session 8). `emit_quad` pushed 4 vertices but the draw used
  `GL_TRIANGLES`. Fix: emit 6 (two triangles), draw `nquads*6`.

- **Startup draw deadlock** (session 8). The first draw waited on a frame callback that can never fire
  before a buffer is attached. Fix: the first configure calls `redraw()` directly.

- **Fan of colored lines** (session 7). `FLOATS_PER_VERT` was 11 while `push_vert` writes 13. Fix: 13.

- **Geometry off the top** (session 7). MVP translation mapped gl.y into [1,3]. Fix: `mvp[13] = -1`.

- **Event-loop starvation** (session 7). `wl_display_read_events()` in a drain loop blocked on the
  display fd and starved the timerfd/signalfd (no redraw, child exit never detected). Fix: canonical
  prepare_read -> epoll_wait -> read_events.

- **libwayland abort on pointer motion** (session 6). `wl_pointer.motion` was NULL. Fix: no-op handler.

- **`render_resize` EGL_BAD_MATCH** (session 6). It destroyed and recreated the EGL surface on every
  configure. Fix: only update vw/vh (`wl_egl_window_resize` already resizes the surface).

- **`render_init` config-fallback array bounds** (session 6). `sizeof` in bytes used where an element
  count was meant (out-of-bounds copy). Fix: build the fallback by element count.

- **`render_init` EGL init/surface failures** (sessions 5-6). Deprecated `eglGetDisplay(wl_display)`
  (0x3001) and `wl_display` passed as the EGL native window (0x300b). Fix:
  `eglGetPlatformDisplayEXT(EGL_PLATFORM_WAYLAND_EXT)` + pass the `wl_egl_window` (stored in `Render.nwin`).

- **Swap before the first configure ack** (session 5). Protocol error + SIGSEGV. Fix: no initial swap;
  the first swap happens in `render_draw` after the ack.

- **`font_init` overwrote `Font.ft` (the library) with the `FT_Face`** (session 5). SIGSEGV in
  `FT_Done_FreeType`. Fix: separate `Font.ftlib`.

- **Makefile link/define gaps** (sessions 2, 5). Missing `wayland-egl` (undefined `wl_egl_window_*`) and
  missing `-D_GNU_SOURCE` (implicit `openpt`/`grantpt`/`ptsname`). Both fixed.

- **main.c API misuse** (sessions 5-7). `wl_display_poll_out` does not exist (use `poll(POLLOUT)` on
  `wl_display_get_fd`); listener signatures mismatched (`wl_pointer.leave` serial, `wl_keyboard.enter`
  keys, `wl_keyboard.leave` surface); `xkb_keymap_key_get_*` does not exist (use
  `xkb_state_key_get_one_sym` + `xkb_state_key_get_utf8` on the keycode); `seat_listener` /
  `wm_base_listener` were defined but never registered. `wl_compositor` is now bound at
  `min(version,4)` with `wl_surface_set_buffer_scale` guarded on `comp_ver >= 3`.

- **font.c bearing fields** (session 2). Read `bitmap.left/top`, which do not exist. Fix: use
  `slot->bitmap_left` / `bitmap_top`.

- **VT grid model** (sessions 3-4). Hand-rolled `memmove` shifts duplicated row pointers and
  double-freed buffers. Fix: rotation-based permutation model (`rot_left`/`rot_right`); whole-viewport
  scroll slides `scroll_off`. Verified by dbg "MODEL OK".

- **CSI parser** (session 3). `npar` was treated as a count instead of an index, so single-parameter CSIs
  mis-parsed, the last parameter was skipped, and stale parameter slots leaked between CSIs. Fix: reset
  and normalize all 32 parameter slots; defaults applied per VT spec; SGR `38;5;idx` / `48;5;idx` support.

- **Compiler warning: unused parameter `t` in `toplevel_configure`** (present since session 5, silenced
  session 20). `make` is now warning-free under `-Wall -Wextra`.
