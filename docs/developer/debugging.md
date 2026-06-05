# Debugging (DAP)

RotIDE debugs through the Debug Adapter Protocol. The adapter is an external
process the editor speaks to over stdio pipes — the same boundary model as LSP.
All protocol handling runs on the main thread; adapter traffic is one of the
fd sources the input loop waits on, so responses and events are serviced between
keystrokes without a dedicated thread. The debuggee is fully isolated inside the
adapter's own process tree.

C is the first supported language (adapter: `gdb -i dap`); the launch/response
machinery is language-agnostic, so other adapters need only configuration.

![DAP flow](../diagrams/svg/dap-flow.svg)

## Configuration

Two layers, both TOML:

- **Adapters** live in the global config (`~/.rotide/config.toml`) under
  `[dap.adapters]`, mapping a free-form id to a command run via `/bin/sh -c`:

  ```toml
  [dap.adapters]
  c = "gdb -i dap"
  ```

- **Launch configs** live in the project's `.rotide.toml` under
  `[dap.launch.<id>]`. `adapter` references an adapter id; `request` must be
  `launch` (attach/remote are not supported yet and are rejected at load).
  Remaining keys are forwarded verbatim as the launch request's arguments, so
  the accepted set is whatever the adapter understands. String values expand
  `${workspaceFolder}`, `${file}`, `${fileDirname}`, and `${fileBasename}`.

  ```toml
  [dap.launch.c_dap_fixture]
  name = "C DAP Fixture"
  adapter = "c"
  request = "launch"
  program = "${workspaceFolder}/tests/dap/supported/c/dap_sample.out"
  cwd = "${workspaceFolder}"
  args = ["branch-a"]
  stopOnEntry = false
  console = "terminal"
  ```

`[dap.defaults.<id>]` templates in the global config can be copied into a
project's `.rotide.toml` from the Debugger drawer.

## Launch lifecycle

Ordering is load-bearing and is enforced by an explicit session state machine:

1. Send `initialize`; **wait for its response** before doing anything else.
   Sending `launch` early makes some adapters start the program before
   breakpoints are configured, so it runs to exit before the user sees anything.
2. On the initialize response, send the queued `launch` request.
3. On the `initialized` event, register breakpoints (`setBreakpoints` per source)
   and send `configurationDone`.
4. The program runs. A `stopped` event triggers a fan-out:
   `threads → stackTrace` (stopped thread) `→ scopes` (top frame)
   `→ variables` (each scope), which populates the DAP drawer.

`continued`/`terminated`/`exited` clear the inspection state and update the
running/stopped indicators.

## Breakpoints

Breakpoints are editor state, set before or during a session. They can be
toggled from the line-number gutter (left-click), the editor right-click menu,
the `dap_toggle_breakpoint` keybinding, and are listed in the Debugger drawer.
A set breakpoint shows a marker in the gutter's separator column (no gutter widening);
the current stopped line shows a distinct marker that takes precedence.

Breakpoints are sent to the adapter with **absolute** source paths so they match
the adapter's debug-info paths regardless of how the buffer was opened. In-editor
matching (gutter marker, toggle) stays a plain path comparison against the open
buffer.

## Execution control

Continue, step over/into/out, and pause are thread-scoped: each carries the
thread of the most recent stop (falling back to the first known thread, then the
main thread). Stop disconnects and tears the session down. After every stop the
stack/scopes/variables fan-out re-runs, so the views reflect the new location.

## Variables

RotIDE requests richer variable metadata from adapters during `initialize`,
including variable types, memory references, and variable paging support. The
Debugger drawer uses that data when available to render locals and arguments with
compact type/value/address information, and it opportunistically asks for a small
preview of child variables so arrays and structs can show useful contents instead
of only an address.

These capabilities are requested, not required. Adapters that omit type,
memory-reference, or child-preview data still populate the drawer with the base
DAP `name`, `value`, and `variablesReference` fields.

## Debug UI

While a session is active the status bar becomes a control bar: a `PAUSED` /
`RUNNING` badge followed by buttons — stopped shows Cont/Over/Into/Out/Restart/
Stop, running shows Pause/Restart/Stop. The buttons are clickable independent of
which pane has focus, so execution can be driven even when the console/terminal
pane holds keyboard focus.

On launch a **Debug Console panel** opens as a bottom split, with a
`Terminal | Debug Console` tab strip styled like normal pane tabs (active tab in
the tab-active theme style). When the launch config sets `console = "terminal"`
the panel also owns the debuggee's tty terminal:

- **Terminal tab** — the debuggee's stdout/stderr on a real tty. gdb ignores the
  DAP `tty` launch arg but honours the `--tty` startup flag (the same mechanism
  VS Code's cppdbg uses), so rotide appends `--tty=<pts>` to gdb-family adapter
  commands when `console = "terminal"`; program output then flows straight to the
  pts and the tab renders it. Non-gdb adapters instead receive the launch `tty`
  argument.
- **Debug Console tab** — a scrollable transcript of all adapter `output` events
  (gdb banner, notifications) plus lifecycle lines and REPL echoes, with an
  **inline REPL input line** at the bottom: focus the panel, type an expression,
  Enter evaluates it (`evaluate`, scoped to the top frame when stopped) and prints
  `> expr` / `= result`. `dap_evaluate` also opens a one-line prompt for the same.
  Because the debuggee has its own tty, its output never appears here — matching
  the VS Code Terminal/Debug Console split.

Click a tab to switch; PgUp/PgDn scroll the console tab. The panel is transient
(not persisted across restarts).

## Console output

`console = "terminal"` hosts the debuggee's tty in a terminal pane. The PTY is
allocated **without forking a placeholder process** (see
[concurrency](concurrency.md) — the editor runs a worker thread, and a fork that
touches an inherited lock before `exec` can deadlock the child): the editor opens
the master, hands the adapter the slave path, and the debuggee writes to it.
Adapter `output` events and lifecycle messages are also captured for the drawer's
Output group.

## Errors

Failed responses are surfaced with the adapter's own text — the detailed
`body.error.format` when present, otherwise the short `message`. `initialize`/
`launch` failures abort the session; other request failures show in the status
bar and the captured output without ending the session. A `configurationDone`
that answers `notStopped` is expected once the program is already running and is
treated as benign.

## Known limitations

- Only `launch` requests; no `attach`/remote.
- `stopOnEntry` depends on the adapter (gdb's DAP does not honor it — use a
  breakpoint to stop early).
- While a focused terminal/console pane has input focus, control keybindings go
  to that pane; use the clickable status-bar debug controls when focus should
  stay in the panel.
- Adapter capabilities from the `initialize` response are still only partially
  modeled. RotIDE requests richer variable metadata and renders it when present,
  but most debugger actions are not yet gated by advertised capabilities.

## Fixture

`tests/dap/supported/c/` holds a self-contained multi-file C program built to
`dap_sample.out` with debug info via its `Makefile`. The fixture is split across
`main.c`, `items.c`, `numeric.c`, `runmode.c`, and `worker.c` so manual
end-to-end checks can cover cross-file stepping, breakpoints, stack frames,
threads, and rich locals/arguments.
