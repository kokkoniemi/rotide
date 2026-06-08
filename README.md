# RotIDE

RotIDE is a terminal text editor that began with kilo-style minimalism and now
focuses on predictable behavior, explicit data flow, and strong regression
coverage. Its earliest shape was inspired by antirez's
[kilo](https://github.com/antirez/kilo).

## Status

RotIDE is under active development. Core editing, tabs, drawer navigation,
search, undo/redo, Tree-sitter highlighting, crash recovery, LSP-backed
definition lookup, an LSP Problems drawer, and incremental ESLint integration
are implemented and tested.

![RotIDE editing its own source](docs/media/screenshots/editor-source.png)

## Screenshots

See [docs/media/screenshots/](docs/media/screenshots/README.md) for the feature
and theme showcase. Regenerate screenshots with:

```bash
make docs-media
```

Use `make docs-media DOCS_MEDIA_FLAGS=--skip-lsp` to skip language-server
scenes.

## Quick Start

Requirements:

- POSIX-like environment
- C compiler with C2x support

Build and run:

```bash
make
./build/rotide README.md
```

Run tests:

```bash
make test
make test-sanitize
```

If LeakSanitizer is flaky locally:

```bash
ASAN_OPTIONS=detect_leaks=0 make test-sanitize
```

Build a size-oriented release binary:

```bash
make release
```

Use `make V=1` to print full compiler and linker commands.

## Features

- UTF-8/grapheme-safe editing and cursor movement.
- Multi-tab workflow with preview tabs from drawer clicks.
- Project drawer with expand/collapse, mouse resize, file search, project text
  search, Git changes, and LSP Problems/Symbols views.
- Search, go to line, matching bracket jump, selection/copy/cut/paste.
- Undo/redo with edit grouping.
- Selectable editing-input systems via `[input] system`: Vim (default — modal
  editing with motions, operators, counts, registers, search, text objects, and
  an ex command line; the current mode shows in the status bar) or CUA.
- Configurable keymap and editor settings (per-system `[keymap.cua]` /
  `[keymap.vim]`).
- Atomic save flow and crash recovery snapshots.
- Optional OSC52 clipboard sync.
- Built-in themes plus custom TOML themes.
- Tree-sitter syntax highlighting for C/C++, Go, Shell, HTML, JavaScript,
  TypeScript, TSX, CSS/SCSS, JSON/JSONC, Python, PHP, Rust, Java, C#,
  Haskell, Ruby, OCaml, Julia, Scala, EJS, ERB, Markdown, TOML, YAML, XML,
  Make, Diff, Regex, and LaTeX.
- LSP definition lookup for Go, C/C++, HTML, CSS/SCSS, JSON, and JavaScript.
- LSP-backed document symbols, problems drawer, and autocomplete where enabled.
- ESLint diagnostics and manual `eslint_fix` action for JavaScript buffers.
- Missing-language-server install/help prompts with read-only task-log output.

Syntax fixture samples are stored in [`tests/syntax/`](tests/syntax/README.md).

## Default Keybindings

These are the CUA system's bindings (`[input] system = "cua"`). The default
system is Vim, whose modal keys (motions, operators, `:` ex commands, etc.) are
summarized in [`docs/developer/input-systems.md`](docs/developer/input-systems.md);
many of these chords (save/quit/tabs/drawer/panes) still apply in Vim too.

- `Ctrl-S`: save
- `Ctrl-Q`: quit, confirming when dirty or a task is running
- `Ctrl-N`: new tab
- `Ctrl-W`: close tab
- `Alt-Right` / `Alt-Left`: next/previous tab
- `Ctrl-Shift-Alt-Left` / `-Right` / `-Up` / `-Down`: move the active tab to the neighbouring pane
- `Ctrl-E`: toggle focus between editor and drawer
- `Ctrl-B`: toggle the drawer (collapse/expand the sidebar)
- `Alt-M`: open the main menu in the drawer
- `Ctrl-P`: search files in the drawer
- `Ctrl-Alt-F`: search text across the project
- `Ctrl-Alt-L`: show LSP Problems/Symbols drawer
- `Ctrl-F`: search
- `Ctrl-G`: go to line
- `Ctrl-]`: jump to matching bracket
- `Ctrl-O` / `Ctrl + left click`: go to definition for supported source buffers
- `Alt-Z`: toggle soft line wrapping
- `Alt-N`: toggle absolute line numbers
- `Alt-H`: toggle current-line highlight
- `Ctrl-A`: select all
- `Shift-Arrow`: extend selection (a plain arrow collapses it); `Ctrl-Shift-Left`/`-Right` extend by word; `Shift-Home`/`Shift-End` extend to line start/end
- `Alt-Shift-Arrow`: column (box) selection
- `Ctrl-C` / `Ctrl-X` / `Ctrl-D` / `Ctrl-V`: copy/cut/delete/paste selection
- `Ctrl-Z` / `Ctrl-Y`: undo/redo
- `Ctrl-Left` / `Ctrl-Right`: move by word
- `Ctrl-Up` / `Ctrl-Down`: scroll the viewport up/down without moving the cursor
- arrows, home/end, page up/page down: movement and viewport navigation

`eslint_fix`, `scroll_left`, `scroll_right`, `toggle_selection` (modal keyboard
selection, superseded by Shift+move), and `resize_drawer_narrow`/
`resize_drawer_widen` are configurable actions without default bindings in the
built-in keymap.


## Configuration

RotIDE reads TOML config from `~/.rotide/config.toml` only. On first launch it
auto-creates that file, and `~/.rotide`, with the documented defaults.

Project-local `.rotide.toml` files are not loaded for general editor/LSP
settings. Opening an untrusted repo should not let it override LSP server
commands, keybindings, or other settings.

See [`config.toml.example`](config.toml.example) for the complete option list.
Common sections:

- `[editor]`: cursor style/blink, line wrap, line numbers, current-line
  highlight, indentation, column selection drag modifier.
- `[theme]`: built-in or custom theme selection.
- `[lsp]`: language-server enable flags, commands, install commands, and
  autocomplete settings.
- `[input]`: active editing-input system (`vim`, the default, or `cua`).
- `[keymap.cua]`: CUA action bindings.
- `[keymap.vim]`: mode-qualified Vim bindings (`normal.*`, `insert.*`,
  `visual.*`).

Built-in themes include `terminal`, `a11y-dark`, `a11y-light`, `acme`,
`silentium`, `256noir`, `github-light`, `github-dark`, `modus-operandi`,
`modus-operandi-tinted`, `modus-vivendi`, `modus-vivendi-tinted`, `molokai`,
`kanagawa-wave`, `kanagawa-dragon`, and `kanagawa-lotus`.

## Developer Documentation

Architecture and maintenance docs live under
[`docs/developer/`](docs/developer/README.md):

- [Architecture](docs/developer/architecture.md)
- [Workflows](docs/developer/workflows.md)
- [Build and tests](docs/developer/build-and-tests.md)

PlantUML diagram sources live under [`docs/diagrams/src/`](docs/diagrams/src/)
and committed SVGs live under [`docs/diagrams/svg/`](docs/diagrams/svg/).
Regenerate them with:

```bash
make docs-diagrams
```

## CI

GitHub Actions runs:

- `make`
- `make test`
- `make test-sanitize`

Workflow file: [`.github/workflows/ci.yml`](.github/workflows/ci.yml)

## License

See [`LICENSE`](LICENSE).
