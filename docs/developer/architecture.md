# Architecture

RotIDE is a single-process terminal editor. One thread handles all editing,
rendering, and I/O; a background worker exists only for Tree-sitter parsing
of large documents. Language servers, DAP adapters, terminal-pane children,
and project-search jobs are external processes the editor talks to over
pipes or PTYs.

![Containers](../diagrams/svg/architecture-container.svg)

## Design rules

These are the invariants the rest of the code is built around. Breaking
them quietly is the fastest way to introduce desync bugs.

- **One writable owner per text**: each tab's `editorDocument` is the only
  authoritative byte storage. Rows, render columns, syntax spans, search
  matches, diagnostics, and viewport state are derived.
- **Edits are descriptors, applied through one pipeline**: every mutation
  (insert, delete, undo, redo, paste, restore) becomes an edit descriptor
  (byte range, inserted text, before/after cursor, before/after dirty) run
  through the shared edit pipeline. This is what makes dirty state and
  history deterministic.
- **Dirty tracks text only**: navigation, search, viewport changes, focus
  moves, LSP requests, and drawer changes never mark a tab dirty.
- **Validate at boundaries**: external input (TOML, JSON-RPC, filesystem,
  keys) is validated where it enters; internal callers are trusted. See
  [error_handling.md](error_handling.md).

## State ownership

![Document model](../diagrams/svg/document-model.svg)

`struct editorConfig E` holds the live editor state, grouped into clusters
(environment, active buffer, workspace, layout, preferences, …). The
*active buffer* (cursor, syntax state, LSP doc state, view scroll, etc.)
is declared once as `struct editorBuffer` and inlined into `E` so call
sites continue to read `E.cy`, `E.cursor_offset`, and so on. Each tab
stores its own `editorBuffer`; switching tabs swaps the struct in bulk
rather than copying field-by-field.

A separate `editorPrimaryFocus` enum tracks whether keys route to the text
area or the drawer. This is distinct from *pane* focus, which is owned by
the layout tree.

## Layout and panes

![Pane layout](../diagrams/svg/pane-layout.svg)

The layout is a binary tree of pane nodes. Leaves carry a `pane view`
(the cursor/scroll/selection snapshot) plus optional kind-state (e.g. a
terminal-pane handle). Internal nodes describe a split orientation and
ratio. The focused leaf's view is mirrored into `E`'s active state, so
focus change is a save/load operation between leaf view and `E`.

Tabs are global (`E.tabs[]`); each pane filters which global tabs appear
in its tab strip. The layout tree (splits + ratios) is persisted in the
workspace state file; per-pane kind-state is session-bound.

## Containers

**Input** decodes keys, mice, and synthetic events. It is a chain of
gates (synthetic events, prompts, mouse hit-testing, terminal-pane
forwarding) before the configured keymap maps a key to an
`editorAction`. The dispatch path is the only entry point to editor
behavior; mouse, drawer, and DAP commands take the same route.

**Text engine** owns documents, edits, history, selection, and the edit
pipeline. The pipeline writes the document, refreshes the row cache,
syncs the cursor, fans the edit out to syntax/LSP/diagnostics, records
history, and updates dirty state, in that order, atomically per edit.
A single fan-out point is the bridge to the language services, so adding
a new edit listener does not mean touching the edit code itself.

**Workspace** owns tabs, the pane tree, the drawer (project tree / file
search / project search / Git / LSP problems / DAP), recovery, and
persisted workspace state. The drawer is a *view*; it never owns file
text. File and Git state are polled (no inotify dependency).

**Renderer** builds a frame from current state. It is a pure renderer:
the main loop pumps LSP/DAP/viewport before calling it, so non-loop
callers (prompt sub-loops, recovery) can call render without re-running
event drains. Cursor placement, soft wrap, and pane borders are
computed here.

**Language services** cover Tree-sitter highlighting and LSP. Syntax
state is tab-local; the background worker accelerates large-document
parsing using a snapshot/revision protocol (see
[concurrency.md](concurrency.md)). LSP clients live in a registry keyed
by `(server_kind, workspace_root)`. There is no implicit "active
client"; call sites acquire the client explicitly before each request,
which is what lets a session talk to several servers across several
workspaces at once.

![LSP document lifecycle](../diagrams/svg/lsp-document-lifecycle.svg)

**Terminal + DAP** owns PTY-backed panes (libvterm), DAP launch and
session state, and a small contract between them: a DAP launch with
`console = "terminal"` opens an owned terminal pane and threads its tty
into the launch payload. The pane is closed automatically when the
session ends.

**Config** loads editor, theme, keymap, LSP, and DAP settings from TOML
at global (`~/.rotide/`) and project (`<project>/.rotide/`) scope. It
fans out to consumers at startup; runtime reloads go through the same
path.

## Concurrency and shutdown

The syntax background worker is the only auxiliary thread. It owns no
editor state; it parses worker-private byte copies and publishes results
tagged with `(revision, generation)`. Stale results are dropped on
publish. See [concurrency.md](concurrency.md).

Termination signals route through one handler that shuts down DAP, LSP,
the syntax worker, and the terminal before exit. Clean quit follows the
same shutdown sequence.

## Persistence boundaries

- **Document text**: saved atomically (temp + fsync + rename + dir
  fsync). Dirty cleared only after success.
- **Recovery snapshot**: serialized tab state + text; restored
  document-first, derived rows rebuilt on restore.
- **Workspace state** (non-document): drawer settings, recent files,
  open tabs, layout tree as an s-expression.
- **Config**: read-only at runtime; user edits go through the editor
  like any other file.

## Module layout

| Top-level dir | Responsibility |
|---|---|
| `support/` | Terminal raw mode, signal handler, allocation, file IO. |
| `text/` | Document, rope, UTF-8/grapheme, row helpers. |
| `editing/` | Edit pipeline, history, selection, search range. |
| `input/` | Decoding, dispatch, prompts, mouse, action families. |
| `render/` | Frame builder, surface painters, wrap, viewport. |
| `workspace/` | Tabs, drawer, layout, project search, Git, recovery, persistence, file watch. |
| `language/` | Tree-sitter syntax, LSP, autocomplete, language metadata. |
| `terminal/` | PTY transport, libvterm-backed terminal panes. |
| `debug/` | DAP client and terminal-console integration. |
| `config/` | TOML loaders for editor, theme, keymap, LSP, DAP. |
| `tests/` | Behavior tests, split per production boundary. |

For the runtime paths through these containers see
[workflows.md](workflows.md). For OOM and validation policy see
[error_handling.md](error_handling.md).
