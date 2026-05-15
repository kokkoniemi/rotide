# Error Handling

Two categories, two policies.

## Recoverable failures: return 0 + status

Allocation failures, disk-full, transient I/O. The convention:

> Return `0` (or `NULL`), free any partial work as you unwind, and let the
> top of the call chain call `editorSetAllocFailureStatus()` (declared in
> `src/editing/buffer_core.h`). For more specific failures, call
> `editorSetStatusMsg` with the appropriate message.

This is why most internal helpers return `int` — the return value carries
the recoverable-failure signal. Don't `(void)` it away in callers that
could surface the failure.

The next `editorRefreshScreen` paints the status; the document, tabs,
layout, and undo stack stay consistent.

## Invariant violations: not handled at runtime

Internal callers are trusted. Values that came out of `E.*` are not
re-validated downstream. If an invariant breaks, the fix lives in the
function that produced the bad value, not in defensive checks at the use
site. Invariants are documented in struct/function comments and locked in
by tests, not by runtime asserts in the data path.

## Where validation belongs

Only at system boundaries: command-line args, prompt input, decoded key
sequences, TOML/JSON payloads, filesystem syscalls, Git porcelain,
project-search output.

## OOM tests

`tests/alloc_test_hooks.c` can fail the *N*-th allocation. Search for
`*_oom_*` tests (e.g. in `tests/test_input_*.c`, `tests/test_workspace_io.c`)
for the pattern. When adding a path that allocates across multiple steps,
add a matching test so partial-state cleanup stays locked in.

## Signals

`SIGINT`/`SIGTERM`/`SIGHUP`/`SIGQUIT` go through
`editorHandleTerminationSignal` (`src/support/terminal.c`), which runs the
LSP/DAP/syntax/terminal shutdowns and exits. This is a *shutdown* path,
not an error path — no recovery is attempted. See
[concurrency.md](concurrency.md) for the async-signal-safety trade-off.
