# Workflows

This page follows high-signal paths through RotIDE. The diagrams are
intentionally small: they show state ownership and sequencing without
trying to duplicate the source code.

## Startup and Main Loop

![Startup loop](../diagrams/svg/startup-loop.svg)

`main()` runs roughly in this order (`src/rotide.c`):

1. `setRawMode` — terminal raw mode + termios save.
2. `initEditor` — clipboard sink, theme, drawer state, single-leaf layout
   root + focused leaf, default keymap, bootstrap tab via
   `editorTabsInit`.
3. `editorSyntaxBackgroundStart` — Tree-sitter background worker.
4. `editorConfigEnsureGlobalConfig` — create the user's
   `~/.rotide/config.toml` on first launch.
5. `editorRecoveryInitForCurrentDir` — recovery path setup for the cwd.
6. `editorWorkspaceStateInitForCurrentDir` — workspace state path.
7. `editorConfigApplyConfiguredSettings` — apply theme, keymap, LSP, DAP
   settings from TOML.
8. `editorStartupLoadRecoveryOrOpenArgs` — restore snapshot or open
   CLI-named files.
9. `editorDrawerInitForStartup`, `editorWorkspaceStateLoadAndApply`,
   `editorWorkspaceStateRestoreTabs`, `editorGitInit`.

The main loop then runs forever:

```
editorSyntaxBackgroundPoll();
editorRefreshScreen();   // also pumps LSP/DAP/terminal panes
editorProcessKeypress(); // dispatches one event per iteration
```

`editorRefreshScreen` is where LSP, DAP, and terminal-pane pumping happens
each frame; `editorReadKey` (inside `editorProcessKeypress`) additionally
pumps terminal panes every VTIME tick (~100 ms) while waiting for input
and returns `TERMINAL_EVENT` if any bytes were drained so the next
iteration repaints.

## Keypress to Action

![Action dispatch](../diagrams/svg/action-dispatch.svg)

Terminal input is decoded in `src/support/terminal.c`.
`src/input/dispatch.c` remains the top-level pipeline: it handles a chain of
gates before reaching the keymap, while delegated modules own the larger gate
and action-family implementations:

- **Synthetic events** (`RESIZE_EVENT`, `TASK_EVENT`, `SYNTAX_EVENT`,
  `WATCH_EVENT`, `TERMINAL_EVENT`, `BRACKETED_PASTE_START/END_EVENT`,
  `INPUT_EOF_EVENT`) short-circuit before the editor sees them as regular
  keys. The bracketed-paste markers also toggle `E.paste_active` and call
  `vterm_keyboard_start_paste`/`end_paste` on the focused terminal pane
  if any.
- **Prompt mode** forwards bytes to the active prompt callback.
- **Mouse events** are hit-tested against the layout: clicks/wheel/drag
  inside a terminal pane with mouse tracking enabled are forwarded via
  libvterm and the pane is focused; otherwise the existing
  drawer/tab/text handlers in `src/input/mouse.c` run.
- **Terminal-pane key gate**: when the focused leaf is a terminal pane
  and we're not in the drawer, keystrokes go straight to the PTY via
  `editorTerminalPaneSendKey` unless the next chord is the configured
  `terminal_prefix`. Pressing the prefix arms a one-shot escape so the
  next key dispatches through the normal keymap.

After the gates, keymap lookup resolves configured bindings to
`enum editorAction`, then the dispatcher calls action-family helpers such as
`actions_edit`, `actions_workspace`, `actions_file_tab`,
`actions_language`, and `actions_terminal_debug`. Search and cursor-navigation
glue still lives in `dispatch.c` until that path gets a dedicated extraction.

## Edit Application and Dirty State

![Edit flow](../diagrams/svg/edit-flow.svg)

Edits are constructed in `src/editing/edit.c` and selection helpers,
then applied through `editorApplyDocumentEdit()`. The edit descriptor
carries the byte range, inserted text, before/after cursor offsets, and
before/after dirty values. This keeps dirty-state behavior deterministic
across insert, delete, undo, redo, save, and recovery.

Document mutation happens before derived work:

- replace bytes in `editorDocument`
- rebuild `struct erow` cache
- sync cursor from byte offset
- update syntax state
- notify LSP clients
- record history entry
- refresh visible syntax spans on demand

## Undo and Redo

History entries store operations: removed bytes, inserted bytes, cursor
offsets, dirty values, and edit kind. Typing may coalesce into grouped
entries. Any new edit after undo clears redo history. Undo and redo
replay document edits through the same canonical mutation path instead
of restoring full snapshots.

## Split and Focus

![Split and focus](../diagrams/svg/split-focus-flow.svg)

`src/workspace/layout.c` owns the pane tree. Splitting wraps the focused
leaf in a fresh split node, allocates a new sibling that inherits the
splitting pane's view, then resets the sibling's `pane_tabs` membership
to only the splitting pane's active tab — that's the VSCode-style
"open current file in the new pane" behavior.

Focus changes go through `editorLayoutSetFocusedLeaf`, which captures
the outgoing pane's live cursor/scroll/active-tab into its view, swaps
the global active tab if the incoming pane records a different one
(invoking `editorTabSwitchToIndex`), then loads the incoming pane's
cursor on top. Close-pane promotes the sibling and re-loads its view.

## Save and Recovery

![Save and recovery](../diagrams/svg/save-recovery.svg)

Save reads the active document as text, writes a temporary file,
fsyncs, renames, fsyncs the parent directory, and clears dirty state
only after success. Recovery autosaves active workspace state and
restores through the document-first loading path, so restored rows
remain derived from `editorDocument`.

Workspace state (`src/workspace/workspace_state.c`) is the adjacent
persistence path for the non-document state that should survive a
restart: drawer width and mode, recent files, the open-tab list, the
active tab, and the serialized pane layout tree
(`editorLayoutSerialize` writes a compact `layout=<expr>` line and the
loader deserializes it on startup).

## Search and Highlight

![Search flow](../diagrams/svg/search-flow.svg)

Search uses the prompt API plus search callbacks still hosted in
`src/input/dispatch.c`, then scans the active `editorTextSource`. The active
match is stored as byte offset plus length. Rendering maps the match back to
rows and applies highlight overlays without mutating text or dirty state.

## Syntax Highlighting

![Syntax flow](../diagrams/svg/syntax-flow.svg)

Language detection chooses a table entry from
`src/language/languages.c`. `editorSyntaxState` owns the Tree-sitter
tree, injected trees, budgets, and limit events for one tab. Visible
rows request captures for byte ranges through `src/language/syntax.c`;
the render pane/row path combines syntax spans with selection, search, and
diagnostic overlays.

## LSP

![LSP flow](../diagrams/svg/lsp-flow.svg)

Opening or editing a supported file ensures the LSP document is tracked
and versioned. Changes are converted from editor byte/row state to
protocol positions. Requests such as definition, implementation,
symbols, completion, diagnostics, and ESLint code actions route through
JSON-RPC helpers and write results back to tab-local state.

Range `didChange` notifications are used when the edit can be
represented in the server's position encoding. RotIDE falls back to a
full-document `contentChanges` entry when range conversion would need
pre-edit text (e.g. UTF-16-encoded deletes).

Missing language servers can open install/help task-log tabs. Those
tabs are generated output views and do not become normal editable
files.

## Terminal Panes

![Terminal pane lifecycle](../diagrams/svg/terminal-pane-flow.svg)

`editorPaneNodeNewTerminalLeaf` (or `editorTerminalPaneOpenSplit`,
which splits then converts the new sibling) creates a leaf of kind
`EDITOR_PANE_KIND_TERMINAL` backed by an `editorTerminalPane`. The
constructor wires a vterm parser to the PTY: child output is fed to
`vterm_input_write` on every poll tick; libvterm's output callback
writes encoded keystrokes/mouse events back to the master fd.

The main-loop input poll drains terminal panes via
`editorTerminalPanePumpAll` and returns `TERMINAL_EVENT` whenever any
bytes were consumed, which keeps the screen responsive to long-running
child output without blocking on stdin. SIGWINCH propagates through
`editorTerminalPaneResizeAllToLayout`, which mirrors the renderer's
split-rect math and `TIOCSWINSZ`-resizes each pane. Exited terminal panes
are closed from the event/draw path, with focus restored to a surviving
sibling when the focused pane disappears.

Mouse passthrough (`editorTerminalPaneSendMouseButton/Move`) only fires
when the child has enabled DECSET 1000/1002/1003 via the `settermprop`
callback. Bracketed paste forwarding wraps the pasted bytes only if the
child enabled DECSET 2004.

## DAP

![DAP launch and lifecycle](../diagrams/svg/dap-flow.svg)

A `dap_start` action calls `editorDapStartLaunch`. The launch flow
takes a local copy of the launch config so launch-time mutations stay
out of `E.dap_launches`, then runs `editorDapPrepareTerminalConsole`:
if `console = "terminal"`, the focused pane is split horizontally, a
placeholder shell (`sleep infinity`) is spawned only to allocate the PTY,
`ptsname(master_fd)` resolves the slave path, the placeholder is stopped,
and `tty` is injected into the outgoing launch JSON. The `console` field
itself is always stripped — it is a rotide hint, not a DAP standard.

The adapter (gdb-dap, lldb-dap, dlv dap, …) is spawned over stdio. The
session runs through `editorDapPumpNotifications` from
`editorRefreshScreen`. On `terminated`/`exited` events or explicit
shutdown, `editorDapCloseOwnedTerminalPane` retires the owned terminal
pane and restores focus to the sibling.

## Task Logs

![Task log flow](../diagrams/svg/task-log-flow.svg)

Task logs run a child process, merge stdout/stderr, append output to a
generated document, rebuild rows, and keep the tab open with a final
status line. They are read-only, non-savable, and capped by
`ROTIDE_TASK_LOG_MAX_BYTES`.
