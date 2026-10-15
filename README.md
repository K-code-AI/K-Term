# K-Term

K-Term is a lightweight terminal emulator for Linux, written in C and built for native Wayland. It runs your shell in a terminal window with GPU-accelerated rendering, Unicode text, color emoji, scrollback, and clipboard support.

K-Term is currently a working prototype. It builds cleanly, runs a shell, and exits cleanly.

## Features

- **Native Wayland** — Resizable windows with a terminal grid that adapts to the window size.
- **Reflow on resize** — Changing the window width re-wraps the text instead of cutting it: a line that
  was wrapped at the old margin is joined back and re-broken at the new one, so widening the window
  brings the text back. Scrollback is re-wrapped too; narrowing far enough drops the oldest lines.
- **GPU rendering** — OpenGL rendering for fast text and graphics.
- **Unicode and color emoji** — Supports complex emoji sequences, including skin tones, flags, and joined emoji.
- **Text and colors** — UTF-8 text, 16-color, 256-color, and 24-bit truecolor support.
- **Scrollback** — Retains history (2000 lines by default) and scrolls with the mouse wheel or the
  keyboard; new output snaps the view back to the live bottom.
- **Full-screen applications** — The alternate screen (`vim`, `htop`, `less`, `nano`) is supported; the
  main screen is parked while an app owns it and comes back re-wrapped when the app exits.
- **Clear scrollback** — `clear` can remove both visible content and terminal history.
- **Mouse support** — Application mouse reporting, text selection, and clipboard integration.
- **Key repeat** — Holding a key down repeats it, with the delay and rate set in the configuration file.
- **Transparency** — Configurable window opacity, subject to compositor support.
- **Font zoom** — Increase or decrease the font size without resizing the window.
- **System clipboard** — Copy and paste use the standard Wayland clipboard, so text copied in K-Term
  pastes into other applications and text copied anywhere else pastes into K-Term.
- **Configuration** — A simple configuration file and command-line options.

## Requirements

- Linux on a Wayland desktop (K-Term is a native Wayland client; it does not run under X11)
- A C compiler supporting `-std=gnu11`
- `pkg-config`
- Wayland client, cursor, and EGL
- EGL and OpenGL
- xkbcommon
- FreeType
- fontconfig
- HarfBuzz
- wayland-scanner
- wayland-protocols

## Build

Build K-Term with:

```sh
make
```

The executable is created at `bin/k-term`.

Other available targets:

| Command | Description |
|---|---|
| `make test` | Build and run the VT unit tests |
| `make asan` | Run tests with AddressSanitizer and UBSan |
| `make run` | Build and launch K-Term |
| `make clean` | Remove generated build files |

## Usage

Launch K-Term:

```sh
./bin/k-term
```

Run a specific command:

```sh
./bin/k-term -e htop
```

Open a terminal in a specific directory:

```sh
./bin/k-term -w "/home/me/My Projects"
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

Command-line options take precedence over configuration values. Everything that describes how the
terminal looks — font, font size, columns, rows, scrollback, colours, opacity — is configured in the
configuration file only. Any option K-Term does not accept is rejected with
`k-term: unknown option '<flag>'` followed by the option list above.

## Configuration

The default configuration file is:

```text
~/.config/k-term/k-term.conf
```

If `XDG_CONFIG_HOME` is set, K-Term uses `$XDG_CONFIG_HOME/k-term/k-term.conf` instead. Use `-c FILE` to specify another location.

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
| `shell` | Empty | Shell to run; defaults to `$SHELL` |
| `working_dir` | Empty | Startup directory; defaults to the home directory |
| `app_id` | `K-Term` | Wayland app ID reported to the compositor (window rules, taskbar grouping) |
| `clear_scrollback` | `1` | Clear history when `clear` is used |
| `copy_on_release` | `0` | Copy to the clipboard when the selection drag is released |
| `repeat_delay` | `300` | Milliseconds a key must be held before it starts repeating (`0` disables repeating) |
| `repeat_rate` | `30` | Repeats per second once a key starts repeating |
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

Dragging with the left mouse button selects text when the running application has not taken control of
the mouse. Releasing the button keeps the selection visible; `Ctrl`+`Shift`+`C` copies it to the system
clipboard, from where any other application can paste it. Set
`copy_on_release = 1` in the configuration file to copy as soon as the button is released (the earlier
behaviour). Text copied in another application pastes into K-Term with `Ctrl`+`Shift`+`V`.

Selection works a whole character at a time: a highlighted range always covers complete glyphs, never half of one. Empty space past the end of a line is not highlighted, while spaces between characters are — the copied text is unchanged either way.

When an application enables bracketed paste, multiline clipboard content is sent as a single paste operation rather than being executed line by line.

Holding a key down repeats it: the first repeat arrives after `repeat_delay` milliseconds and further
repeats follow at `repeat_rate` per second. Repeating works for everything typed into the shell — text,
`Backspace`, arrows, `Enter`, `Tab`, `Escape`, function keys, and `Ctrl` combinations — as well as
`Shift`+`PageUp`/`PageDown`/`Home`/`End` history scrolling. The shortcuts that change the terminal itself
(`Ctrl`+`+`/`-`/`0` zoom and `Ctrl`+`Shift`+`C`/`V`) never repeat, so a held key cannot run past a font
size or paste over and over. Releasing the key stops the repeat immediately; setting `repeat_delay = 0`
disables repeating entirely.

## Documentation

- [ISSUES.md](ISSUES.md) — Known limitations and outstanding issues.
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — Internal design and implementation details.
- [LICENSE](LICENSE) — MIT License.

## License

K-Term is licensed under the MIT License.
