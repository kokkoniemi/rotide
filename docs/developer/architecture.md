# Architecture

RotIDE keeps one main editor process and state model, with helper threads and
child processes for specific work such as Tree-sitter background parsing, LSP
servers, task logs, project search, and terminal panes. The main design rule is
that text has one writable owner: `editorDocument`. Rows, syntax captures,
rendered columns, diagnostics, search matches, and viewports are derived from
that document or from tab-local state.

![RotIDE container architecture](../diagrams/svg/architecture-container.svg)

## State Model

The global editor state lives in `struct editorConfig E` (`src/rotide.h`). It
stores terminal dimensions, active buffer fields, runtime config, task state,
tab list, drawer state, theme, and clipboard integration. Each real tab stores
the same buffer-facing fields in `struct editorTabState`, and tab switching
copies state between the active fields and the selected tab.

The important ownership split is:

- `editorDocument`: canonical bytes for a tab.
- `editorRope`: chunked byte storage used by the document.
- `line_starts`: document-owned line index for byte/line mapping.
- `struct erow`: derived row text and render cache.
- `cursor_offset`, search offsets, and selection anchors: canonical positions.
- `cy`, `cx`, `rx`, `rowoff`, `coloff`, `wrapoff`: derived or view state.

![Document model](../diagrams/svg/document-model.svg)

`src/text/document.c` owns document reset, copy, replace, and byte/line mapping.
`src/text/rope.c` owns chunked storage. `src/text/row.c` owns UTF-8 and
grapheme-aware row helpers. `src/editing/buffer_core.c` bridges the document
model to active editor state.

## Text and Dirty State

Text mutations are represented as `struct editorDocumentEdit` and applied
through `editorApplyDocumentEdit()`. That descriptor is the contract for byte
range, inserted text, cursor movement, and dirty-state transitions; the
step-by-step mutation order is covered in [Workflows](workflows.md).

Navigation, search, viewport changes, drawer changes, and LSP requests do not
mark a tab dirty. Undo and redo restore both text and dirty metadata from
operation history, not from full-buffer snapshots.

## Input and Actions

Key behavior routes through `enum editorAction`. Defaults are built in
`src/config/keymap.c`, optional user bindings are loaded from
`~/.rotide/config.toml`, and `src/input/dispatch.c` maps decoded terminal input
to actions.

Keeping commands action-based makes key behavior testable and keeps prompt,
mouse, drawer, and editor commands on the same dispatch path.

## Tabs, Drawer, and Read-only Views

File tabs are editable and savable. Preview tabs come from drawer/file-search
navigation and can be pinned when edited or explicitly opened. Task-log tabs are
generated documents used for child-process output; they are read-only and not
savable. Unsupported-file and Git-diff tabs also avoid normal save semantics.

![Tab lifecycle](../diagrams/svg/tab-lifecycle.svg)

The drawer is a view over project tree entries, search results, Git state, and
LSP problem/symbol entries. It does not own file text.

## Rendering

`src/render/screen.c` builds the terminal frame from active state: tab bar,
drawer, text viewport, status bar, message bar, popup, syntax spans, selection,
search highlight, diagnostics, and cursor position. Rendering reads derived
rows plus overlay state. It does not mutate canonical text.

Soft wrapping and rendered columns depend on row caches. The canonical cursor
position remains `cursor_offset`; row/column fields are synchronized through
mapping helpers when text or cursor state changes.

## Syntax

Syntax state is tab-local (`editorSyntaxState`). Language detection and parser
metadata are table-driven in `src/language/languages.c`. Query text is embedded
at build time from `scripts/queries_manifest.txt` into
`src/language/syntax_query_data.h`.

Tree-sitter parsing uses `editorTextSource`, so syntax can read document bytes
without requiring a permanent flattened buffer. Query budgets and injection
limits degrade behavior before hard-disabling highlighting for large or
expensive inputs. The background syntax runner is an in-process worker thread
that receives snapshots and commits only results that still match the tab's
syntax revision.

## LSP

LSP state is tracked per tab with document-open flags and versions. The process
clients live under `src/language/lsp.c`, `lsp_protocol.c`, and
`lsp_transport.c`. Definition, implementation, completion, symbols,
diagnostics, and ESLint fixes all route through the same document position
helpers used by the editor.

![LSP document lifecycle](../diagrams/svg/lsp-document-lifecycle.svg)

LSP diagnostics are stored on the owning tab and rendered through the LSP drawer
and text overlays. ESLint is a separate JavaScript diagnostics/fix provider.

## Save and Recovery

Saves use `src/support/file_io.c` for the atomic temp-file/fsync/rename flow.
Save syscall wrappers in `src/support/save_syscalls.c` make failure paths
testable.

Recovery snapshots are document-first. `src/workspace/recovery.c` persists tab
state and text, restores tabs on startup when requested, and normalizes older
row-oriented data into the current document model.

## Module Map

- `src/rotide.c`, `src/rotide.h`: process lifecycle, global state, main loop.
- `src/support/`: terminal, allocation, file IO, testable save syscalls.
- `src/text/`: document, rope, UTF-8, grapheme, row/render helpers.
- `src/editing/`: document edit application, history, selection, edit builders.
- `src/input/`: action dispatch, prompts, mouse handling.
- `src/render/`: frame rendering, viewport, overlays, popups.
- `src/workspace/`: tabs, drawer, project search, Git, recovery, task logs.
- `src/config/`: TOML loading for editor, theme, keymap, LSP, and runtime config.
- `src/language/`: Tree-sitter, LSP, autocomplete, terminal/PTY support.
- `tests/`: behavior tests split by subsystem.

Keep future architecture docs at this level: explain ownership, contracts, and
flows before listing functions.
