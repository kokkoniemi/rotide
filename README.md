# RotIDE

RotIDE is a terminal text editor inspired by [kilo](https://github.com/antirez/kilo), focused on predictable behavior, explicit data flow, and strong test coverage.

## Status

RotIDE is under active development. Core editing, tabs, drawer navigation, search, undo/redo, Tree-sitter highlighting, crash recovery, and Go definition lookup are implemented and tested.

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
  - C (`.c`, `.h`)
  - Go (`.go`, `go.mod`, `go.sum`)
  - Shell (`.sh`, rc files, extensionless shebang scripts)
  - HTML (`.html`, `.htm`, `.xhtml`)
  - JavaScript (`.js`, `.mjs`, `.cjs`, `.jsx`)
  - CSS (`.css`, `.scss`)
- Go LSP definition lookup (`Ctrl-O`) via `gopls`.
- Missing-`gopls` install prompt with live output in read-only task-log tabs.
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
- `Ctrl-O`: Go definition (Go buffers)
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
- `gopls_command` can be set globally or per-project.
- `gopls_install_command` is **global-only** (`~/.rotide/config.toml`).
- If `gopls_install_command` appears in project config, RotIDE ignores that key and keeps parsing the rest of `[lsp]`.
- Default install command:
  - `go install golang.org/x/tools/gopls@latest`

See [`.rotide.toml`](.rotide.toml) for a complete action/key example.

## Architecture and Terminology

This section names the core concepts used throughout the codebase.

### `editorDocument` (canonical text model)

- The canonical source of truth for tab text.
- Owned in `editorConfig`/`editorTabState` as `document`.
- Backed by `src/rope.c` plus a line-start index (`src/document.c`).

### Rope

- Implemented in [`src/rope.c`](src/rope.c) / [`src/rope.h`](src/rope.h).
- Stores text in fixed-size chunks (currently 1024 bytes).
- Supports read/copy/dup/replace by byte range.

### Derived row cache (`struct erow`)

- Implemented from the document in `src/buffer.c`.
- Used for rendering and cursor/display conversions.
- Not the canonical storage path.

### Byte offset vs `(cy, cx, rx)`

- `cursor_offset` is the canonical cursor location in bytes.
- `cy`/`cx` are derived row/column coordinates.
- `rx` is rendered column (tabs/control escapes expanded).
- Mapping helpers:
  - `editorBufferPosToOffset`
  - `editorBufferOffsetToPos`
  - row render helpers in `src/buffer.c`/`src/output.c`

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
- Applied through one core mutation path in `src/buffer.c`, then row cache/syntax/LSP/history are updated.

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

- Per-tab `editorSyntaxState` in [`src/syntax.c`](src/syntax.c).
- Tree-sitter host parse plus optional HTML injections (JS/CSS).
- Query and parse budgets support graceful degraded modes instead of immediate hard disable for moderate file sizes.

### LSP state

- Go-only LSP client in [`src/lsp.c`](src/lsp.c) with JSON-RPC transport.
- Tracks per-tab document open/version and sends didOpen/didChange/didSave/didClose.
- Definition lookup integrates with tabs and position conversion helpers.

### Source tree

- First-party runtime code lives under [`src/`](src/).
- Top-level files are primarily project metadata, build files, docs, tests, and vendored dependencies.
- Subdirectories inside `src/` are responsibility-oriented:
  - `src/editor/`: stateful editor subsystems such as tabs, drawer, recovery, and file I/O
  - `src/text/`: reusable UTF-8, grapheme, and row/render helpers

### Recovery snapshot

- Autosave/recovery persists tabs and text for crash recovery.
- Recovery snapshots use the current document-first format (`RTRECOV1` version `2`).

## Module Map

- [`src/rotide.c`](src/rotide.c), [`src/rotide.h`](src/rotide.h): lifecycle, global state, startup wiring.
- [`src/terminal.c`](src/terminal.c): raw mode, key decoding, mouse packets, OSC52, terminal size.
- [`src/input.c`](src/input.c): action dispatch, prompts, search/go-to-line/go-to-definition, mouse handling.
- [`src/buffer.c`](src/buffer.c): canonical edit pipeline, selection/history/edit orchestration, save integration.
- [`src/document.c`](src/document.c), [`src/rope.c`](src/rope.c): canonical text storage and offset/line mapping.
- [`src/output.c`](src/output.c): rendering of tab bar, drawer, text viewport, status/message bars.
- [`src/syntax.c`](src/syntax.c): Tree-sitter parser/query integration and capture collection.
- [`src/lsp.c`](src/lsp.c): Go LSP process lifecycle and JSON-RPC messaging.
- [`src/keymap.c`](src/keymap.c): keymap/config parser and load precedence.
- [`src/alloc.c`](src/alloc.c), [`src/save_syscalls.c`](src/save_syscalls.c): testable wrappers for allocation and save syscalls.
- [`src/editor/`](src/editor): editor subsystems split out of the former monolithic buffer module.
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
