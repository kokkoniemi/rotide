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
  args = ["${workspaceFolder}/tests/dap/supported/c/dap_sample.c"]
  stopOnEntry = false
  console = "terminal"
  ```

`[dap.defaults.<id>]` templates in the global config can be copied into a
project's `.rotide.toml` from the DAP drawer.

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
the `dap_toggle_breakpoint` keybinding, and are listed in the DAP drawer. A set
breakpoint shows a marker in the gutter's separator column (no gutter widening);
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

## Debug UI

While a session is active the status bar becomes a control bar: a `PAUSED` /
`RUNNING` badge followed by buttons — stopped shows Cont/Over/Into/Out/Restart/
Stop, running shows Pause/Restart/Stop. The buttons are clickable independent of
which pane has focus, so execution can be driven even when the console/terminal
pane holds keyboard focus.

A REPL is available via `dap_evaluate`: it prompts for an expression, evaluates
it (`evaluate`, scoped to the top frame when stopped), and echoes `> expr` and
`= result` into the output transcript.

`dap_console` toggles a **Debug Console** pane — a scrollable view of the output
transcript (adapter `output` events, lifecycle lines, and the REPL echoes),
opened as a bottom split. PgUp/PgDn/↑/↓ scroll it while focused. It is
display-only (input goes through the `dap_evaluate` prompt) and transient (not
persisted across restarts).

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
  to that pane; focus an editor pane to use them (status-bar debug controls that
  work regardless of focus are planned).
- Adapter capabilities from the `initialize` response are not yet used to gate
  unsupported features.

## Fixture

`tests/dap/supported/c/` holds a self-contained C program (`dap_sample.c`, built
to `dap_sample.out` with debug info via its `Makefile`) used as the
`c_dap_fixture` launch target for manual end-to-end checks.
