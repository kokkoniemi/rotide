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
- Go LSP definition lookup (`Ctrl-]`) via `gopls`.
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
- `Ctrl-]`: Go definition (Go buffers)
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
- Backed by `rope.c` plus a line-start index (`document.c`).

### Rope

- Implemented in [`rope.c`](rope.c) / [`rope.h`](rope.h).
- Stores text in fixed-size chunks (currently 1024 bytes).
- Supports read/copy/dup/replace by byte range.

### Derived row cache (`struct erow`)

- Implemented from the document in `buffer.c`.
- Used for rendering and cursor/display conversions.
- Not the canonical storage path.

### Byte offset vs `(cy, cx, rx)`

- `cursor_offset` is the canonical cursor location in bytes.
- `cy`/`cx` are derived row/column coordinates.
- `rx` is rendered column (tabs/control escapes expanded).
- Mapping helpers:
  - `editorBufferPosToOffset`
  - `editorBufferOffsetToPos`
  - row render helpers in `buffer.c`/`output.c`

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
- Applied through one core mutation path in `buffer.c`, then row cache/syntax/LSP/history are updated.

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

- Per-tab `editorSyntaxState` in [`syntax.c`](syntax.c).
- Tree-sitter host parse plus optional HTML injections (JS/CSS).
- Query and parse budgets support graceful degraded modes instead of immediate hard disable for moderate file sizes.

### LSP state

- Go-only LSP client in [`lsp.c`](lsp.c) with JSON-RPC transport.
- Tracks per-tab document open/version and sends didOpen/didChange/didSave/didClose.
- Definition lookup integrates with tabs and position conversion helpers.

### Recovery snapshot

- Autosave/recovery persists tabs and text for crash recovery.
- Includes legacy compatibility path for older row-based recovery payloads, normalized into document-first state when restored.

## Module Map

- [`rotide.c`](rotide.c), [`rotide.h`](rotide.h): lifecycle, global state, startup wiring.
- [`terminal.c`](terminal.c): raw mode, key decoding, mouse packets, OSC52, terminal size.
- [`input.c`](input.c): action dispatch, prompts, search/go-to-line/go-to-definition, mouse handling.
- [`buffer.c`](buffer.c): canonical edit pipeline, tab state, drawer state transitions, save/recovery/history integration.
- [`document.c`](document.c), [`rope.c`](rope.c): canonical text storage and offset/line mapping.
- [`output.c`](output.c): rendering of tab bar, drawer, text viewport, status/message bars.
- [`syntax.c`](syntax.c): Tree-sitter parser/query integration and capture collection.
- [`lsp.c`](lsp.c): Go LSP process lifecycle and JSON-RPC messaging.
- [`keymap.c`](keymap.c): keymap/config parser and load precedence.
- [`alloc.c`](alloc.c), [`save_syscalls.c`](save_syscalls.c): testable wrappers for allocation and save syscalls.
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
