# RotIDE Code Map

## Core architecture

- Canonical text storage:
  - `document.c` / `document.h` (`editorDocument`)
  - `rope.c` / `rope.h` (chunked byte storage)
- Derived render/cache state:
  - `struct erow` arrays in `buffer.c`/`rotide.h`
- Shared byte-read abstraction:
  - `struct editorTextSource` in `rotide.h`
  - built by `editorBuildActiveTextSource()` in `buffer.c`

## Major modules

- `rotide.c` / `rotide.h`
  - global editor state (`E`), startup, config status messaging, main loop
- `terminal.c`
  - raw mode, key decoding, mouse packet parsing, OSC52 helpers, terminal sizing
- `input.c`
  - action dispatch, prompts, search flow, go-to-line/definition, mouse interactions
- `buffer.c`
  - document edit application, row-cache rebuild, tab/drawer transitions, save/recovery/history
- `output.c`
  - full-screen render pipeline and viewport/cursor scroll behavior
- `keymap.c`
  - keymap defaults/parsing plus editor/theme/LSP config TOML loaders
- `syntax.c`
  - Tree-sitter parser/query loading, captures, predicates/locals/injections, performance budgets
- `lsp.c`
  - Go LSP client lifecycle, JSON-RPC wire protocol, didOpen/didChange/didSave/didClose + definition

## High-signal workflows

### Text edit path

1. User action in `input.c`.
2. Edit descriptor prepared in `buffer.c`.
3. `editorApplyDocumentEdit(...)` mutates document.
4. Row cache rebuilt from document.
5. Cursor/offset state updated.
6. Syntax incremental/full update emitted.
7. LSP didChange notification emitted.
8. Undo/redo operation entry recorded.

### Search path

1. `editorFind()` opens callback prompt.
2. `editorFindCallback()` chooses direction/start position.
3. `editorBufferFindForward/Backward()` search through active text source.
4. Active match stored as offset + length.
5. Renderer highlights active match span.

### Task-log path

1. Task start creates `EDITOR_TAB_TASK_LOG`.
2. Command runs as child process (stdout/stderr merged).
3. Output appended to task-log document and rows rebuilt.
4. Final status line appended on completion/failure.

### Recovery path

1. Autosave writes session snapshot.
2. Restore loads snapshot, normalizes text into document.
3. Active/tab states rebuilt from document-first restore flow.
4. Legacy row-format snapshots are supported through normalization.

## Common change recipes

### Update cursor/search/selection behavior

- Touch: `input.c`, `buffer.c`, `output.c`, `tests/rotide_tests.c`
- Keep:
  - offset-first invariants (`cursor_offset`, selection/search offsets)
  - UTF-8/grapheme clamping
  - non-edit actions not changing dirty state

### Update syntax behavior

- Touch: `syntax.c`, `buffer.c`, `output.c`, tests + vendor query files if needed
- Keep:
  - tab-local syntax state
  - budget/degraded mode behavior
  - selection/search overlay precedence

### Update LSP behavior

- Touch: `lsp.c`, `input.c`, `buffer.c`, `keymap.c` (for `[lsp]` config), tests
- Keep:
  - Go-only gating semantics
  - proper open/change/save/close sequencing
  - install prompt/task-log behavior for missing `gopls`

## Validation baseline

- `make`
- `make test`
- `make test-sanitize` for sensitive paths
