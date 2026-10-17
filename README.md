# Kai-Term

Kai-Term is a lightweight terminal emulator for Linux, written in C and built for native Wayland. It runs your shell in a terminal window with GPU-accelerated rendering, Unicode text, color emoji, scrollback, and clipboard support.

Kai-Term is currently a working prototype. It builds cleanly, runs a shell, and exits cleanly.

## Features

- **Native Wayland** — Resizable windows with a terminal grid that adapts to the window size.
- **Reflow on resize** — Changing the window width re-wraps the text instead of cutting it: a line that
  was wrapped at the old margin is joined back and re-broken at the new one, so widening the window
  brings the text back. Scrollback is re-wrapped too; narrowing far enough drops the oldest lines.
- **GPU rendering** — OpenGL rendering for fast text and graphics.
- **Unicode and color emoji** — Supports complex emoji sequences, including skin tones, flags, and joined emoji.
- **Images in the terminal** — An application can draw a picture inline in the text with the kitty
  graphics protocol (`timg -p k`). PNG payloads, optionally zlib-compressed, and raw RGB/RGBA pixels
  are decoded; the picture is placed at the cursor, sized from the terminal's cell pixel size, scrolls
  with the text around it, stays in scrollback, and never covers the text drawn over it.
- **Text and colors** — UTF-8 text, 16-color, 256-color, and 24-bit truecolor support.
- **Scrollback** — Retains history (2000 lines by default) and scrolls with the mouse wheel or the
  keyboard; new output snaps the view back to the live bottom.
- **Full-screen applications** — The alternate screen (`vim`, `htop`, `less`, `nano`) is supported; the
  main screen is parked while an app owns it and comes back re-wrapped when the app exits.
- **Clear scrollback** — `clear` can remove both visible content and terminal history.
- **Mouse support** — Application mouse reporting, text selection, and clipboard integration.
- **Key repeat** — Holding a key down repeats it, with the delay and rate set in the configuration file.
- **Transparency** — Configurable window opacity, subject to compositor support.
- **Window padding** — A configurable margin around the text, so a compositor's rounded window corners
  never clip a glyph.
- **Several sessions in one window** — Each session has its own shell, scrollback, selection and title;
  opening, closing and switching between them are keybindings you set in the configuration file.
- **Tab bar** — With two or more sessions, one row at the top of the window names each of them and marks
  the ones with output you have not seen. A click switches to a session, a middle click closes it.
- **Side-by-side view** — The sessions can be drawn next to each other as equal-width panes in the same
  window; one shortcut flips between that and the one-session-per-window view.
- **Font zoom** — Increase or decrease the font size without resizing the window.
- **System clipboard** — Copy and paste use the standard Wayland clipboard, so text copied in Kai-Term
  pastes into other applications and text copied anywhere else pastes into Kai-Term.
- **Configuration** — A simple configuration file and command-line options.

## Requirements

- Linux on a Wayland desktop (Kai-Term is a native Wayland client; it does not run under X11)
- A C compiler supporting `-std=gnu11`
- `pkg-config`
- Wayland client, cursor, and EGL
- EGL and OpenGL
- xkbcommon
- FreeType
- fontconfig
- HarfBuzz
- libpng
- zlib
- wayland-scanner
- wayland-protocols

## Build

Build Kai-Term with:

```sh
make
```

The executable is created at `bin/kai-term`.

Other available targets:

| Command | Description |
|---|---|
| `make test` | Build and run the unit tests, the grid-model checker and the command-line tests |
| `make test-cli` | Run only the command-line tests against `bin/kai-term` |
| `make asan` | Run the tests with AddressSanitizer and UBSan |
| `make run` | Build and launch Kai-Term |
| `make clean` | Remove generated build files |

## Usage

Launch Kai-Term:

```sh
./bin/kai-term
```

Run a specific command:

```sh
./bin/kai-term -e htop
```

Open a terminal in a specific directory:

```sh
./bin/kai-term -w "/home/me/My Projects"
```

The default startup directory is the user's home directory. You can change it in the configuration file or override it for a single launch with `-w`.

### Command-line options

| Option | Long form | Description |
|---|---|---|
| `-e COMMAND` | `--exec COMMAND` | Run a command instead of the default shell |
| `-w DIR` | `--workingdir DIR` | Set the startup directory for this launch |
| `-c FILE` | `--config FILE` | Specify a configuration file |
| `-d` | `--debug` | Enable debug logging |
| `-v` | `--version` | Print the version and exit |
| `-h` | `--help` | Show the option list and exit |

Long options also accept the `--name=VALUE` form (for example `--workingdir=/tmp`).

### Images

Kai-Term speaks the kitty graphics protocol: an application sends a picture as one or more escape
sequences, Kai-Term decodes it and draws it between the cell backgrounds and the text, so the text
stays readable on top of it. The picture is placed at the cursor, sized from the cell pixel size
(Kai-Term reports that to the shell in the PTY window size, and answers `CSI 14 t` / `CSI 16 t` for
applications that ask), scrolls with the text around it, and comes back when you scroll into history.

With [timg](https://timg.sh) installed:

```sh
timg -p k foo.png        # -p k selects the protocol Kai-Term understands
```

The picture appears at the cursor and the prompt lands below it. The mouse wheel or `Shift`+`PageUp`
bring it back after it has scrolled off; `clear` drops it together with the history.

timg does not choose that protocol by itself: it auto-detects graphics only on terminals whose `TERM`
is `xterm-kitty` or `xterm-ghostty` (measured with timg 1.6.3). Without `-p k` it draws the picture
with quarter-block characters instead, which needs no terminal support at all. What else is not
supported is listed in [ISSUES.md](ISSUES.md) under "Known limitations".

Command-line options take precedence over configuration values. Everything that describes how the
terminal looks — font, font size, columns, rows, scrollback, colours, opacity — is configured in the
configuration file only. Any option Kai-Term does not accept is rejected with
`kai-term: unknown option '<flag>'` followed by the option list above.

## Configuration

The default configuration file is:

```text
~/.config/kai-term/kai-term.conf
```

If `XDG_CONFIG_HOME` is set, Kai-Term uses `$XDG_CONFIG_HOME/kai-term/kai-term.conf` instead. Use `-c FILE` to specify another location.

Settings use a simple `key = value` format.

### Available settings

| Setting | Default | Description |
|---|---|---|
| `font` | `monospace` | Fontconfig font pattern |
| `font_size` | `14` | Font size in pixels |
| `cols` | `80` | Initial number of columns |
| `rows` | `24` | Initial number of rows |
| `scrollback` | `2000` | History lines to retain. The grid allocates `rows + scrollback` lines once at startup, so a window taller than `rows` keeps fewer of them (`rows + scrollback - current_rows`) |
| `foreground` | `#e5e5e5` | Text color |
| `background` | `#101014` | Background color |
| `cursor_color` | `#d8d8d8` | Cursor color |
| `opacity` | `1.0` | Window opacity |
| `padding` | `8` | Pixels of margin between the window edge and the first/last row and column (`0` puts the text flush against the edge). Applied on all four sides; pixels left over by whole cells still go to the bottom and right |
| `shell` | Empty | Shell to run; defaults to `$SHELL` |
| `working_dir` | Empty | Startup directory; defaults to the home directory |
| `app_id` | `Kai-Term` | Wayland app ID reported to the compositor (window rules, taskbar grouping) |
| `clear_scrollback` | `1` | Clear history when `clear` is used |
| `copy_on_release` | `0` | Copy to the clipboard when the selection drag is released |
| `repeat_delay` | `300` | Milliseconds a key must be held before it starts repeating (`0` disables repeating) |
| `repeat_rate` | `30` | Repeats per second once a key starts repeating |
| `max_tabs` | `8` | Sessions one window may hold (clamped to 1–64) |
| `tab_bar` | `auto` | `off` never draws the tab bar, `auto` draws it from the second session on, `always` also with one session |
| `split_max` | `4` | Sessions drawn side by side at most; more sessions stay open and stay listed in the bar |
| `key_new_tab` | `Ctrl+Shift+T` | Open another session (`none` unbinds it) |
| `key_close_tab` | `Ctrl+Shift+W` | Close the visible session (`none` unbinds it) |
| `key_next_tab` | `Ctrl+Shift+Right` | Show the next session |
| `key_prev_tab` | `Ctrl+Shift+Left` | Show the previous session |
| `key_tab_prefix` | `Ctrl+Shift` | Hold these and press `1`–`9` to jump to that session (`none` disables it) |
| `key_toggle_layout` | `Ctrl+Shift+S` | Switch between the one-session view and the side-by-side view |
| `log` | `1` | Log level: 0 = error, 1 = warning, 2 = info, 3 = debug |

Font size, grid dimensions, scrollback limits, opacity, and the key-repeat timings have defined valid
ranges; out-of-range values are clamped to the nearest valid one. Invalid configuration values should be
corrected according to the application's diagnostics.

### Example configuration

```ini
font = JetBrains Mono
font_size = 16
cols = 100
rows = 30
background = #1c1c22
opacity = 0.95
```

## Keyboard shortcuts

| Shortcut | Action |
|---|---|
| Mouse wheel | Scroll through terminal history |
| `Shift` + `PageUp` / `PageDown` | Scroll by one page |
| `Shift` + `Home` / `End` | Jump to the oldest history line / live bottom |
| `Ctrl` + `+` / `-` | Increase / decrease font size |
| `Ctrl` + `=` / `-` | Alternative font zoom shortcuts |
| `Ctrl` + `0` | Restore the configured font size |
| `Ctrl` + `Shift` + `C` | Copy the selection |
| `Ctrl` + `Shift` + `V` | Paste from the clipboard |
| `Ctrl` + `Shift` + `T` | Open another session in this window |
| `Ctrl` + `Shift` + `W` | Close the visible session (closing the last one exits Kai-Term) |
| `Ctrl` + `Shift` + `Right` / `Left` | Show the next / previous session |
| `Ctrl` + `Shift` + `1`…`9` | Jump to that session |
| `Ctrl` + `Shift` + `S` | Switch between the one-session view and the side-by-side view |

Dragging with the left mouse button selects text when the running application has not taken control of
the mouse. Releasing the button keeps the selection visible; `Ctrl`+`Shift`+`C` copies it to the system
clipboard, from where any other application can paste it. Set
`copy_on_release = 1` in the configuration file to copy as soon as the button is released (the earlier
behaviour). Text copied in another application pastes into Kai-Term with `Ctrl`+`Shift`+`V`.

Selection works a whole character at a time: a highlighted range always covers complete glyphs, never half of one. Empty space past the end of a line is not highlighted, while spaces between characters are — the copied text is unchanged either way.

When an application enables bracketed paste, multiline clipboard content is sent as a single paste operation rather than being executed line by line.

Holding a key down repeats it: the first repeat arrives after `repeat_delay` milliseconds and further
repeats follow at `repeat_rate` per second. Repeating works for everything typed into the shell — text,
`Backspace`, arrows, `Enter`, `Tab`, `Escape`, function keys, and `Ctrl` combinations — as well as
`Shift`+`PageUp`/`PageDown`/`Home`/`End` history scrolling. The shortcuts that change the terminal itself
(`Ctrl`+`+`/`-`/`0` zoom and `Ctrl`+`Shift`+`C`/`V`) never repeat, so a held key cannot run past a font
size or paste over and over. Releasing the key stops the repeat immediately; setting `repeat_delay = 0`
disables repeating entirely.

## Sessions and layouts

One Kai-Term window can hold several sessions. Each one runs its own shell and keeps its own scrollback,
selection, scroll position and title; they share the window, the font size and the layout.

With two or more sessions a bar appears at the top of the window (unless `tab_bar = off`). Each item shows
the session number, a `*` when a session you are not looking at has produced output, and that session's
title — the title the shell or application sets for itself, or `shell` until it does. The visible session's
item is highlighted. A left click on an item shows that session; a middle click closes it. In side-by-side
view a click inside a pane that is not the visible one focuses that session.

`Ctrl`+`Shift`+`S` flips between the two views: one session filling the window, and all sessions (up to
`split_max`) side by side as equal-width panes with the visible one bordered. Sessions beyond `split_max`
stay open and stay listed in the bar; the shown group of panes slides to contain the visible one. Every
session gets the same grid size in a given layout, so switching to a session that was hidden costs no
resize and no re-wrap of its scrollback.

Closing the last session exits Kai-Term; a session whose shell exits closes by itself and the others keep
running.

The shortcuts are configurable, and a binding is written as modifiers joined by `+` and one key, for example
`Ctrl+Shift+T`, `Alt+Right`, `Super+Return`, `Ctrl+Shift+F5`. Named keys are the arrows, `Home`, `End`,
`Page_Up`, `Page_Down`, `Insert`, `Delete`, `BackSpace`, `Tab`, `Return`, `Escape`, `Space` and `F1`–`F12`;
any other character stands for itself. A binding with no modifier is refused, so it cannot swallow ordinary
typing, and `none` unbinds an action. `key_tab_prefix` is the modifier half of the `1`–`9` jump: with the
default `Ctrl+Shift`, pressing `Ctrl+Shift+3` shows the third session.

## Documentation

- [ISSUES.md](ISSUES.md) — open problems (currently none) and known limitations.
- [AGENT.md](AGENT.md) — Development journal: current state, design invariants and known pitfalls.
- [LICENSE](LICENSE) — MIT License.

## License

Kai-Term is licensed under the MIT License.
