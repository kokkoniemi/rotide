# RotIDE Code Map

Source lives under `src/<area>/`; tests are split per concern under `tests/test_*.c`.

## Core architecture

- Canonical text storage:
  - `src/text/document.c` / `document.h` (`editorDocument`)
  - `src/text/rope.c` / `rope.h` (chunked byte storage)
  - `src/text/row.c` / `row.h` (derived `erow` cache)
  - `src/text/utf8.c` / `utf8.h` (encoding helpers)
- Shared byte-read abstraction:
  - `struct editorTextSource` declared in `src/rotide.h`
  - built by `editorBuildActiveTextSource()` in `src/editing/buffer_core.c`

## Major modules

- `src/rotide.c` / `src/rotide.h`
  - global editor state (`E`), startup, config status messaging, main loop
- `src/support/`
  - `terminal.c` — raw mode, key decoding, mouse packet parsing, OSC52, sizing
  - `alloc.c` — allocation hooks/budget plumbing
  - `file_io.c` — atomic save helpers
  - `save_syscalls.c` — syscall seam used by save/recovery tests
- `src/input/dispatch.c`
  - action dispatch, prompts, search flow, go-to-line/definition, mouse interactions
- `src/editing/`
  - `buffer_core.c` — document edit application, row-cache rebuild, tab/drawer transitions, save coordination
  - `edit.c` — edit descriptor construction and apply path
  - `selection.c` — selection/copy/cut/paste primitives
  - `history.c` — undo/redo grouping
- `src/render/screen.c`
  - full-screen render pipeline, viewport/cursor scroll, syntax/selection/search overlay precedence
- `src/workspace/`
  - `tabs.c` — tab lifecycle and active-tab transitions
  - `drawer.c` — file-tree drawer
  - `recovery.c` — autosave snapshots and document-first restore
  - `task.h` — task-log API used by long-running child processes
- `src/config/`
  - `keymap.c` — keymap defaults/parsing
  - `editor_config.c`, `theme_config.c`, `lsp_config.c` — TOML loaders for `[editor]`, `[theme]`, `[lsp]`
  - `common.c` — shared config plumbing (global vs project precedence)
- `src/language/`
  - `syntax.c` — Tree-sitter parser/host parse, incremental edits, injection orchestration, performance budgets
  - `syntax_queries.c` — query cache, fallback query strings, predicate/locals/injection metadata
  - `lsp.c` — multi-server LSP lifecycle and routing (`gopls`, `clangd`, `vscode-html-language-server`, `vscode-css-language-server`, `vscode-json-language-server`, `typescript-language-server`, `vscode-eslint-language-server`); server kinds in `lsp_internal.h`
  - `lsp_protocol.c` — JSON-RPC encoding and message dispatch
  - `lsp_transport.c` — child-process transport layer

## High-signal workflows

### Text edit path

1. User action in `src/input/dispatch.c`.
2. Edit descriptor prepared in `src/editing/edit.c`.
3. `editorApplyDocumentEdit(...)` (in `src/editing/buffer_core.c`) mutates document.
4. Row cache rebuilt from document (`src/text/row.c`).
5. Cursor/offset/selection state updated.
6. Syntax incremental/full update emitted (`src/language/syntax.c`).
7. LSP didChange notification routed to the active server (`src/language/lsp.c`).
8. Undo/redo operation entry recorded (`src/editing/history.c`).

### Search path

1. `editorFind()` opens callback prompt.
2. `editorFindCallback()` chooses direction/start position.
3. Forward/backward search runs through the active text source.
4. Active match stored as offset + length.
5. Renderer highlights the active match span (`src/render/screen.c`).

### Task-log path

1. Task start creates `EDITOR_TAB_TASK_LOG` (API in `src/workspace/task.h`).
2. Command runs as a child process with stdout/stderr merged.
3. Output appended to the task-log document and rows rebuilt.
4. Final status line appended on completion/failure.

### Recovery path

1. Autosave writes a session snapshot.
2. Restore loads the snapshot and normalizes text into a document.
3. Active/tab state rebuilt from the document-first restore flow (`src/workspace/recovery.c`).
4. Legacy row-format snapshots are supported through normalization.

## Common change recipes

### Update cursor/search/selection behavior

- Touch: `src/input/dispatch.c`, `src/editing/buffer_core.c`, `src/editing/selection.c`, `src/render/screen.c`, `tests/test_input_search.c`, `tests/test_document_text_editing.c`
- Keep:
  - offset-first invariants (`cursor_offset`, selection/search offsets)
  - UTF-8/grapheme clamping
  - non-edit actions not changing dirty state

### Update syntax behavior

- Touch: `src/language/syntax.c`, `src/language/syntax_queries.c`, `src/editing/buffer_core.c`, `src/render/screen.c`, `tests/test_syntax.c`, `tests/test_render_terminal.c`, vendor query files if needed
- Keep:
  - tab-local syntax state
  - budget/degraded mode behavior
  - selection/search overlay precedence

### Update LSP behavior

- Touch: `src/language/lsp.c`, `src/language/lsp_protocol.c`, `src/language/lsp_transport.c`, `src/input/dispatch.c`, `src/editing/buffer_core.c`, `src/config/lsp_config.c`, `tests/test_lsp.c`
- Keep:
  - per-server enable/disable gating (`gopls`, `clangd`, HTML/CSS/JSON, `typescript-language-server`, `vscode-eslint-language-server`)
  - global vs project precedence for `*_command` keys, with `javascript_install_command` global-only
  - proper open/change/save/close sequencing per active buffer
  - install prompt / task-log behavior for missing language servers
  - ESLint diagnostics + `eslint_fix` code-action path for JavaScript buffers

## Validation baseline

- `make`
- `make test`
- `make test-sanitize` for sensitive paths
