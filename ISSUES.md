# Kai-Term — Known Issues

Current problems and current behaviour only. The history of what was broken and fixed is NOT kept here:
54 root-cause entries and the 4 unproven reports cleared 2026-10-17 are in `git show d5afe4a:ISSUES.md`.
The rules those bugs produced are in AGENT.md "Architecture & Invariants" — read those, not the history.

## Open

**None.** Verified 2026-10-17 from a clean tree: `make` exit 0 with 0 warnings; `make test` -> 501 checks
0 failures + `MODEL OK (0 problems)` + `CLI: 86 checks, 0 failures, 0 skipped`; `make asan` -> 501/0 +
MODEL OK with no AddressSanitizer/UBSan/LeakSanitizer report; no `TODO`/`FIXME`/`XXX` anywhere in
`src/`, `include/`, `tests/`.

To file a problem: symptom, exact reproduction, and what `./bin/kai-term -d` logs. Not reproducible ->
not filed; it is not a bug until it reproduces.

## Known limitations

Not bugs — current behaviour, each re-checked against the source 2026-10-17. Do not "fix" by accident.

**Grid / scrollback**

- **Scrollback capacity is fixed at startup.** `rcap = rows + scrollback` is allocated once, so every row
  the viewport gains by growing is a row of history lost: the bound is
  `sb_limit() = min(scrollback_max, rcap - rows)`, not `scrollback_max`. Shrinking does not restore it.
- **Narrowing can evict the OLDEST history lines.** Reflow re-wraps every retained line; if the result
  needs more rows than `rcap`, the oldest go, like ordinary overflow. Newest always survive; widening
  cannot bring evicted lines back. `-d` logs `reflow: 55 -> 58 rows at 26 cols (3 evicted)`.
- **Reflow is normal-screen only.** The alternate screen (`?1049h`: vim, htop, less) is truncated/grown in
  place by `grid_resize_plain()` — a full-screen app repaints after SIGWINCH and re-wrapping would
  scramble it. The parked main screen IS reflowed.
- **Whole-cell remainder stays at bottom/right.** `cols = w / cell_w` is floor division, so `w % cell_w`
  and `h % cell_h` px are left over, painted with the configured `background` like the `padding` margin
  (bottom margin = `padding` + vertical remainder). The grid is anchored top-left so `ptr / cell` names
  the right cell — anchoring elsewhere breaks every click, drag and SGR report.
- **Minimum window 200x100 px** (`xdg_toplevel_set_min_size`). A compositor may still configure smaller
  and Kai-Term handles it (110 px high -> 6 rows), but interactive resizing stops there.

**Selection / clipboard**

- **Selection is viewport-relative** — scrolling into history, resizing or zooming clears it
  (`term_select_clear`).
- **Selection highlights whole cells only** (by design): a range always starts/ends on a cell edge, so a
  partial glyph is impossible. The band skips cells never written (`ch == 0`), so a drag past end-of-line
  stops at the last glyph; real spaces and `ATTR_WIDE` continuation cells stay highlighted. Copied text is
  unaffected (blank cells copy as spaces, trailing spaces per line trimmed).
- **A mouse drag alone does not copy.** `copy_on_release` defaults to 0, so releasing only leaves the
  selection visible — pasting elsewhere gives whatever was there before. `Ctrl+Shift+C` (or
  `copy_on_release = 1`) is what puts it on the clipboard. Usual cause of a "Kai-Term has its own
  clipboard" report: the copy was never offered. The clipboard is the ordinary Wayland selection
  (`wl_data_device`), shared with every client — verified in both directions.
- **Only `text/plain` is offered on copy** (one mime type requested on paste), and
  `wl_data_offer_accept()` is never called before `wl_data_offer_receive()`. niri tolerates both
  (`wl-copy` text reaches the PTY byte-for-byte), but a compositor that matches mime types strictly could
  refuse. First things to check if pasting from other apps breaks.
- **A paste over `PASTE_BUF_MAX` (8 MiB) is sent unwrapped** — no `ESC[200~`/`ESC[201~` — with a log
  warning, because the whole block must be buffered before delimiters can surround it.
- **Bracketed paste is per application:** the delimiters are sent only when the app has DEC mode 2004 on.
  A shell/readline with bracketed paste off correctly keeps getting raw line-by-line pastes.

**Sessions / layout**

- **The tab bar costs one text row, and the grid wins if both do not fit.** With `tab_bar = always`, or
  from the second session on under the default `auto`, one cell row at the top is chrome. If the window is
  too small for that row + its gap + one text row, the bar is dropped for that frame rather than shrinking
  the grid to nothing.
- **Bar labels are cut to their item's width, no ellipsis.** Items have equal width (grid width / session
  count), so with many sessions a label can be a few characters. The cut is at a UTF-8 codepoint edge,
  never inside a multi-byte sequence; the number and the `*` unread mark are part of the label, so a long
  OSC title is what gets truncated.
- **Only `split_max` sessions are drawn in side-by-side view.** The rest stay open, keep running and stay
  listed in the bar; their output reaches you only as the `*` mark and `!N` in the window title. The shown
  pane group slides to contain the active session, so first and last sessions are reachable.
- **All sessions of a window share one grid size.** Any layout change (resize, zoom, tab/side-by-side
  toggle, open/close) resizes EVERY session to the same cols x rows: every shell gets SIGWINCH and every
  session's scrollback re-wraps. That is what makes switching to a hidden session free, and why a toggle
  touches sessions you are not looking at.

**Rendering / input**

- **Color emoji come from whatever color font fontconfig finds.** The installed NotoColorEmoji.ttf is
  bitmap-only (CBDT/CBLC, a single strike), so zoomed emoji are scaled from that one strike instead of
  re-rasterized; the scalable-color-font branch (`FT_IS_SCALABLE` -> `FT_Set_Pixel_Sizes` +
  `hb_ft_font_changed`) has never run here. At most `CLUSTER_MAX` = 8 modifier codepoints fold into one
  anchor cell (longer sequences split into an extra cell). Unicode text-presentation bases (heart, keycap)
  stay monochrome unless the sequence carries U+FE0F. Some glyphs are grayscale in the installed font —
  font data, not a rendering bug.
- **Transparency needs a compositor that blends ARGB surfaces.** `opacity` is verified on niri (screenshot
  A/B); on a non-compositing or RGB-only path the window just looks opaque.
- **Key repeat tracks one key at a time.** No Wayland compositor sends repeat events, so Kai-Term
  generates them (`repeat_delay` ms, then `repeat_rate`/s). Only the most recently pressed repeatable key
  repeats: hold `a`, press and release `b`, and `a` stops repeating though it is still held. The
  compositor's `wl_keyboard.repeat_info` is logged under `-d` but deliberately NOT used, so timings come
  from the config and feel identical on every compositor. `repeat_delay = 0` disables repeat entirely —
  a compositor never repeats for you.
- **The window title is Kai-Term's own, not the application's.** `update_title()` -> `mux_title()` ->
  `xdg_toplevel_set_title()`: `Kai-Term`, or `Kai-Term [active/total]`, plus `!N` when N background
  sessions produced output. An app's OSC 0/2 title goes to that session's tab bar label instead (or
  `shell` until it sets one), so a taskbar, window rule or script matching on the window title sees
  Kai-Term's text, not the shell's.
