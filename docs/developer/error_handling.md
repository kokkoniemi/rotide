# Error Handling

RotIDE has two main classes of error to deal with:

1. **Resource exhaustion** (allocation failures, full-disk on save,
   transient I/O errors) — recoverable; the editor stays alive and
   surfaces a status-bar message.
2. **Logic errors / invariant violations** — internal bugs; we don't try
   to recover from them in release code. They're caught by the test
   suite, not by runtime validation.

This page documents how those two categories are handled in code.

## OOM and the status-bar policy

The convention across the codebase is:

> When an operation fails because an allocation or other recoverable
> condition failed, return `0` (or a `NULL` for object-returning APIs)
> and call `editorSetAllocFailureStatus()` so the user sees
> `"Out of memory"` (or a more specific message) in the status bar.

`editorSetAllocFailureStatus` is declared in
`src/editing/buffer_core.h`. Callers that already have a more specific
status message ("Could not save: ...", "Could not open settings", etc.)
just call `editorSetStatusMsg` directly with the specific text.

The full chain for an OOM during an edit then looks like:

1. A low-level helper (`writeBufReserve`, row builder, syntax span
   array growth, …) tries to `realloc` and fails.
2. It returns `0` to its caller.
3. Every caller up the stack also returns `0` on failure, freeing any
   partially-owned intermediate buffers as it unwinds.
4. The top-level action handler (the dispatcher entry point or a
   long-running pump) sets a status message via
   `editorSetAllocFailureStatus`.
5. The next `editorRefreshScreen` paints the status bar with that
   message; document, tabs, layout, and undo stack remain consistent.

This is why most internal helpers return `int` even when they appear to
just "do a thing": the return value carries the recoverable-failure
signal. *Do not* swallow it with `(void)foo(...)` in caller code that
can do something useful with a failure.

## Where validation belongs

Validation only happens at *system boundaries*:

- **User input**: command-line args, prompt input, `read_key` decoded
  sequences, mouse coordinates.
- **External data**: TOML config files, recovery snapshots, workspace
  state, LSP/DAP JSON payloads, Git porcelain output, project-search
  results.
- **Filesystem I/O**: filename length, path canonicalization,
  `stat`/`open` failures, write/rename atomicity.

Internal callers are *trusted*. If `editorActiveBufferRows()` returns
the row array, downstream code does not re-check that it's non-null or
that the row count is non-negative — it would mean a different function
already misbehaved, and the right fix is in that function, not here.

Concretely, you will not find runtime null-checks on values that came
out of `E.*` after an action handler decided to act on them, and you
will not find assertion-style defensive code in the data-flow path.
Invariants are documented in the function or struct comments and
enforced by tests.

## When to use which

| Situation                          | Action                                                |
|------------------------------------|-------------------------------------------------------|
| `malloc`/`realloc` returned `NULL` | Return `0`/`NULL` + `editorSetAllocFailureStatus`.    |
| `write`/`fsync`/`rename` failed    | Return `0` + a specific `editorSetStatusMsg`.         |
| TOML/JSON parse failed             | Log a diagnostic on the relevant tab, fall back to defaults. |
| File doesn't exist on open         | Treat as new file; status reflects this.              |
| LSP/DAP server crashed             | Mark client unhealthy; status message; clean restart. |
| Invariant violated in internal API | This is a bug. Fix the API; do *not* add a runtime check. |

## The test side

Allocation-failure paths are exercised by `tests/alloc_test_hooks.c`,
which can fail the *N*-th allocation deterministically. A handful of
tests (search for `editor_*_oom_*` in `tests/test_input_*.c`,
`tests/test_workspace_io.c`, etc.) drive that hook to confirm:

- the action returns the failure signal,
- the status bar is updated,
- the document, undo stack, and layout remain consistent.

When adding a new path that mutates state across multiple allocations,
add a matching OOM test so the partial-state cleanup is locked in.

## Signal-driven termination

Termination signals (`SIGINT`, `SIGTERM`, `SIGHUP`, `SIGQUIT`) are
handled in `src/support/terminal.c`'s `editorHandleTerminationSignal`,
which tears down DAP / LSP / syntax-worker / shared syntax resources
and restores the terminal before re-raising the default handler. See
[concurrency.md](concurrency.md) for the async-signal-safety trade-off.

Signal handling is *not* an "error handling" path in the OOM sense — it
is a *shutdown* path and never tries to recover. The editor exits.
