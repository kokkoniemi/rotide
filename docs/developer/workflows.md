# Workflows

Sequenced paths through the containers in [architecture.md](architecture.md).
Diagrams carry the structure; prose calls out invariants and edge cases that
matter when changing each path.

## Startup and main loop

![Startup loop](../diagrams/svg/startup-loop.svg)

Startup is deterministic: raw mode, editor state, syntax worker, config,
recovery / CLI args, workspace state, Git. The order matters. Config has
to be applied before tabs are restored so theme and keymap settings reach
restored buffers.

The main loop is six steps per tick: poll the syntax worker, pump LSP,
pump DAP, update viewport, render, dispatch one input event. The render
call is a pure renderer; pumping is explicit at the top of the tick. Sub-
loops (prompt input, recovery confirm, mid-action redraws) can call
render without re-pumping.

Termination signals route through the same shutdown sequence as a clean
quit: DAP, LSP, syntax worker, terminal restore. See
[concurrency.md](concurrency.md) for the async-signal-safety trade-off.

## Input dispatch

[Action dispatch](../diagrams/svg/action-dispatch.svg)

Input flows through gates before reaching the keymap so that prompts,
mouse hits in terminal panes, and direct-to-PTY keystrokes can short-
circuit the editor. Anything that reaches the keymap becomes an
`editorAction`, which is the testable boundary.

The terminal-pane gate has a one-shot prefix arm: when the focused leaf
is a terminal, keys go to the PTY unless the configured prefix is
pressed, which makes the *next* key dispatch through the normal keymap.

## Edits

![Edit flow](../diagrams/svg/edit-flow.svg)

Every text mutation is an edit descriptor handed to the edit pipeline.
The pipeline's job is to keep document, row cache, cursor, language
services, and history consistent. It computes removed text, the affected
row range, and any syntax edit from the old document; reserves insert
capacity for the forward and revert paths; mutates `editorDocument`; then
derives replacement rows from the new document and splices them into the
row cache. The fan-out to syntax/LSP/diagnostics runs *after* the
document and row cache have been updated, so listeners always observe a
consistent state.

Undo and redo replay edits through the same pipeline rather than
restoring snapshots. That is how dirty state stays accurate across
arbitrary edit sequences.

## Split, focus, close

![Split and focus](../diagrams/svg/split-focus-flow.svg)

The focused leaf's view is mirrored into `E`; focus change is a
save/load operation. A split inherits the splitting pane's view and
re-seeds the new sibling's tab membership to the active tab only
(the "open current file in new split" behavior). Closing a leaf promotes
its sibling.

## Save and recovery

![Save and recovery](../diagrams/svg/save-recovery.svg)

Saves are atomic (temp → fsync → rename → fsync dir). Dirty clears only
after rename succeeds. Autosave writes a recovery snapshot containing
the document text; startup detection prompts the user and restores
through the document model.

Workspace state is the parallel persistence path for non-document state
(drawer width, open tab list, layout tree). The layout tree is an
s-expression so the user's split shape survives restarts.

## Search

![Search](../diagrams/svg/search-flow.svg)

Search uses the prompt path. Matches are stored as `(byte offset,
length)`; rendering overlays them at draw time. Search never mutates
text and never affects dirty state.

## Syntax

![Syntax](../diagrams/svg/syntax-flow.svg)

Per tab: detect language, parse (incrementally on edits), collect
captures for the visible byte range, parse injections if present.
Budgets degrade behavior before failing: predicates are dropped, then
injections, before highlighting is disabled. Large documents are
parsed on the background worker; see [concurrency.md](concurrency.md).

## LSP

![LSP flow](../diagrams/svg/lsp-flow.svg)

Each request first looks up the relevant client by
`(server_kind, workspace_root)`. There is no implicit "active LSP";
tab switches just pick a different client. Range `didChange` is used
when the encoding allows; full-document `contentChanges` is the
fallback. Missing servers open install/help task-log tabs rather
than failing silently.

## Terminal panes

![Terminal pane](../diagrams/svg/terminal-pane-flow.svg)

A terminal pane wraps a PTY child with a libvterm parser. Child output
is drained on each input tick; the pump emits a synthetic event so
the next frame repaints. Exited panes are closed from the event/draw
path with focus restored to the surviving sibling. Mouse and bracketed
paste forwarding is conditional on the child enabling the corresponding
DECSET modes.

## DAP

![DAP](../diagrams/svg/dap-flow.svg)

The launch payload is rotide-augmented with a `console = "terminal"`
hint: when set, the launch flow opens an owned terminal pane,
resolves its tty, and injects it into the payload. The hint itself is
stripped before sending. The adapter runs over stdio JSON-RPC; the
session pump runs once per main-loop tick. Session teardown closes the
owned pane and restores focus.

## Task logs

![Task log](../diagrams/svg/task-log-flow.svg)

A task log is a generated, read-only tab fed by a child process's
merged stdout/stderr. Output is appended to a document, rows are
rebuilt, and a cap prevents unbounded growth. The tab stays open
after the process exits, with a final status line appended.
