# Concurrency

RotIDE is single-threaded except for one worker thread that parses
Tree-sitter for the focused tab. Everything else — language servers, DAP
adapters, terminal panes, project search — runs in separate processes
and is drained from the main thread.

## Snapshot / revision protocol

![Syntax worker](../diagrams/svg/syntax-worker.svg)

Each tab carries a *revision* (bumped on any edit) and a *generation*
(bumped when the language changes). The main thread hands the worker a
self-contained snapshot — language, revision, generation, an owned byte
copy, and the visible row window. The worker parses on private memory
and publishes a result tagged with the snapshot's `(language, revision,
generation)`.

On publish, the main thread compares the tags against current tab
state. Mismatch → drop. That is the entire safety property: stale
results never overwrite live state. The slot holds at most one queued
job and one pending result; staleness is absorbed by overwriting.

The same pattern is the template if a second worker is ever introduced:
snapshot, tag, parse on worker-owned memory, validate on publish.

## Shutdown

Clean quit and termination signals share one shutdown sequence: DAP,
LSP, syntax worker (stop flag → wake → join), terminal restore.

The signal path is not strictly async-signal-safe (malloc, mutex). The
trade-off is intentional: when a termination signal arrives the editor
is blocked on `read()`, and reaping adapter children matters more than
formal safety.

## What is not parallel

- Incremental Tree-sitter edits and small-document parses still run on
  the main thread. The worker is an optimization for large documents,
  not the primary parser.
- LSP, DAP, terminal-pane, and project-search I/O is poll-driven on the
  main thread. There are no I/O threads.
