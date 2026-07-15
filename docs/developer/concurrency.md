# Concurrency

RotIDE is single-threaded except for one worker thread that prepares visible
Tree-sitter spans for the focused tab and performs full parses when no reusable
tree exists. Everything else (language servers, DAP adapters, terminal panes,
project search) runs in separate processes and is drained from the main thread.

## Snapshot / revision protocol

![Syntax worker](../diagrams/svg/syntax-worker.svg)

Each tab carries a *revision* (bumped on any edit) and a *generation*
(bumped when the language changes). The main thread hands the worker a
self-contained snapshot: language, revision, generation, an owned byte copy,
the visible row window, and, when available, a shallow Tree-sitter tree copy plus
the edits since that tree was accepted. The worker applies the pending edits,
parses once incrementally, collects spans on private state, and publishes a
result tagged with the snapshot's `(language, revision, generation)`. Initial
parses and missing-tree fallbacks parse the snapshot from scratch.

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

- The main thread records Tree-sitter edit deltas and owns revision checks; tree
  edits, parsing, and span collection run on the worker.
- LSP, DAP, terminal-pane, and project-search I/O is poll-driven on the
  main thread. There are no I/O threads.
- Inline git blame runs `git blame --incremental` synchronously on the main
  thread on the first lookup for the active file. The result is stored in a
  single cache keyed by file/repository/branch/HEAD/disk-state, so subsequent
  line changes use in-memory lookups. Activating a different key replaces the
  cache. Files or command output past the cache guards fall back to per-line
  blame.
