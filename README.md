# RotIDE

RotIDE is a small terminal text editor inspired by
[antirez/kilo](https://github.com/antirez/kilo), with a focus on simple code,
clear control flow, and safety-oriented behavior.

## Current status

RotIDE is an active project and still evolving. Core editing, navigation,
search, save, undo/redo, selection, tabs, and keymap configuration are implemented.

## Features

- Terminal-native editor loop with raw mode input handling.
- UTF-8 aware row operations and grapheme-safe cursor movement.
- Incremental search, go-to-line, and selection mode.
- Multi-tab file buffers with a top tab bar and mouse tab switching.
- Clipboard copy/cut/paste with optional OSC52 terminal sync.
- Undo/redo history for insert/delete/newline edit flows.
- Mouse support (click, drag selection, wheel scroll).
- Atomic save path with temp-file + rename strategy and cleanup handling.
- Crash recovery via autosaved per-project recovery session files.
- Configurable keymap via TOML (`~/.rotide/config.toml` and `./.rotide.toml`).

## Build and run

Requirements:
- C compiler with C2x support.
- POSIX-like environment.

Build:

```bash
make
```

Run:

```bash
./rotide README.md
```

You can pass multiple files and RotIDE opens each one in its own tab:

```bash
./rotide README.md rotide.c tests/rotide_tests.c
```

If no file path is provided, RotIDE starts with an empty tab and prompts for a
filename on first save.

## Default keybindings

- Save: `Ctrl-S`
- Quit: `Ctrl-Q`
- New tab: `Ctrl-N`
- Close tab: `Ctrl-W` (second press required for dirty tab)
- Next/previous tab: `Alt-Right` / `Alt-Left`
- Find: `Ctrl-F`
- Go to line: `Ctrl-G`
- Selection toggle: `Ctrl-B`
- Copy selection: `Ctrl-C`
- Cut selection: `Ctrl-X`
- Delete selection: `Ctrl-D`
- Paste: `Ctrl-V`
- Undo/Redo: `Ctrl-Z` / `Ctrl-Y`
- Move: arrows, `Home`, `End`, `PageUp`, `PageDown`
- New line: `Enter`
- Backspace/Delete: `Backspace` / `Del` (`Ctrl-H` also maps to backspace by default)
- Redraw: `Ctrl-L`

## Configuration

RotIDE supports keymap configuration from TOML files.

Load order (lowest to highest precedence):
1. Built-in defaults
2. Global config: `~/.rotide/config.toml`
3. Project config: `./.rotide.toml`

Behavior on invalid config:
- Invalid global config: ignored, then defaults/project continue.
- Invalid project config: full fallback to defaults.

Keymap section format:

```toml
[keymap]
save = "ctrl+s"
quit = "ctrl+q"
```

Supported key specs:
- Modifiers are case-insensitive and can be in any order.
- Letter combos: `ctrl+<a-z>`, `alt+<a-z>`, `ctrl+alt+<a-z>`
- Arrow combos: `ctrl+left/right/up/down`, `alt+left/right/up/down`,
  `ctrl+alt+left/right/up/down`
- Named keys: `left`, `right`, `up`, `down`, `home`, `end`, `page_up`,
  `page_down`, `enter`, `esc`, `backspace`, `del`

A full example with all configurable actions is included at project root:
`./.rotide.toml`.

## Autosave and recovery

- RotIDE writes recovery snapshots to swap/recovery data (not directly to edited files).
- Autosave is activity-triggered with a short debounce while unsaved changes exist.
- Recovery files are scoped per working directory.
- On startup, if recovery data exists, RotIDE prompts once to restore or discard it.
- If you restore a session, startup file arguments are ignored for that launch.
- Recovery data is deleted on clean exit and when the session becomes fully clean.

## Clipboard integration (OSC52)

RotIDE can mirror internal clipboard writes to the terminal clipboard via OSC52.
Use `ROTIDE_OSC52`:

- `auto` (default): enabled only when output looks compatible.
- `off`: disable OSC52 writes.
- `force`: always attempt OSC52 writes.

Large clipboard payloads above `ROTIDE_OSC52_MAX_COPY_BYTES` are skipped for
safety.

## Testing and validation

Run tests:

```bash
make test
```

Run sanitizer suite:

```bash
make test-sanitize
```

If LeakSanitizer is flaky in your local environment:

```bash
ASAN_OPTIONS=detect_leaks=0 make test-sanitize
```

## Project layout

- `rotide.c`: editor state init and main loop.
- `terminal.c`/`terminal.h`: terminal mode, key decoding, resize, OSC52, mouse.
- `buffer.c`/`buffer.h`: text buffer model, row ops, open/save, clipboard, history.
- `output.c`/`output.h`: screen rendering pipeline.
- `input.c`/`input.h`: prompts, movement, and key action dispatch.
- `keymap.c`/`keymap.h`: keymap defaults, TOML parsing, config loading, lookup.
- `alloc.c`/`alloc.h`: allocation wrappers and test hooks.
- `save_syscalls.c`/`save_syscalls.h`: save syscall wrappers/failure injection.
- `tests/`: unit and behavior tests.

## License

See `LICENSE`.
