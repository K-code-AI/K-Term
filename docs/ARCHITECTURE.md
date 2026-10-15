# K-Term — Architecture

Module map and invariants. Kept for debugging and future development; build/usage
details live in README.md, work history in AGENT.md, bugs in ISSUES.md.

## Data flow

```
shell (PTY child)
   |  bytes on pty.mfd
   v
term_parse()            src/vt.c     UTF-8 + ESC/CSI/OSC state machine
   |                                   writes Cells into Term.g (Grid); folds emoji
   |                                   grapheme clusters (ZWJ/VS16/skin tone/keycap/RI pair)
   |                                   into the anchor Cell and reserves 2 cells for wide ones
   v
Grid (rows x cols viewport + scrollback)
   |  render_draw(r, &term, &font)
   v
Font atlas (alpha8) + color-emoji atlas (RGBA8) + per-instance quad records   src/font.c, src/render.c
   |  HarfBuzz shapes a cluster to ONE glyph id; FreeType rasterizes its color bitmap
   |  one glDrawArraysInstancedARB (or expanded glDrawArrays)
   v
EGL surface -> wl_surface -> Wayland compositor      src/main.c (xdg-shell)

keyboard / pointer (Wayland) -> kb_key / ptr_* -> pty_write()  (or selection/scrollback handling)
```

## Modules

| File | Responsibility |
|---|---|
| `include/k-term/k-term.h` | All shared types (`Cell`, `Grid`, `Term`, `Parser`, `Config`, `Pty`, `Font`/`Glyph`, `Render`), prototypes, log levels, clamp/utf8 helpers. Single header; no private headers. |
| `src/vt.c` | VT/ANSI/UTF-8 parser + grid model + selection text extraction + alternate-screen switching. No Wayland/GL dependencies — this is the unit-tested core. |
| `src/pty.c` | `posix_openpt`/`grantpt`/`unlockpt`; child does `setsid` + `TIOCSCTTY` + `chdir(working dir)` + exec `$SHELL` (or `-e COMMAND`); sets `TERM`/`COLORTERM`/`TERM_PROGRAM`; `SIGWINCH`-free resize via `pty_setsize`. |
| `src/font.c` | fontconfig pattern -> FreeType face; alpha8 glyph atlas (shelf packer + open-addressing hash by codepoint); fontconfig fallback faces for codepoints the primary font lacks; color emoji: fontconfig `FC_COLOR` face + HarfBuzz shaping of a whole cluster into one glyph id + RGBA8 color atlas (`font_color_glyph`); `font_resize()` re-rasterizes at a new pixel size (zoom). |
| `src/render.c` | EGL (platform Wayland) + GL 2.x. Builds one shader + one VAO; per frame: clear, emit the per-cell quads in three back-to-front passes, upload the two atlases when dirty, one draw call. |
| `src/config.c` | `key = value` config file + defaults + clamping. |
| `src/main.c` | Wayland shell (xdg-shell), registry binding, xkb keyboard, client-side key repeat (`send_key` + `repeat_start`/`repeat_fire`/`repeat_stop`), pointer (selection, mouse reporting, scrollback wheel), selection endpoint snapping (`sel_cell` + `sel_set_endpoints`), frame-paced redraw, epoll event loop, CLI flags, clipboard data-device. |
| `tests/test_vt.c` | Unit tests for the parser/grid/selection (incl. sub-cell band geometry and the per-glyph endpoint rule)/alt-screen/emoji-cluster/CSI 3 J/bracketed-paste/reflow/viewport-height-resize model, plus the config-file keys (364 checks). |
| `tests/dbg.c` | Grid-model invariant checker: asserts the `row[]` permutation + `scroll_off`/`live_off` bounds after scripted scroll, resize, reflow and zoom stages (incl. a reflow that overflows a tiny buffer). |
| `tests/cli_test.sh` | Drives the REAL `bin/k-term` to cover `main()`'s argument parser, which no linked test binary can reach (77 checks; run by `make test-cli`, which `make test` depends on). Skips the window-opening checks when `WAYLAND_DISPLAY` is unset. |
| `build/generated/` | `xdg-shell-client-protocol.h` + `xdg-shell-protocol.c`, produced by `wayland-scanner` from the system `wayland-protocols` XML. Git-ignored. |

## Event loop (src/main.c)

Single `epoll` loop over six descriptors:

1. `wl_display_get_fd()` — Wayland protocol traffic.
2. `pty.mfd` — shell output.
3. `timerfd` — 8 ms batching window (`FRAME_INTERVAL_NS`); coalesces bursts of output into one redraw.
4. `signalfd(SIGCHLD)` — detects shell exit (blocked SIGCHLD so only signalfd reads it).
5. paste idle timerfd (`pwfd`) — one-shot, armed only while a clipboard paste is buffered; flushes it if
   the clipboard source never closes the pipe.
6. key-repeat timerfd (`krfd`) — armed only while a key is held (see "Key repeat" below).

A seventh descriptor appears transiently: the read end of the clipboard paste pipe, added by
`start_paste()` and removed as soon as the paste is complete.

The loop uses the canonical pattern `wl_display_dispatch_pending()` ->
`wl_display_prepare_read()` -> `epoll_wait()` -> `wl_display_read_events()`.
Calling `read_events()` inside a drain loop blocks on the display fd and starves the
timerfd/signalfd (past bug: no redraw, child exit never detected).

**The wayland read is finished before ANY other descriptor is handled** (read_events, or cancel_read when
epoll did not wake for the display fd, then dispatch_pending). That is not cosmetic: `eglSwapBuffers`
waits for a `wl_buffer.release`, and while a `wl_display_prepare_read()` is outstanding that release can
never be read — the thread that would read it is parked inside the driver. The swap then blocks forever
and the terminal looks dead (black window, no keys, no output; ISSUES.md "Fixed").

### Frame pacing (why the draw path looks the way it does)

`W.frame_cb` holds the one outstanding `wl_surface_frame` callback (NULL = none). Three rules, each of
them the result of a real freeze (ISSUES.md "Fixed: Fullscreen" and "Fixed: Black window"):

1. **A frame callback is armed only together with a commit that attaches a NEW buffer**, i.e. only from
   `redraw()` after a successful `eglSwapBuffers`. A damage-driven compositor runs a render pass only
   when something on the output changed, and frame callbacks are delivered during such a pass, so a
   callback armed on an EMPTY commit can stay pending forever — and every draw path is gated on "no
   callback in flight", so the display freezes. `redraw()` therefore checks `render_draw()`'s return
   value: a failed swap keeps `W.dirty` set for the 8 ms timer to retry and arms nothing.
2. **`eglSwapBuffers` is not guaranteed to return.** It waits until the driver gives a `wl_buffer` back,
   and only the compositor releases buffers (measured on this NVIDIA driver: it blocks in a futex, and
   `eglSwapInterval(0)` does not change that). A blocked swap stops the whole event loop — no keys, no PTY
   reads, no redraw. The constraint is about the wayland READ, not about listeners: a swap must never run
   while a `wl_display_prepare_read()` is outstanding, because the `wl_buffer.release` it waits for could
   only be read by the thread that is parked inside the driver (that is exactly what the session-46 tree
   did from the timerfd branch — ISSUES.md "Fixed: Black window, no prompt, no input"). The loop finishes
   the read (`wl_display_read_events()`, or `wl_display_cancel_read()`) and `wl_display_dispatch_pending()`s
   before it handles any other descriptor, so swapping inside a listener is fine as well; `frame_done()` is
   the preferred moment because the compositor has just shown the previous buffer.
3. **The first buffer must be attached by the FIRST `xdg_surface_configure` handler** (`!W.swapped_once`):
   a window is not mapped until a commit carries a buffer, so a client that defers its first draw to the
   8 ms timer never maps and therefore never receives a frame callback either. Nothing is in flight then,
   so that swap cannot wait for a release. Every later configure is answered by the frame path.

A configure that changes the pixel size sets `W.resized` (cleared by `redraw()` once a buffer of that
size is attached). Both the timerfd branch and `frame_done()` draw when `dirty || resized`, because a
size change is frequently the ONLY change — the compositor is waiting for a buffer of the new size, and
waiting for `dirty` alone is what left the screen frozen in fullscreen. Under `-d` the answer is logged:
`configure answered: buffer 936x1002 attached (grid 117x62)`.

## Wayland protocol & input invariants (src/main.c)

* **Pointer coordinates are fixed-point.** `wl_fixed_t` is `typedef int32_t` encoding px*256.
  `wl_pointer.enter`/`motion` deliver `surface_x`/`surface_y` as `wl_fixed_t`; `ptr_enter`/`ptr_motion`
  convert with `wl_fixed_to_int` before storing into `W.ptr_x`/`W.ptr_y`. Storing the raw int (which
  compiles warning-free because `wl_fixed_t == int32_t`) is 256x too large and breaks the pointer->cell
  mapping and SGR mouse reporting (past bug, session 31; see ISSUES.md "Fixed").
* **Key repeat is the client's job.** A Wayland compositor never synthesizes repeat events: `wl_keyboard.key`
  delivers exactly one press and one release per physical keypress, and `wl_keyboard.repeat_info` only
  reports the user's system-wide setting (niri here: rate 25/s, delay 600 ms). K-Term therefore generates
  the repeats itself: `kb_key` handles a press through `send_key()`, which RETURNS whether that press is
  worth repeating — 1 for keystrokes delivered to the PTY and for Shift+PageUp/Down/Home/End scrollback
  navigation, 0 for the terminal-local one-shots (zoom, `Ctrl`+`Shift`+C/V, which would otherwise fly past
  a font size or paste repeatedly). A repeatable press arms the `krfd` timer for `repeat_delay` ms
  (one-shot); when it fires, `repeat_fire()` re-sends the key and re-arms at `1/repeat_rate` s; releasing
  the tracked key, losing keyboard focus (`kb_leave`) or replacing the keymap (`kb_keymap`) calls
  `repeat_stop()`, which also DRAINS an expiry that already fired so a release racing the timer cannot
  deliver one extra keystroke. The timing comes from the config file rather than from `repeat_info` so the
  feel is identical on every compositor and the settings are unit-testable; `repeat_delay = 0` disables
  K-Term's repeat entirely. Two details that are easy to get wrong: `it_interval.tv_nsec` must stay < 1e9
  (`repeat_rate = 1` would otherwise hand `timerfd_settime` an invalid 1000000000 ns), and the key is
  re-TRANSLATED on every repeat (`send_key` reads the current xkb state), so pressing Shift while holding
  `a` starts producing `A`. Only the newest repeatable press repeats and only one key is tracked.
* **Seat bound at v5** (`imin_(version,5)`) to get `axis_discrete`/`frame` for wheel scrolling;
  `wl_keyboard` has no events above v4, so this adds no unhandled keyboard events. Every listener slot
  the bound version can dispatch must be non-NULL or libwayland aborts; in the installed protocol
  `axis_discrete` has **no** `time` argument (unlike `axis_stop`).
* **Clipboard is push-based, and it is the SYSTEM clipboard.** The installed `wl_data_*` API has no
  `wl_data_device_request_selection`; the compositor *pushes* the selection offer. Copy =
  `wl_data_device.set_selection` (so the text K-Term offers is handed to every other client by the
  compositor); paste = receive the offered `text/plain` into a pipe whose read end joins the epoll loop.
  `wl_data_source_listener.send` takes `(data, source, const char *mime, int32_t fd)`.
  Verified end to end against a FOREIGN client, not against K-Term itself — an internal test cannot tell a
  shared clipboard from a private one, which is why every older paste test here proved less than it seemed:
  `wl-copy 'TEXT'` + `Ctrl`+`Shift`+`V` in K-Term puts exactly those bytes in the PTY, and the user
  confirmed both directions in the real desktop (2026-10-15). Two protocol details a future change must not
  break or "fix" blindly: K-Term offers only `text/plain` (niri also advertises `UTF8_STRING`,
  `text/plain;charset=utf-8`, `TEXT`, `STRING` for other clients and serves a `text/plain` request from
  them fine), and `wl_data_offer_accept()` is never called before `wl_data_offer_receive()` — niri
  tolerates that, a stricter compositor might not (ISSUES.md "Known limitations").
* **Paste is one bracketed block, not a stream.** The clipboard text is accumulated in a growable buffer
  and written to the PTY only when the source closes the pipe, because the bracketed-paste delimiters
  have to surround the WHOLE block. When the application has DEC mode 2004 on (`Term.bracketed_paste`,
  set by `ESC[?2004h`, cleared by `ESC[?2004l`) the bytes sent are `ESC[200~` + text + `ESC[201~`; bash,
  zsh and vim then insert the text literally, so a multi-line paste does not execute line by line. The
  text is written in <=4096-byte pieces: one big `write()` to the master is capped by the tty input
  buffer, and a block split across that cap would separate the paired delimiters. `paste_wrap()` (vt.c)
  is the tested reference for the exact bytes; `PASTE_START`/`PASTE_END` are the shared macros both it
  and `paste_flush()` use. A paste over `PASTE_BUF_MAX` (8 MiB) is sent unwrapped with a warning.

## Grid model invariants (src/vt.c)

`Grid.row` is a fixed-capacity array of `rcap = rows + scrollback_max` row buffers.
It is always a **permutation** of the allocated buffers — scrolling is expressed as
rotations (`rot_left`/`rot_right`), never as `memmove` of pointers, so no slot is ever
NULLed, duplicated, or double-freed. `tests/dbg.c` asserts this on every stage.

Viewport addressing: `grid_vprow(g, y)` returns `row[scroll_off + y]`.

`Grid.valid == GRID_VALID` between `grid_init` and `grid_destroy`. `term_init` frees the grid it is
rebuilding (that is what makes `ESC c` leak-free), so it must distinguish "an old grid exists" from
"this `Term` was never initialized"; a plain non-zero flag is NOT sufficient because uninitialized stack
memory is routinely non-zero (it was: `valid=32511` next to a stack-half `row` pointer). The sentinel is
what makes `term_init` safe on any memory.

```
row[0 .. scroll_off-1]              older retained lines (visible when scrolled back)
row[scroll_off .. scroll_off+rows-1] viewport
row[scroll_off+rows .. rcap-1]      free, OR the newest lines while scrolled back
```

Anchor invariant: `0 <= scroll_off <= live_off <= sb_limit = min(scrollback_max, rcap - rows)` and
`live_off + rows <= rcap`. `sb_limit` matters: `rcap` never changes, so a window that becomes taller than
the initial row count eats into the scrollback, and `scrollback_max` alone is then NOT a safe bound
(using it let `scroll_up` blank `row[rcap]` — a segfault after ~2000 lines in a grown window).

* `live_off` is the `scroll_off` that shows the newest output. It advances with every
  whole-viewport scroll; user navigation moves `scroll_off` **below** it.
* `snap_to_live()` (main.c) restores `scroll_off = live_off`; the PTY read path does
  this before parsing so new output lands at the live bottom instead of overwriting the
  rows being viewed.
* `cy` is a **live-view** coordinate: the parser always addresses `row[live_off + cy]`.
  `term_resize` therefore re-anchors `cy` with `live_off`, not `scroll_off`.
* The cursor is drawn only when `scroll_off == live_off` (src/render.c).
* Whole-viewport scrolls snap `scroll_off` back to `live_off` first, otherwise the slide
  branch would blank a slot that still holds the newest content.

Alternate screen: `Term.g` stays the ACTIVE grid (so every `t->g` reference keeps working) and the main
grid is parked in `Term.alt`; the alt grid has scrollback 0. `?1049` saves/restores the cursor,
`?47`/`?1047` switch without saving, `?1048` is cursor-only; `term_resize` resizes the parked main grid
too.

CSI 3 J ("erase saved lines", terminfo `E3`, sent by the system `clear` because K-Term sets
`TERM=xterm-256color`): `erase_scrollback()` drops the retained history while leaving the viewport
untouched. It moves the viewport rows to slots `0..rows-1` (through a scratch pointer array whose count
is validated before anything is committed, so a logic bug can never corrupt the grid), blanks every
other row buffer, and sets `scroll_off = live_off = 0` — which also snaps a scrolled-back view back to
the live bottom. The permutation invariant above is what makes this safe; `tests/dbg.c` has `ed3*`
stages that assert it. The behaviour is gated by a process-wide setting (`term_set_clear_scrollback`,
default on, from `clear_scrollback` in the config): it is deliberately NOT a `Term` field because
`ESC c`/RIS calls `term_init`, which memsets the whole `Term` and would silently re-enable a setting the
user had turned off.

Reflow (a width change): `Grid.wrap[i] != 0` means the line in `row[i]` was broken by the RIGHT MARGIN
and continues in `row[i+1]`; a hard break (CR/LF, an app cursor move, an erase that removed the wrapped
part) leaves the mark 0. The mark is a byte array parallel to `row[]` because the grid model above forbids
restructuring the slots — it rotates with `row[]` in `rot_left`/`rot_right` and is cleared by
`grid_blank_slot()` and by `erase_line`-style erases, so a blank slot never claims to continue.
`grid_reflow()` then: (1) computes `ulen[i] = row_used(row[i], cols)` — the leading cells that are not
`cell_pristine()` (ch, nextra, attr AND both colours default) — so joining rows drops never-written blanks
and the padding a wide glyph leaves at a row end instead of turning them into literal spaces, and never
trims past the tracked cursor cell; (2) joins the slots of each logical line and re-breaks them into
chunks of the new width, pulling a chunk boundary back when the cell that would START the next chunk
carries `ATTR_WIDE` (a wide glyph moves whole to the next line; the `x > 0` guard keeps progress possible
at `new_cols == 1`); (3) builds the output oldest-first in a fixed `2*rcap+2` array, freeing the oldest
produced row as soon as `rcap` exist (narrowing evicts from the top, like scrollback overflow) and
compacting the window, so memory stays bounded even when one line becomes fifty rows; (4) commits by
freeing every old buffer and installing the new ones, which keeps the permutation invariant, and sets
`live_off = scroll_off = clamp(kept - rows, 0, min(scrollback_max, rcap - rows))`. The cursor is passed as an absolute
`row[]` slot + column and comes back as the same logical cell (or the newest surviving line if its line
was evicted). `grid_resize()` snaps `scroll_off = live_off` first because reflow needs free slots below
the viewport; `grid_resize_plain()` is the no-reflow variant used for the ALTERNATE screen (a full-screen
app repaints itself after SIGWINCH, so re-wrapping its rows would scramble the display) — the parked main
grid is still reflowed, so the text you return to is correct.

## Configuration file (src/config.c)

`key = value`, one per line, read once at startup from `-c FILE`, else
`$XDG_CONFIG_HOME/k-term/k-term.conf`, else `$HOME/.config/k-term/k-term.conf`. CLI flags are applied
AFTER the file, so they win; `-w DIR` affects only that launch and never writes the file back.

* Parsing details: the value is trimmed; `#` starts a comment only at the start of a line or after a
  space/tab, so a value may contain `#` (e.g. `working_dir = /home/me/my#project` — past bug, session
  39, when the first `#` anywhere truncated it). Unknown keys log `config: unknown key '<k>'` and are
  ignored. A missing file is not an error.
* Colours (`foreground`, `background`, `cursor_color`) accept `#RRGGBB`, `0xRRGGBB` or `AARRGGBB`; an
  omitted alpha means opaque (`|= 0xff000000`); trailing garbage falls back to the previous value.
* Ranges are clamped at parse time: `font_size` 6–256, `cols` 20–4096, `rows` 4–4096,
  `scrollback` 0–1000000, `opacity` 0.0–1.0 (`fclamp_`-style clamp inline), `repeat_delay` 0–5000 ms
  (0 = no client-side repeat), `repeat_rate` 1–200 per second, `log` `L_ERROR`–`L_DEBUG`.
  `clear_scrollback` and `copy_on_release` are off only for `0`/`false`/`no`/`off` (one shared
  `cfg_bool()`).
* `working_dir` is stored already tilde-expanded (`expand_tilde`, a leading `~` or `~/`); main.c then
  resolves `-w` > config > `$HOME`, `stat`s it in the parent (clean warning + `$HOME` fallback instead
  of a chdir failure inside the child) and logs `shell starts in <dir>`.
* Defaults live in `config_default()`; the same values are the `DEF_*` macros in k-term.h, so a README
  table and the code cannot drift apart silently — check both when changing one.

## Rendering

Two textures: the alpha8 mono glyph atlas and the RGBA8 color-emoji atlas (each uploaded only when
its `*_dirty` flag is set). One shader: `position = a_origin + a_pos * a_size`,
`tex = mix(a_tex0, a_tex1, a_pos)` (Y-flip baked into `tex0`/`tex1`). `a_glyph` selects the path:
0 = background quad, 1 = mono glyph (alpha8, tinted by `v_fg`), 2 = color emoji (premultiplied RGBA,
not tinted).

* Pass order (back to front): (1) every cell's background, (2) the selection bands, (3) the glyph
  quads. All backgrounds must precede all glyphs: a WIDE glyph quad spans into the next cell, and the
  old per-cell loop emitted that neighbour's background AFTER the glyph, clipping its right half
  (invisible until font fallback made wide glyphs actually render). Continuation cells (`ATTR_WIDE`)
  are skipped in pass 3. Layer order stays background < band < glyph, so the cursor block, `ATTR_DIM`
  and the per-fragment sub-glyph selection all behave as before.
* Instanced path (used when the driver advertises both `GL_ARB_draw_instanced` and
  `GL_ARB_instanced_arrays`): static unit-quad corner VBO supplies `a_pos` (divisor 0);
  a second VBO holds 19-float per-instance records (origin, size, tex0, tex1, fg, bg,
  glyph, band0, band1) with divisor 1; `glDrawArraysInstancedARB(TRIANGLES, 0, 6, nquads)`.
* Fallback path: the same records are expanded to interleaved per-vertex data (21 floats per
  vertex: the 2-float corner + the 19-float record) and drawn with `glDrawArrays`.
  Same shader, same single draw call.

Note: `GL_ARB_vertex_instanced` does not exist — do not gate on it (past bug: the
instanced path never activated).

### Window geometry: one helper decides the grid size AND its origin

`term_grid_geometry(win_w, win_h, cell_w, cell_h, pad, &cols, &rows, &xoff, &yoff)` (src/vt.c) is the
only place that turns a window pixel size into a grid size and a pixel origin. THREE users must agree on
it, and that is why it is one function: `term_size_changed()` (the grid and the PTY size),
`sel_cell()`/`mouse_cell()` (the pointer -> cell mapping) and `render_draw()` (where the quads go, via
`render_set_origin()`). Any drift between them puts the highlight on a different glyph than the one
under the pointer.

Rules it implements, and the invariant they protect:

* `Config.padding` is applied on all four sides, so no glyph sits under a compositor's rounded window
  corner. `xoff = yoff = pad`.
* The margin is capped at `(window - one cell) / 2` per side, so a tiny window still gets a 1x1 grid
  whose cell lies inside the window.
* `cols = (win_w - 2*xoff) / cell_w` (floor, minimum 1) — the pixels left over by whole cells stay at
  the **right and bottom**, never at the top/left:

```
pad = vh - gy - rows*ch       (clamped >= 0; the configured margin PLUS the remainder)
py  = (rows - 1 - y) * ch + pad        (GL y grows upward)
px  = gx + x * cw
```

Viewport row y therefore occupies window pixels `[gy + y*cell_h, gy + (y+1)*cell_h)` and column x
occupies `[gx + x*cell_w, gx + (x+1)*cell_w)`, and the pointer mapping is exact:
`row = (ptr_y - gy) / cell_h`, `col = (ptr_x - gx) / cell_w`, clamped to the grid. A pointer in the
margin clamps to the first row/column, one in the leftover bottom/right strip to the last. Anchor the
grid anywhere else and every click, drag selection and SGR mouse report lands on the wrong row (past
bug, session 30: bottom-left anchoring put the leftover strip at the top and shifted the selection by up
to a whole row). `tests/test_vt.c` sweeps every pixel of the grid area for several window/cell/padding
combinations and checks that the cell the input formula names is the one the renderer draws there.

The margin is painted by `glClear`, not by a quad, so the clear colour must be the CONFIGURED
background (`Render.bg`, premultiplied by `Config.opacity`) — `render_init` used to hardcode `DEF_BG`
there, which showed up as a differently-coloured frame around any window with a custom `background`
(measured with `wf-recorder -c ffv1`, which records losslessly). `render_draw` clears every frame: the
margin is outside every quad, and a resized back buffer has no defined contents at all.

### Selection: cell-granular endpoints, then per-fragment compositing

**Endpoints (main.c).** A drag only ever produces CELL edges: `sel_cell()` maps the pointer to a cell
(no sub-cell fraction any more) and `sel_set_endpoints(ax,ay,bx,by)` gives the endpoint that comes FIRST
in reading order that cell's LEFT edge (`sel_af/sel_bf = 0`) and the later one its RIGHT edge (`1000`).
The direction is recomputed on every motion event, so dragging left past the anchor SWAPS the two fracs
— `term_selection_span` only reorders the pair, it does not mirror them, and without the swap the anchor
cell would drop out of a leftward selection. Consequence: a highlighted range always covers whole glyphs,
and a press without a drag covers exactly one cell.

**Which covered cells are painted (vt.c `term_cell_highlightable`).** Being inside the span is not
enough: pass 2 skips a cell that was never written (`ch == 0`) so a selection dragged past the end of a
line stops at the last glyph, while a real space (`' '`) and the `ATTR_WIDE` continuation cell of a wide
glyph keep their band (dropping the continuation would strip the highlight off the right half of a CJK /
emoji glyph). Pass 1's cursor-block `hl_on` uses the SAME predicate, otherwise a selection covering the
(usually empty) cursor cell would hide the block without drawing a band in its place.

**Compositing (no GL blending).** `glEnable(GL_BLEND)` is never called; the fragment shader composites
`premul + bg*(1-a)` and treats `bg` as the colour already behind the quad. The covered sub-range of a
cell is computed in `vt.c` (`sel_span_cover` -> `sel_band_pixels`, absolute pixel x/width) and passed to
the shader as `band0`/`band1`; the vertex shader passes the fragment's absolute pixel x (`v_px`). Behind
a glyph the background is chosen per fragment: `bg = (v_glyph > 0.5 && v_px >= band0 && v_px < band1) ?
u_sel : v_bg`. The sub-cell machinery is deliberately kept (it is unit-tested and still handles any
fraction) even though main.c now only ever feeds it whole-cell edges — that keeps the geometry general
without paying for GL blending, which would break the transparency feature below. The band is drawn for
every covered, highlightable cell, including the cursor cell (its glyph is tinted `CURSOR_COL` instead of
being drawn as a solid block), so the highlight always matches `term_selection_text()`.

### Window transparency (`opacity`)

`Config.opacity` (0.0–1.0, from the config key or `-o`) is copied into `Render.opacity` by `render_init`
with **`fclamp_`, never `iclamp_`** — `iclamp_` takes `int`, so `iclamp_(0.5f, 0.0f, 1.0f)` silently
becomes 0 and the window turns fully transparent (past bug, session 41; see ISSUES.md "Fixed").
The EGL config asks for `EGL_ALPHA_SIZE 8` (falling back to a config without it) and the window surface
is therefore ARGB. In `render_draw` the alpha of the per-cell background colour (`bg[3] *= r->opacity`),
of the selection colour (`selc[3] *= r->opacity`, which also feeds `u_sel`) and of the GLYPH quad's own
background colour (pass 3 — it used to stay opaque, which painted a near-black box over the counter of an
`o` on a translucent window) carries the window opacity into the shader via `a_bg.a`; `glClearColor` is
premultiplied by the same factor.
The three output branches of the fragment shader then composite **source-over in premultiplied space**:
`bgp = bg.rgb * bg.a` and `gl_FragColor = vec4(ink + bgp*(1-a), a + bg.a*(1-a))` (for a color emoji
texel: `vec4(c.rgb + bgp*(1-c.a), c.a + bg.a*(1-c.a))`). Two reasons, both measured:
* PREMULTIPLICATION — a Wayland EGL ARGB buffer is premultiplied (the compositor computes
  `surface_rgb + (1-surface_a)*backdrop`), so writing the raw colour with a partial alpha adds the
  background at FULL strength and a translucent window reads washed out (session 41's screenshot: a
  (16,16,20) background at `-o 0.3` over a red backdrop read (45,38,52) instead of the correct blend).
* NOT `max(a, bg.a)` — that forced alpha 1 for every fragment inside a glyph quad's rectangle, so the
  glyph's bitmap bounding box (the counter of an `o`, the margins around a thin glyph) was painted opaque
  near-black while the rest of the window stayed translucent.
The block cursor goes through the same path, so at opacity < 1 it is translucent too — a deliberate
simplification (no extra attribute for it). At opacity 1.0 every branch is byte-identical to the old
behaviour (`bg.a == 1` -> `bgp == bg.rgb` and `a + 1*(1-a) == 1`), so the opaque default cannot regress.

### Color emoji & grapheme clusters (src/vt.c -> src/font.c -> src/render.c)

The whole text-to-pixel chain for an emoji is: bytes -> ONE glyph id -> raw pixels -> GPU texture.

1. **Shape (vt.c + HarfBuzz).** `putch` folds every joiner/modifier into the *anchor* cell of the
   cluster instead of giving it a cell of its own: ZWJ, variation selectors (U+FE00-FE0F), skin-tone
   modifiers (U+1F3FB-1F3FF), the enclosing keycap (U+20E3), tag chars (U+E0020-E007F), and the
   SECOND regional indicator of a flag pair. They are stored inline in `Cell.extra[]` (`nextra`,
   capped at `CLUSTER_MAX` = 8) so a Cell stays copyable by value through row realloc, ICH/DCH
   shifts and grid rotations. `font_color_glyph(seq, n)` then hands the WHOLE sequence to
   `hb_shape`, which applies the font's GSUB rules and collapses it to exactly one glyph id
   (family, flag, skin-tone thumbs-up, keycap, ...). A sequence that does not collapse to one real
   glyph falls back to the mono path.
2. **Rasterize (FreeType).** `FT_Load_Glyph(gid, FT_LOAD_COLOR | FT_LOAD_NO_SCALE)` yields the
   premultiplied BGRA bitmap embedded in the color font (CBDT/CBLC on this box); it is byte-swapped
   to RGBA and packed into the RGBA8 color atlas. Metrics stay UNSCALED strike pixels (128 here);
   the renderer scales by `cell_h / color_px`, so zooming never re-rasterizes an emoji.
3. **Upload (OpenGL).** The atlas is uploaded with `glTexImage2D(..., GL_RGBA, ...)` on texture
   unit 1 and sampled by the `a_glyph == 2` branch of the fragment shader
   (`c + bg*(1-c.a)`, deliberately not fg-tinted; `v_fg` is white, 0.5 grey for `ATTR_DIM`).

**Upload-order invariant:** both atlases are uploaded *after* the three emit passes and *before* the
 draw. The glyph lookups run inside pass 3, so uploading at the top of `render_draw` shows every
 newly rasterized glyph one frame late — and a first color emoji as a black box, because the frame
 that shapes it is often the last one before the terminal idles (past bug, session 37).

**Width reservation (the GPU grid rule).** A wide cluster occupies TWO grid cells so its bitmap is
never squished: `putch` reserves the pair up front when `cp_is_wide(base)` (CJK + Emoji_Presentation
ranges + a lone regional indicator) and writes the second cell as an `ATTR_WIDE` continuation, which
pass 3 skips. `cluster_is_wide(seq, n)` is the single source of truth for the width of a WHOLE
cluster: it is also true when a modifier switches the base to emoji presentation — U+FE0F on an
emoji-capable base (U+2764 U+FE0F, U+1F3F3 U+FE0F ZWJ U+1F308) or U+20E3 on a digit/`#`/`*`. Because
the base is written *before* the modifier arrives, `put_cluster_join` upgrades the 1-cell anchor to a
2-cell pair in place (writes the continuation cell, steps the cursor over it, sets `cl_w = 2`);
at the right margin there is no second cell, so the emoji keeps one cell. `render_draw` sizes the
color quad with the SAME predicate, so a quad can never be wider than what the grid reserved.

**Cluster state.** `Term.cl_valid/cl_cx/cl_cy/cl_w/cl_join` track the anchor being built; `cl_cx/cl_cy`
are VIEWPORT coordinates, so `cluster_invalidate()` runs on every row rotation, cursor jump, BS/HT/CR,
newline and `term_resize`. `nextra` is cleared wherever a cell is blanked (`row_fillblank`,
`erase_cells`, grown columns).

**Caches & limits.** The color cache is keyed by the codepoint SEQUENCE (`ctab`), and misses are
cached too (`gid = 0`, `w = h = 0`) so an unsupported codepoint is shaped once, not per frame. The
color atlas starts at 1024x512 and doubles its height up to `CATLAS_H_MAX` (8192 = 32 MB); a glyph
that cannot fit is invalidated (falls back to mono) rather than drawn with stale texcoords, and the
packer's fit check must account for the shelf WRAP (past bug: heap-buffer-overflow, session 37).
`font_resize` keeps the atlas for a bitmap-only strike and rebuilds it for a scalable color face
(`hb_ft_font_changed`). `font_destroy` destroys the hb font BEFORE the face (it holds a reference).

## Debugging entry points

* `./bin/k-term -d` — log level 3 (font chosen, `color emoji: <path> (<n>px strike)`, GL version,
  instancing state, `shell starts in <dir>`, `ED 3: dropped N scrollback rows`,
  `paste: N bytes, bracketed 0|1` for every clipboard paste, `wl_pointer` interface version, clipboard
  data device, grid size + window/cell geometry
  (`grid resized to CxR (window WxH px, cell wxh, padding P, unused ux x uy px)`), `reflow: OLD -> NEW rows at N
  cols (K evicted)` for every width change that needed more rows than before, `compositor repeat_info:
  rate R/s delay D ms (k-term uses repeat_delay=… repeat_rate=… from the config)` (what the compositor
  was configured with vs. what K-Term actually uses), `toplevel state: fullscreen F maximized M
  activated A (configure WxH px)` whenever the compositor changes those states (the only client-side
  evidence of entering/leaving fullscreen), `configure answered: buffer WxH attached (grid CxR)` for
  every configure whose size change was answered with a real buffer, `zoom: font Npx, grid CxR` after each
  zoom step, `selection copied: N bytes` / `selection finished: ax,ay -> bx,by (copy_on_release off —
  Ctrl+Shift+C to copy)` at the end of a drag, and — while a selection
  drag is live — the cell each pointer event maps to (`sel press/drag cell=(col,row) frac=… ptr=(x,y) …`).
  Use that line to check a selection-vs-pointer complaint without a screenshot. A `swap failed (N in a
  row), retrying on the next frame` warning means `eglSwapBuffers` returned failure; the frame stays
  dirty and the 8 ms timer retries, so a few of those are self-healing while a long streak means the
  compositor is not showing this window at all.
* `./bin/dbg` — scripted scroll/resize stages; each stage line prints
  `scroll_off/live_off/rows/rcap/used` plus one two-character token per slot up to
  `used`: first char `v` (viewport slot) or `s` (scrollback slot), second char the row's
  first cell (or `.` if blank). Asserts `row[]` is a NULL-free, duplicate-free permutation.
* **Screenshots are available through the compositor**: niri has built-in screenshot actions —
  `niri msg action screenshot-screen` / `screenshot-window --id <id>` write a PNG to
  `~/Pictures/Screenshots/` (path comes from `screenshot-path` in `~/.config/niri/config.kdl`). Analyze
  them with PIL (`python3` + `PIL` are installed) instead of looking at the screen: locate a window by
  the colour of its background, then sample pixels. Session 41 proved transparency this way by floating
  one k-term over another and comparing the same pixel coordinates at `-o 0.3` vs `-o 1.0`.
  `grim`/`slurp` are NOT installed, wf-recorder is installed ; `niri msg --json windows` gives window ids, app_ids,
  pids and sizes but NO absolute screen position (`layout.window_size` and
  `tile_pos_in_workspace_view` are workspace-view values), so do not try to crop a screenshot to a window
  from that JSON. `niri msg action set-window-width/-height --id <id> <px>` plus
  `move-window-to-floating` / `move-floating-window -x -y` drive real resizes (that is how the resize and
  reflow paths were verified: 300x200 -> 30x11 cells, 1800x1000 -> 180x55, and every width change logged a
  `reflow:` line, no errors). **Identify the test window as "the window id that appeared after launch"**
  (snapshot the ids, diff them): matching by title once closed the user's file manager, and `$!` after
  `timeout ./bin/k-term &` is the timeout wrapper's pid, not k-term's, so a pid match fails too.
* A temporary `glReadPixels` readback inside `render_draw` is the other way to measure pixels (add it,
  run, then REMOVE it — it must run BEFORE `eglSwapBuffers`, because after a swap the back buffer is
  undefined and the readback proves nothing). For color emoji, gate it on the frame that actually drew
  color quads (the first frame is the empty startup screen) and classify each cell by
  max-channel-difference (colorful) vs. distance from the background colour (ink); for selection bands,
  sample ABOVE the glyph (85% of the cell height) and twice per cell (left/right quarter), otherwise the
  ink hides the background you are trying to measure. An A/B build (same tree, one predicate stubbed out
  in a copy of `src/vt.c` compiled to a throwaway binary) is what proves a grid/width change actually
  changed the pixels — session 38: pre-fix 7 color quads over 11 cells, fixed 6 quads over 12 cells for
  flag + heart + keycap + grin + rainbow flag + thumbs-up.
* Headless harness pattern: link `src/vt.c` + `src/config.c` into a throwaway program to
  exercise grid/scroll/font logic that Wayland input events cannot reach.
