# Concurrency

RotIDE runs as a single-threaded editor with one exception: Tree-sitter
parsing for the focused tab can be hoisted onto a background worker thread.
This page documents the snapshot/revision protocol that keeps the main
thread and the worker honest about who owns what.

## Threads

- **Main thread** owns `struct editorConfig E`, all `editorDocument`s,
  every `editorSyntaxState`, the visible syntax-span cache, terminal-pane
  state, the layout tree, LSP/DAP clients, and all I/O.
- **Syntax worker thread** (`src/language/syntax_worker.c`) is the only
  additional thread. It owns nothing in `E`: it operates on
  worker-private copies of the bytes plus a freshly-built
  `editorSyntaxState` produced inside the worker.

LSP servers, DAP adapters, terminal-pane children, and project-search
children run in *separate processes*, not threads, and communicate over
pipes or PTYs. They do not share memory with the editor.

## Snapshot / revision protocol

Each tab tracks two counters in its active-buffer fields (the X-macro
union, see [architecture.md](architecture.md)):

- `syntax_revision` — bumped every time the document bytes change in a
  way that invalidates a parse tree. Edits, undo, redo, save-as language
  changes, and external file reloads all bump it.
- `syntax_generation` — bumped when the tab's *language* changes (e.g.
  `save_as` flips a `.c` file to `.go`), so a result from an old parser
  cannot be silently re-applied to a different language.

When the main thread wants a background reparse it builds an
`editorSyntaxWorkerJob` snapshot containing:

1. the target language,
2. the current `(revision, generation)` pair,
3. the visible row window (`first_row`, `row_count`),
4. a `malloc`-owned copy of the document bytes (`text`, `text_len`).

The job is handed to `editorSyntaxWorkerSchedule`, which under
`g_syntax_worker_mutex` replaces any pending job (the worker only ever
holds one queued job and one in-flight job) and signals the condition
variable. The main thread does not block — it returns to the event loop.

## The worker loop

`editorSyntaxWorkerMain` runs:

1. Wait on the condvar until either a job is pending or the worker has
   been told to stop.
2. Move the pending job out under the lock, mark `g_syntax_worker_running = 1`,
   release the lock.
3. Parse the snapshot into a fresh `editorSyntaxState`, then collect
   captures for the requested row window. All of this happens outside the
   lock, on worker-private memory.
4. Wrap the parsed state plus the row-span vectors in an
   `editorSyntaxWorkerResult` tagged with the snapshot's
   `(language, revision, generation)`.
5. Re-take the lock, destroy any older un-consumed result, publish the new
   one, mark `g_syntax_worker_running = 0`, broadcast the condvar.

A consequence of step 5: at any time the slot holds at most one result.
If the main thread is slow to poll, intermediate results are dropped on
the worker side rather than queued.

## Publishing back to the main thread

The main loop polls each tick:

```c
editorSyntaxBackgroundPoll();   // src/editing/buffer_core.c
```

`editorSyntaxBackgroundPoll` calls `editorSyntaxWorkerTakeResult` (which
atomically moves the result out under the lock) and then validates the
result against the *current* tab state. If

- `result->language != E.syntax_language`, or
- `result->revision != E.syntax_revision`, or
- `result->generation != E.syntax_generation`,

the result is stale and is destroyed without being applied. This is the
core safety property: stale results never overwrite live state, no matter
how out-of-date the snapshot was when the worker finished.

A fresh, matching result is moved into `E.syntax_state` (the previous
state is destroyed) and its row spans are merged into the visible
syntax-span cache. Parse-failure counters are reset and any "syntax
degraded" status is reported.

## Shutdown

- Clean quit (`actions_file_tab.c`'s `editorQuit`) calls
  `editorSyntaxBackgroundStop`, which sets the stop flag, broadcasts the
  condvar, `pthread_join`s the worker, then clears any leftover
  job/result under the lock.
- Termination signals (`SIGINT`, `SIGTERM`, `SIGHUP`, `SIGQUIT`) flow
  through `editorHandleTerminationSignal` in `src/support/terminal.c`,
  which calls `editorSyntaxBackgroundStop` and
  `editorSyntaxReleaseSharedResources` alongside the LSP and DAP
  shutdowns before restoring the terminal and re-raising the default
  handler.

These calls are not strictly async-signal-safe — they touch malloc and
mutex — but in practice the editor is blocked on `read()` when a
termination signal arrives, so cleaning up adapter children and the
worker thread is the pragmatic trade-off over leaving them orphaned.

## What does *not* run in parallel

- Tree-sitter incremental edits and visible-row capture for the focused
  tab still happen on the main thread when the document is small enough
  or when the worker is disabled (`editorSyntaxBackgroundEnabled() == 0`).
  The worker is an optimization layered on top, not the primary parser.
- LSP and DAP I/O is non-blocking on the main thread (read/write through
  poll-driven pumping). There is no LSP thread.
- The terminal-pane `vterm_input_write` and the project-search child
  drain also happen on the main thread, paced by the main-loop poll
  tick.

If you find yourself needing to add concurrency anywhere else, prefer
the same pattern: snapshot the bytes, tag the snapshot with a revision,
run the work on worker-owned memory, and validate the revision on
publish.
