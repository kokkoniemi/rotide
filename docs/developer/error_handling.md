# Error Handling

Two categories, two policies.

## Recoverable failures

Allocation failures, disk-full, transient I/O. The convention:

> Return `0` (or `NULL`), free any partial work as you unwind, and let
> the top of the call chain set a status message: generic
> (`editorSetAllocFailureStatus`) or specific (`editorSetStatusMsg`).

This is why most internal helpers return `int`: the return value carries
the recoverable-failure signal. Don't `(void)`-discard it in callers
that could report the failure.

The next render paints the status. Document, tabs, layout, and undo
stack stay consistent because the pipeline frees partial work as it
unwinds.

## Invariant violations

Not handled at runtime. Internal callers are trusted; values read out of
`E.*` are not re-validated downstream. If an invariant breaks, the fix
lives in the producer, not as a defensive check at the use site.
Invariants are documented in struct/function comments and locked in by
tests.

## Validation belongs at boundaries

Only at the edges where untrusted data enters: CLI args, prompt input,
decoded key sequences, TOML/JSON payloads, filesystem syscalls, Git
porcelain, project-search output.

## OOM tests

Allocation-failure paths are exercised by a test hook that fails the
*N*-th allocation. When adding a path that allocates across multiple
steps, add a matching test so the unwind cleanup stays locked in.

## Termination signals

`SIGINT`/`SIGTERM`/`SIGHUP`/`SIGQUIT` are a *shutdown* path, not an
error path. They run the same teardown as a clean quit; no recovery is
attempted. See [concurrency.md](concurrency.md) for the
async-signal-safety trade-off.
