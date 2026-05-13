# RotIDE

RotIDE is a terminal text editor inspired by [kilo](https://github.com/antirez/kilo), focused on predictable behavior, explicit data flow, and strong test coverage.

## Status

RotIDE is under active development. Core editing, tabs, drawer navigation, search, undo/redo, Tree-sitter highlighting, crash recovery, LSP-backed definition lookup for Go/C/C++/HTML/CSS/JSON/JavaScript, an LSP Problems drawer, and incremental ESLint integration are implemented and tested.

![RotIDE editing its own source](docs/media/screenshots/editor-source.png)

## Screenshots

The full docs media set can be regenerated with `make docs-media`. To render only the screenshots that do not require language servers, run `make docs-media DOCS_MEDIA_FLAGS=--skip-lsp`. Feature screenshots use `github-dark`; theme screenshots use the named theme.

- [Project drawer](docs/media/screenshots/drawer-tree.png): file tree navigation over a full RotIDE fixture workspace.
- [Project search](docs/media/screenshots/project-search.png): project-wide text search results previewing matches in source.
- [JavaScript autocomplete](docs/media/screenshots/lsp-autocomplete-js.png): completion popup powered by `typescript-language-server`.
- [Clangd Problems drawer](docs/media/screenshots/lsp-clang-problems.png): C diagnostics from `clangd` in the LSP drawer.
- [Settings config](docs/media/screenshots/settings-config.png): editable `~/.rotide/config.toml` with the full documented settings file.
- [Git changes](docs/media/screenshots/git-changes.png): the Git drawer opening a generated diff tab for a modified source file.

Theme showcase:
[terminal](docs/media/screenshots/theme-terminal.png),
[a11y-dark](docs/media/screenshots/theme-a11y-dark.png),
[a11y-light](docs/media/screenshots/theme-a11y-light.png),
[acme](docs/media/screenshots/theme-acme.png),
[silentium](docs/media/screenshots/theme-silentium.png),
[256noir](docs/media/screenshots/theme-256noir.png),
[github-light](docs/media/screenshots/theme-github-light.png),
[github-dark](docs/media/screenshots/theme-github-dark.png),
[modus-operandi](docs/media/screenshots/theme-modus-operandi.png),
[modus-operandi-tinted](docs/media/screenshots/theme-modus-operandi-tinted.png),
[modus-vivendi](docs/media/screenshots/theme-modus-vivendi.png),
[modus-vivendi-tinted](docs/media/screenshots/theme-modus-vivendi-tinted.png),
[molokai](docs/media/screenshots/theme-molokai.png),
[kanagawa-wave](docs/media/screenshots/theme-kanagawa-wave.png),
[kanagawa-dragon](docs/media/screenshots/theme-kanagawa-dragon.png),
and [kanagawa-lotus](docs/media/screenshots/theme-kanagawa-lotus.png).

## Quick Start

Requirements:
- POSIX-like environment
- C compiler with C2x support

Build:

```bash
make
```

Build output is compact by default. To print full compiler/linker commands:

```bash
make V=1
```

Build a size-oriented release binary:

```bash
make release
```

The release target rebuilds `rotide` with size-oriented compiler/linker flags and strips unneeded symbols. The binary still includes the supported Tree-sitter grammars statically, so parser tables remain the dominant size cost.

Run:

```bash
./rotide README.md
```

Run tests:

```bash
make test
```

Run sanitizers:

```bash
make test-sanitize
```

If LeakSanitizer is flaky locally:

```bash
ASAN_OPTIONS=detect_leaks=0 make test-sanitize
```

## User-Facing Features

- UTF-8/grapheme-safe editing and cursor movement.
- Multi-tab workflow with preview tabs from drawer clicks.
- Project drawer with expand/collapse, mouse resize, and keyboard navigation.
- Search (`Ctrl-F`), go to line (`Ctrl-G`), matching bracket jump (`Ctrl-]`), selection/copy/cut/paste.
- Undo/redo with edit grouping.
- Tree-sitter syntax highlighting for:
  - C/C++ (`.c`, `.h`, `.cc`, `.cpp`, `.cxx`, `.c++`, `.hh`, `.hpp`, `.hxx`)
  - Go (`.go`, `go.mod`, `go.sum`)
  - Shell (`.sh`, rc files, extensionless shebang scripts)
  - HTML (`.html`, `.htm`, `.xhtml`) with JavaScript/CSS injection highlighting
  - JavaScript (`.js`, `.mjs`, `.cjs`, `.jsx`) with tree-sitter-jsdoc tag/type highlighting in doc comments
  - TypeScript (`.ts`, `.cts`, `.mts`) with tree-sitter-jsdoc tag/type highlighting in doc comments
  - TSX (`.tsx`) with JSX and tree-sitter-jsdoc tag/type highlighting in doc comments
  - CSS (`.css`, `.scss`)
  - JSON (`.json`, `.jsonc`)
  - Python (`.py`, `.pyi`, `.pyw`, extensionless shebang scripts)
  - PHP (`.php`, `.phtml`, `.php3`–`.php8`, `.phps`, extensionless shebang scripts)
  - Rust (`.rs`)
  - Java (`.java`)
  - C# (`.cs`, `.csx`)
  - Haskell (`.hs`, `.lhs`)
  - Ruby (`.rb`, `.rake`, `.gemspec`, `.ru`, `Rakefile`/`Gemfile`/`Guardfile`/`Capfile`/`Vagrantfile`, extensionless shebang scripts)
  - OCaml (`.ml`)
  - Julia (`.jl`)
  - Scala (`.scala`, `.sc`)
  - EJS (`.ejs`) with injected HTML, JavaScript, and nested HTML JavaScript/CSS highlighting
  - ERB (`.erb`) with injected HTML, Ruby, and nested HTML JavaScript/CSS highlighting
  - Markdown (`.md`, `.markdown`) with inline-grammar overlay for emphasis/links/code spans and fenced code-block injection routed by info-string
  - TOML (`.toml`, `.toml.example`)
  - YAML (`.yaml`, `.yml`, `.yaml.example`, `.yml.example`)
  - XML (`.xml`, `.svg`, `.xsd`, `.xslt`, `.xsl`, `.rng`)
  - Make (`.mk`, `.mak`, `Makefile`/`makefile`/`GNUmakefile`/`BSDmakefile`)
  - Diff (`.diff`, `.patch`, plus generated Git diff tabs)
  - Regex (`.regex`)
- Go LSP definition lookup (`Ctrl-O` or `Ctrl + left click`) via `gopls`.
- C/C++ LSP definition lookup (`Ctrl-O` or `Ctrl + left click`) via `clangd`.
- HTML LSP definition lookup (`Ctrl-O` or `Ctrl + left click`) via `~/.local/bin/vscode-html-language-server --stdio` by default.
- CSS/SCSS LSP definition lookup (`Ctrl-O` or `Ctrl + left click`) via `~/.local/bin/vscode-css-language-server --stdio` by default.
- JSON LSP definition lookup (`Ctrl-O` or `Ctrl + left click`) via `~/.local/bin/vscode-json-language-server --stdio` by default.
- JavaScript/JSX LSP definition lookup (`Ctrl-O` or `Ctrl + left click`) via `~/.local/bin/typescript-language-server --stdio` by default.
- ESLint diagnostics for active JavaScript buffers (`.js`, `.mjs`, `.cjs`, `.jsx`) via `~/.local/bin/vscode-eslint-language-server --stdio` by default.
- LSP drawer with clickable syntax and LSP problems for open tabs.
- Manual ESLint fix action (`eslint_fix`) for JavaScript buffers, configurable through `[keymap]`.
- Missing-`gopls` install prompt with live output in read-only task-log tabs.
- Missing-`typescript-language-server` install prompt with live output in read-only task-log tabs.
- Missing-`clangd` prompt that can open an instruction tab with install guidance and the official installation URL.
- Missing-`vscode-langservers-extracted` install prompt with live output in read-only task-log tabs.
- Atomic save flow (temp file + fsync + rename + cleanup).
- Crash recovery snapshots with restore prompt on startup.
- Optional OSC52 clipboard sync.

Syntax fixture samples are stored in [`tests/syntax/`](tests/syntax/README.md).

## Default Keybindings

- `Ctrl-S`: save
- `Ctrl-Q`: quit (confirm if dirty/task running)
- `Ctrl-N`: new tab
- `Ctrl-W`: close tab (confirm if dirty/task running)
- `Alt-Right` / `Alt-Left`: next/previous tab
- `Ctrl-E`: toggle focus between editor and drawer
- `Ctrl-\`: collapse/expand drawer
- `Alt-M`: open the main menu in the drawer
- `Alt-Shift-Left` / `Alt-Shift-Right`: resize drawer
- `Ctrl-P`: search files in the drawer
- `Ctrl-Alt-F`: search text across the project in the drawer
- `Ctrl-Alt-L`: show LSP Problems/Symbols drawer
- `Ctrl-F`: search
- `Ctrl-G`: go to line
- `Ctrl-]`: jump to matching bracket
- `Ctrl-O` / `Ctrl + left click`: Go/C/C++/HTML/CSS/SCSS/JSON/JavaScript definition (supported source buffers)
- `Alt-Z`: toggle soft line wrapping
- `Alt-N`: toggle absolute line numbers
- `Alt-H`: toggle current-line highlight
- `Ctrl-B`: toggle selection
- `Ctrl-C` / `Ctrl-X` / `Ctrl-D` / `Ctrl-V`: copy/cut/delete/paste selection
- `Ctrl-Z` / `Ctrl-Y`: undo/redo
- `Ctrl-Left` / `Ctrl-Right`: move by word
- arrows/home/end/page up/page down: movement and viewport navigation

`eslint_fix` is available as a configurable action but does not have a default binding in the built-in keymap.
Horizontal viewport scroll actions are still configurable as `scroll_left` and `scroll_right`.
Soft line wrapping is off by default and can also be enabled with `[editor] line_wrap = true`.
The cursor blinks by default; use `[editor] cursor_blink = false` for a steady cursor.
Line numbers and current-line highlighting are on by default; disable them with `[editor] line_numbers = false` and `[editor] current_line_highlight = false`.
Auto-indentation is off by default; enable it with `[editor] auto_indent = true`, then choose `[editor] indent_style = "spaces"` or `"tabs"` and `[editor] indent_width = 4`.

## Configuration

RotIDE reads TOML config from `~/.rotide/config.toml` only. On first launch it
auto-creates that file (and `~/.rotide`) with the documented defaults.

Project-local `.rotide.toml` files are **not** loaded — opening an untrusted
repo would otherwise let it override LSP server commands, keybindings, and
other settings. See [`config.toml.example`](config.toml.example) for a full
reference of available options; copy entries into `~/.rotide/config.toml` to
customize.

Sections:
- `[editor]` (for example `cursor_style`, `cursor_blink`, `line_wrap`, `line_numbers`, `current_line_highlight`, `auto_indent`, `indent_style`, `indent_width`)
- `[theme]`
- `[lsp]`
- `[keymap]`

Theme notes:
- `[theme] name = "terminal"` is the default and follows the terminal ANSI palette.
- Built-in themes: `terminal`, `a11y-dark`, `a11y-light`, `acme`, `silentium`,
  `256noir`, `github-light`, `github-dark`, `modus-operandi`,
  `modus-operandi-tinted`, `modus-vivendi`, `modus-vivendi-tinted`, `molokai`,
  `kanagawa-wave`, `kanagawa-dragon`, and `kanagawa-lotus`
  (`kanagawa` is an alias for `kanagawa-wave`).
- Custom themes live at `~/.rotide/themes/<name>.toml`.
- Theme files may define `name`, optional `inherits` from any built-in theme,
  `[theme.syntax]`, and `[theme.ui]`.
- Color values in theme files support `default`, ANSI names such as `bright_blue`, and true-color hex such as `"#6BBEFF"`.
- The Modus built-ins use the upstream CC0 Modus palette mappings from
  <https://protesilaos.com/emacs/modus-themes-colors>.
- The GitHub built-ins use restrained mappings from GitHub Primer's light/dark
  color and syntax tokens.
- The `acme` built-in uses the small Plan 9 Acme-inspired palette from
  <https://github.com/plan9-for-vimspace/acme-colors>.
- The `silentium` built-in uses the monochrome-plus-accent palette from
  <https://github.com/silentium-theme/silentium.nvim>.
- The `256noir` built-in uses the xterm 256-color grayscale palette from
  <https://github.com/andreasvc/vim-256noir>.
- The `molokai` built-in mirrors Tomas Restrepo's Vim port of Molokai
  (<https://github.com/tomasr/molokai>).
- The `kanagawa-*` built-ins port the Kanagawa palette from
  <https://github.com/rebelot/kanagawa.nvim> (Wave dark, Dragon dark, Lotus light).

LSP notes:
- `gopls_enabled`, `clangd_enabled`, `html_enabled`, `css_enabled`, `json_enabled`, `javascript_enabled`, and `eslint_enabled` can be set independently in `[lsp]`.
- `gopls_command`, `clangd_command`, `html_command`, `css_command`, `json_command`, `javascript_command`, and `eslint_command` are read from `~/.rotide/config.toml`.
- `gopls_install_command`, `javascript_install_command`, and
  `vscode_langservers_install_command` are also configured globally in
  `~/.rotide/config.toml`.
- Legacy `enabled = true|false` is accepted as a shorthand that toggles all built-in LSP servers together.
- HTML definition lookup uses `~/.local/bin/vscode-html-language-server --stdio` by default.
- CSS/SCSS definition lookup uses `~/.local/bin/vscode-css-language-server --stdio` by default.
- JSON definition lookup uses `~/.local/bin/vscode-json-language-server --stdio` by default.
- JavaScript/JSX definition lookup uses `~/.local/bin/typescript-language-server --stdio` by default.
- ESLint diagnostics use `~/.local/bin/vscode-eslint-language-server --stdio` by default.
- If `typescript-language-server` is missing, RotIDE offers to run:
  - `npm install --global --prefix ~/.local typescript typescript-language-server`
- If `vscode-html-language-server` is missing, RotIDE offers to run:
  - `npm install --global --prefix ~/.local vscode-langservers-extracted`
- The same install prompt is reused for `vscode-css-language-server`, `vscode-json-language-server`, and `vscode-eslint-language-server`.
- If `~/.local/bin` is already on your `PATH`, you can also set:
  - `html_command = "vscode-html-language-server --stdio"`
  - `css_command = "vscode-css-language-server --stdio"`
  - `json_command = "vscode-json-language-server --stdio"`
  - `javascript_command = "typescript-language-server --stdio"`
  - `eslint_command = "vscode-eslint-language-server --stdio"`
- The `vscode-langservers-extracted` package provides:
  - `vscode-html-language-server`
  - `vscode-css-language-server`
  - `vscode-json-language-server`
  - `vscode-eslint-language-server`
- JavaScript definition lookup uses `typescript-language-server`, while ESLint remains the diagnostics and fix provider for JavaScript buffers.
- ESLint integration is intentionally incremental in this phase:
  - diagnostics are shown for the active JavaScript-family buffer
  - fixes are user-invoked through the `eslint_fix` action
  - save behavior is unchanged
- If `clangd` is missing, RotIDE shows install guidance in a task-log tab instead of trying to install it automatically.
- For most C/C++ projects, `clangd` also needs a `compile_commands.json` compilation database.
- With CMake, generate one with:
  - `cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`
  - then use `build/compile_commands.json`, or copy/symlink it into the project root
- Without CMake, `Bear` is a good option:
  - `bear -- make`
  - or `bear -- <your normal build command>`
  - this is often a good fit for pure C projects
- Default install command:
  - `go install golang.org/x/tools/gopls@latest`
  - `npm install --global --prefix ~/.local typescript typescript-language-server`
  - `npm install --global --prefix ~/.local vscode-langservers-extracted`

See [`config.toml.example`](config.toml.example) for a complete action/key example.

## Architecture and Terminology

This section names the core concepts used throughout the codebase.

### `editorDocument` (canonical text model)

- The canonical source of truth for tab text.
- Owned in `editorConfig`/`editorTabState` as `document`.
- Backed by `src/text/rope.c` plus a line-start index (`src/text/document.c`).

### Rope

- Implemented in [`src/text/rope.c`](src/text/rope.c) / [`src/text/rope.h`](src/text/rope.h).
- Stores text in fixed-size chunks (currently 1024 bytes).
- Supports read/copy/dup/replace by byte range.

### Derived row cache (`struct erow`)

- Implemented from the document in `src/editing/buffer_core.c`.
- Used for rendering and cursor/display conversions.
- Not the canonical storage path.

### Byte offset vs `(cy, cx, rx)`

- `cursor_offset` is the canonical cursor location in bytes.
- `cy`/`cx` are derived row/column coordinates.
- `rx` is rendered column (tabs/control escapes expanded).
- Mapping helpers:
  - `editorBufferPosToOffset`
  - `editorBufferOffsetToPos`
  - row render helpers in `src/editing/buffer_core.c`/`src/render/screen.c`

### `editorTextSource`

- Shared read interface (`read(context, byte_index)`) over active text.
- Used by syntax and LSP without requiring permanent flattened text copies.

### Edit pipeline

- High-level edits are represented as document edits with:
  - start offset
  - removed length/text
  - inserted text
  - before/after cursor offsets
  - before/after dirty values
- Applied through one core mutation path in `src/editing/buffer_core.c`, then row cache/syntax/LSP/history are updated.

### Operation history (undo/redo)

- History entries are operations, not full buffer snapshots.
- Entries store removed/inserted slices and cursor/dirty before/after metadata.
- Typed runs may coalesce; redo invalidates on divergent edit.

### Tab kinds

- `EDITOR_TAB_FILE`: normal file tabs (editable, savable).
- `EDITOR_TAB_TASK_LOG`: generated read-only tabs for command output (not savable).
- File tabs can be marked preview (`is_preview`) and later pinned.

### Task log tabs

- Used for one-shot background tasks (for example installing `gopls`).
- Stream merged stdout/stderr output live.
- Remain open after completion with final status line.

### Viewport modes

- `EDITOR_VIEWPORT_FOLLOW_CURSOR`: keeps cursor visible.
- `EDITOR_VIEWPORT_FREE_SCROLL`: mouse/page/ctrl-arrow scrolling can move view without moving cursor.

### Syntax state

- Per-tab `editorSyntaxState` in [`src/language/syntax.c`](src/language/syntax.c).
- Table-driven language registry in [`src/language/languages.c`](src/language/languages.c) owns parser factories, query bundles, filename/shebang detection, and injection aliases.
- Tree-sitter host parse plus generic tab-local injection trees for nested highlighting (HTML, JavaScript, TypeScript, TSX, PHP, C++, Haskell, Julia, EJS, ERB, and Markdown host grammars all ship injection queries; nested injections are capped at depth 3 and 16 active injected trees per tab).
- Query and parse budgets support graceful degraded modes instead of immediate hard disable for moderate file sizes.

### LSP state

- LSP clients in [`src/language/lsp.c`](src/language/lsp.c) with JSON-RPC transport for `gopls`, `clangd`, `typescript-language-server`, and the `vscode-langservers-extracted` HTML/CSS/JSON/ESLint servers.
- Tracks per-tab document open/version and sends didOpen/didChange/didSave/didClose.
- Stores per-tab diagnostic summaries for active-buffer ESLint results.
- Shows open-tab diagnostics and Tree-sitter parse errors in the LSP drawer.
- Definition lookup and ESLint code actions integrate with tabs and position conversion helpers.

### Source tree

- First-party runtime code lives under [`src/`](src/).
- Top-level files are primarily project metadata, build files, docs, tests, and vendored dependencies.
- Subdirectories inside `src/` are responsibility-oriented:
  - `src/workspace/`: tabs, drawer, recovery, and task-log workspace behavior
  - `src/support/`: terminal, allocation, save, and file/path helpers
  - `src/text/`: reusable UTF-8, grapheme, and row/render helpers

### Recovery snapshot

- Autosave/recovery persists tabs and text for crash recovery.
- Recovery snapshots use the current document-first format (`RTRECOV1` version `2`).

## Module Map

- [`src/rotide.c`](src/rotide.c), [`src/rotide.h`](src/rotide.h): lifecycle, global state, startup wiring.
- [`src/support/terminal.c`](src/support/terminal.c): raw mode, key decoding, mouse packets, OSC52, terminal size.
- [`src/input/dispatch.c`](src/input/dispatch.c): action dispatch, prompts, search/go-to-line/go-to-definition, mouse handling.
- [`src/editing/buffer_core.c`](src/editing/buffer_core.c): canonical edit pipeline, selection/history/edit orchestration, save integration.
- [`src/text/document.c`](src/text/document.c), [`src/text/rope.c`](src/text/rope.c): canonical text storage and offset/line mapping.
- [`src/render/screen.c`](src/render/screen.c): rendering of tab bar, drawer, text viewport, status/message bars.
- [`src/language/syntax.c`](src/language/syntax.c): Tree-sitter parser/query integration and capture collection.
- [`src/language/lsp.c`](src/language/lsp.c): Go/C/C++/HTML/CSS/JSON/JavaScript/ESLint LSP process lifecycle, JSON-RPC messaging, diagnostics, and code actions.
- [`src/config/`](src/config): keymap bindings, editor settings, theme config, LSP config, and shared TOML parsing helpers.
- [`src/support/alloc.c`](src/support/alloc.c), [`src/support/save_syscalls.c`](src/support/save_syscalls.c): testable wrappers for allocation and save syscalls.
- [`src/workspace/`](src/workspace): editor subsystems split out of the former monolithic buffer module.
- [`src/text/`](src/text): shared UTF-8, grapheme, and row/render helpers.
- [`tests/`](tests): behavior and regression tests, split per concern (`test_syntax.c`, `test_lsp.c`, `test_render_terminal.c`, `test_document_text_editing.c`, `test_save_recovery.c`, `test_input_search.c`, `test_workspace_config.c`).

## Tree-sitter Vendor Workflow

Pinned grammar/runtime metadata is in:
- [`vendor/tree_sitter/VERSIONS.env`](vendor/tree_sitter/VERSIONS.env)
- [`vendor/tree_sitter/VERSIONS.md`](vendor/tree_sitter/VERSIONS.md)

Refresh vendored runtime/grammars and parser artifacts:

```bash
./scripts/refresh_tree_sitter_vendor.sh
```

## CI

GitHub Actions runs:
- `make`
- `make test`
- `make test-sanitize`

Workflow file: [`.github/workflows/ci.yml`](.github/workflows/ci.yml)

## License

See [`LICENSE`](LICENSE).
