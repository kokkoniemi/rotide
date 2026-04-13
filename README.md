# RotIDE

RotIDE is a terminal text editor inspired by [kilo](https://github.com/antirez/kilo), focused on predictable behavior, explicit data flow, and strong test coverage.

## Status

RotIDE is under active development. Core editing, tabs, drawer navigation, search, undo/redo, Tree-sitter highlighting, crash recovery, and Go/C/C++ definition lookup are implemented and tested.

## Quick Start

Requirements:
- POSIX-like environment
- C compiler with C2x support

Build:

```bash
make
```

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
- Search (`Ctrl-F`), go to line (`Ctrl-G`), selection/copy/cut/paste.
- Undo/redo with edit grouping.
- Tree-sitter syntax highlighting for:
  - C/C++ (`.c`, `.h`, `.cc`, `.cpp`, `.cxx`, `.c++`, `.hh`, `.hpp`, `.hxx`)
  - Go (`.go`, `go.mod`, `go.sum`)
  - Shell (`.sh`, rc files, extensionless shebang scripts)
  - HTML (`.html`, `.htm`, `.xhtml`)
  - JavaScript (`.js`, `.mjs`, `.cjs`, `.jsx`)
  - CSS (`.css`, `.scss`)
- Go LSP definition lookup (`Ctrl-O` or `Ctrl + left click`) via `gopls`.
- C/C++ LSP definition lookup (`Ctrl-O` or `Ctrl + left click`) via `clangd`.
- HTML LSP definition lookup (`Ctrl-O` or `Ctrl + left click`) via `~/.local/bin/vscode-html-language-server --stdio` by default.
- Missing-`gopls` install prompt with live output in read-only task-log tabs.
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
- `Ctrl-E`: focus drawer
- `Ctrl-\`: collapse/expand drawer
- `Alt-Shift-Left` / `Alt-Shift-Right`: resize drawer
- `Ctrl-F`: search
- `Ctrl-G`: go to line
- `Ctrl-O` / `Ctrl + left click`: Go/C/C++/HTML definition (supported source buffers)
- `Ctrl-B`: toggle selection
- `Ctrl-C` / `Ctrl-X` / `Ctrl-D` / `Ctrl-V`: copy/cut/delete/paste selection
- `Ctrl-Z` / `Ctrl-Y`: undo/redo
- `Ctrl-Left` / `Ctrl-Right`: horizontal viewport scroll
- arrows/home/end/page up/page down: movement and viewport navigation

## Configuration

RotIDE reads TOML configs in this order (low to high precedence):
1. built-in defaults
2. `~/.rotide/config.toml`
3. `./.rotide.toml`

Sections:
- `[editor]` (for example `cursor_style`)
- `[theme.syntax]`
- `[lsp]`
- `[keymap]`

LSP notes:
- `gopls_enabled`, `clangd_enabled`, and `html_enabled` can be set independently in `[lsp]`.
- `gopls_command`, `clangd_command`, and `html_command` can be set globally or per-project.
- `gopls_install_command` is **global-only** (`~/.rotide/config.toml`).
- `vscode_langservers_install_command` is **global-only** (`~/.rotide/config.toml`).
- If `gopls_install_command` appears in project config, RotIDE ignores that key and keeps parsing the rest of `[lsp]`.
- If `vscode_langservers_install_command` appears in project config, RotIDE ignores that key and keeps parsing the rest of `[lsp]`.
- Legacy `enabled = true|false` is accepted as a shorthand that toggles both servers together.
- HTML definition lookup uses `~/.local/bin/vscode-html-language-server --stdio` by default.
- If `vscode-html-language-server` is missing, RotIDE offers to run:
  - `npm install --global --prefix ~/.local vscode-langservers-extracted`
- If `~/.local/bin` is already on your `PATH`, you can also set:
  - `html_command = "vscode-html-language-server --stdio"`
- The `vscode-langservers-extracted` package also provides future server commands:
  - `vscode-css-language-server`
  - `vscode-json-language-server`
  - `vscode-eslint-language-server`
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
  - `npm install --global --prefix ~/.local vscode-langservers-extracted`

See [`.rotide.toml`](.rotide.toml) for a complete action/key example.

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
- Tree-sitter host parse plus optional HTML injections (JS/CSS).
- Query and parse budgets support graceful degraded modes instead of immediate hard disable for moderate file sizes.

### LSP state

- LSP client in [`src/language/lsp.c`](src/language/lsp.c) with JSON-RPC transport for `gopls` and `clangd`.
- Tracks per-tab document open/version and sends didOpen/didChange/didSave/didClose.
- Definition lookup integrates with tabs and position conversion helpers.

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
- [`src/language/lsp.c`](src/language/lsp.c): Go/C/C++ LSP process lifecycle and JSON-RPC messaging.
- [`src/config/`](src/config): keymap bindings, editor settings, theme config, LSP config, and shared TOML parsing helpers.
- [`src/support/alloc.c`](src/support/alloc.c), [`src/support/save_syscalls.c`](src/support/save_syscalls.c): testable wrappers for allocation and save syscalls.
- [`src/workspace/`](src/workspace): editor subsystems split out of the former monolithic buffer module.
- [`src/text/`](src/text): shared UTF-8, grapheme, and row/render helpers.
- [`tests/rotide_tests.c`](tests/rotide_tests.c): behavior and regression tests.

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
