# RotIDE Architecture Audit

**Date:** 2026-05-15
**Scope:** Documentation in [docs/developer/](docs/developer/) treated as source of
truth; codebase under [src/](src/) and [tests/](tests/) inspected to validate.
PlantUML sources in [docs/diagrams/src/](docs/diagrams/src/) consulted where
relevant.
**Method:** Doc-first read; spot-check load-bearing claims against source. All
findings cite specific files and lines so reviewers can verify independently.

---

## TL;DR

RotIDE is a competently-built terminal editor with an unusually disciplined
documentation set and a serious test culture (≈25k lines of tests for ≈47k
lines of production C, with ASan/UBSan in CI). The high-level container model
in [architecture-container.puml](docs/diagrams/src/architecture-container.puml)
matches the on-disk module layout, and the central invariant — *`editorDocument`
is the canonical writable text owner, `struct erow` is derived* — is real and
enforced through a single mutation entry point
([buffer_core.c:1244](src/editing/buffer_core.c#L1244)).

The architecture is, however, mid-refactor. The current shape still bears the
fingerprints of its kilo-derived origins: one huge mutable global
(`struct editorConfig E`), and a per-tab/active-buffer state split implemented
as **three near-identical 60-field copy functions**
([tabs.c:246-518](src/workspace/tabs.c#L246-L518)). That, plus a *single*
non-ESLint LSP client process for the entire editor
([lsp.c:47-60](src/language/lsp.c#L47-L60), [lsp.c:567-573](src/language/lsp.c#L567-L573))
quietly killed/respawned on every cross-language tab switch — a fact not
mentioned in the developer docs — are the two most significant gaps between
documented architecture and reality.

**Overall score: 7 / 10.** Solid foundations, well-documented intent, honest
roadmap in [PLAN.md](PLAN.md), but the central state model and the LSP
"per-tab" framing are doing more work than the docs admit.

---

## What the Documentation Gets Right

These are real architectural strengths that the docs accurately describe, and
the code confirms.

### 1. Canonical text ownership and a single edit pipeline

`editorDocument` (a [rope + line index](src/text/document.h#L11-L16)) really
is the one writable text owner. All edits — direct typing, paste, undo, redo,
column edits, autocomplete acceptance, recovery normalization — funnel through
`editorApplyDocumentEdit`
([buffer_core.c:1244-1333](src/editing/buffer_core.c#L1244-L1333)). The
descriptor `struct editorDocumentEdit`
([buffer_core.h:11-21](src/editing/buffer_core.h#L11-L21)) carries
before/after cursor and before/after dirty values together with the byte
range, so dirty-state semantics are part of the contract rather than left to
each call site. This is the strongest piece of architectural discipline in
the codebase, and the [edit-flow diagram](docs/diagrams/src/edit-flow.puml)
faithfully reflects it.

### 2. Action-based input pipeline with explicit gates

`enum editorAction` ([rotide.h:357-442](src/rotide.h#L357-L442)) is the
single keybinding contract, and `editorProcessKeypress`
([dispatch.c:1871-2059](src/input/dispatch.c#L1871-L2059)) consistently runs
synthetic-event, prompt, mouse, and terminal-pane gates **before** keymap
lookup. The order of gates in the source matches the documented diagram in
[action-dispatch.puml](docs/diagrams/src/action-dispatch.puml). This made
Phase 5 of the refactor (extracting `actions_*.c` modules) genuinely
mechanical, and is why those modules are now small and self-contained.

### 3. Pane layout has a real, narrow API

`src/workspace/layout.{c,h}` owns the binary tree and the focus-change dance,
and dispatch correctly routes split/close/focus through the layout-module
APIs rather than mutating the tree directly. The
[pane-layout diagram](docs/diagrams/src/pane-layout.puml) is unusually
precise: the focused leaf's live state lives in `E.*`; unfocused leaves keep
their `editorPaneView` snapshot; `editorLayoutSetFocusedLeaf` is the only
place that captures→swaps→loads. This is verified at
[layout.c:647-705](src/workspace/layout.c#L647-L705).

### 4. Recovery is document-first

[recovery.c](src/workspace/recovery.c) restores text through the document
model rather than through derived rows
([recovery.c:33-49](src/workspace/recovery.c#L33-L49)), and the format is
versioned (`ROTIDE_RECOVERY_MAGIC = "RTRECOV1"`, version 3, min version 2).
The autosave path is debounced rather than write-on-every-edit
([recovery.c:875-906](src/workspace/recovery.c#L875-L906)). This is a sound
design that the [save-recovery diagram](docs/diagrams/src/save-recovery.puml)
captures correctly.

### 5. DAP's `console = "terminal"` is the right call

The decision to model `console = "terminal"` by opening a real
`editorTerminalPane`, resolving `ptsname(master_fd)`, and injecting `tty`
into the launch JSON — and to strip the rotide-only `console` field before
sending — is a clean integration. See
[dap.c](src/debug/dap.c) and the (well-named)
[dap_console.{c,h}](src/debug/) split from Phase 4. It uses one mechanism
(terminal panes) to satisfy two requirements (a place to put the inferior's
I/O, and a place to host an interactive child) instead of inventing a parallel
"DAP terminal" type.

### 6. The refactor plan is honest

[PLAN.md](PLAN.md) is exceptionally clear about which phases are done, what
the line counts looked like before/after each phase, and what is intentionally
deferred. The fact that `screen.c` went `5159 → 1514` lines and `dispatch.c`
went `4773 → 2059` is verifiable from the current tree. That track record is
itself an architectural strength: this team can plan and finish multi-phase
refactors.

---

## Architectural Issues

Severity classification: **High** = blocks a near-term capability or makes
incidents likely; **Medium** = real cost on every change in this area;
**Low** = polish/clarity, not load-bearing.

### Issue 1 — One LSP process for all non-ESLint languages [High, undocumented]

**What the docs say.** [architecture.md:236-247](docs/developer/architecture.md#L236-L247)
describes LSP as "tracked per tab with document-open flags and versions"
and lists multiple supported servers (gopls, clangd, tsserver, etc.).
[README.md:79](README.md#L79) advertises "LSP definition lookup for Go, C/C++,
HTML, CSS/SCSS, JSON, and JavaScript."

**What the code does.** There are exactly two global LSP clients:
`g_lsp_client` and `g_lsp_eslint_client`
([lsp.c:47-74](src/language/lsp.c#L47-L74)). When a tab needs a different
*server kind* than the one currently running, the code kills the existing
process and spawns a new one:

```c
if (client->server_kind != EDITOR_LSP_SERVER_NONE &&
        (client->server_kind != server_kind || ...)) {
    editorLspResetTrackedDocumentsForServerKind(server_kind);
}
editorLspClientCleanup(client, 0);   // SIGTERM + waitpid
...
if (!editorLspSpawnProcess(command, &pid, ...)) { ... }
```

([lsp.c:567-583](src/language/lsp.c#L567-L583), spawn at
[lsp_transport.c:429](src/language/lsp_transport.c#L429))

**Why it matters.** If a user has one Go and one C tab open and Alt-Tabs
between them, each switch tears down and reinitializes a language server.
gopls/clangd cold-start is multi-second. Diagnostics, index, completion all
restart from scratch. The same applies across two unrelated workspace roots
of the same language. This is a substantial product limitation and an
architectural decision (the `g_lsp_client` singleton), not a configuration
bug.

The phrase "tab-local LSP state" in the docs is technically true — diagnostics,
versions, and open flags are per-tab — but it implies an architecture where
multiple servers can coexist, which is not what is built.

**Recommended actions.**
1. **Document the limitation explicitly** in
   [architecture.md](docs/developer/architecture.md) and the
   [lsp-flow diagram](docs/diagrams/src/lsp-flow.puml). One sentence costs
   nothing and prevents future confusion when readers see "tab-local" and
   assume "one server per language."
2. Plan an `lsp_registry` that keys clients by `(server_kind, workspace_root)`
   so multiple servers can coexist. This is exactly the scope-creep that
   `g_lsp_client` was probably created to avoid — but at this point the
   editor advertises six languages and the cost of cross-language tab
   switching is borne by users. Phase 9 of [PLAN.md](PLAN.md#L625-L656)
   mentions a "process lifecycle" split for LSP; that is the right slot for
   this.

---

### Issue 2 — `editorTabState` / `editorConfig E` duplication [High]

**What the docs say.** [architecture.md:21-25](docs/developer/architecture.md#L21-L25):
"Each real tab stores the same buffer-facing fields in
`struct editorTabState`. Switching tabs copies state between the active
fields and the selected tab."

**What the code does.** `struct editorConfig` (≈220 fields,
[rotide.h:649-868](src/rotide.h#L649-L868)) and `struct editorTabState`
(≈60 fields, [rotide.h:587-647](src/rotide.h#L587-L647)) overlap by
**every field on `editorTabState`**. The "swap" is implemented as three
near-identical field-by-field copies:
[`editorTabStateCaptureActive`](src/workspace/tabs.c#L246-L316),
[`editorTabStateAliasSnapshot`](src/workspace/tabs.c#L318-L381),
[`editorTabStateAliasToActive`](src/workspace/tabs.c#L383-L446), plus
[`editorTabStateLoadActive`](src/workspace/tabs.c#L448-L518) and the
duplicate field-init in
[`initEditor`](src/rotide.c#L28-L213) and
[`editorTabStateInitEmpty`](src/workspace/tabs.c#L64-L246).

This is roughly five copies of "every per-tab field." Any new per-tab field
requires touching five places. The [`extern struct editorConfig E`
search](src/) returns >3400 hits.

**Why it matters.**
- Every per-tab field addition is a five-site change with no compiler help if
  one site is missed. Silent bugs are likely (a new field that captures but
  doesn't load on focus change, etc.).
- The "active buffer is a copy" model means `E.document`, `E.rows`, search,
  selection, etc. are *aliased* into the active tab during edits. Aliasing
  works, but it makes every helper that touches `E.*` implicitly tab-coupled.
  Most of the >3400 `E.` references are tab state pretending to be globals.
- It actively blocks Phase 12 ("Shrink `rotide.h`"). You can't shrink the
  header until you've reduced the number of cross-cutting fields, and you
  can't reduce them while five sites must stay in lockstep.

**Recommended actions.**
1. Introduce `struct editorBuffer` containing exactly the per-tab fields, used
   *in place of* both copies. The "active tab" pointer becomes
   `E.active_buffer = &E.tabs[E.active_tab]` (or equivalent). Capture/load
   collapses to pointer reassignment. This is consistent with the docs'
   description of `editorDocument` ownership — extend the same discipline one
   level up.
2. Treat this as a prerequisite for, not a follow-up to, the Phase 6/7/9
   splits. Splitting `buffer_core.c` while five sites still field-copy the
   active buffer just spreads the duplication across more files.

---

### Issue 3 — `struct editorConfig E` is overloaded [Medium]

**What the docs say.** [architecture.md:13-19](docs/developer/architecture.md#L13-L19)
treats `E` as "the global editor state."

**What the code does.** `E` holds:
- Terminal/window geometry.
- *Every per-tab field* (cursor, scroll, search, selection, history, syntax,
  LSP, dirty, filename, disk state, …).
- The active drawer mode + tree + selection + 12 drawer-search fields.
- Recent files list.
- Git repo state.
- ≈25 DAP-related fields plus seven fixed-size arrays of breakpoints/threads/
  scopes/variables/etc.
- Cursor/theme/wrap/indent settings.
- The pane layout root + focused leaf.
- The keymap, popup state, clipboard buffer, `orig_attrs`, `paste_active`,
  `terminal_prefix_armed`, `task_*`, `recovery_*`, `workspace_state_*`, …

There are at least six different "kinds" of state in here masquerading as one
type. The container diagram in
[architecture-container.puml](docs/diagrams/src/architecture-container.puml)
shows seven containers; `E` is effectively their shared address space.

**Why it matters.** `E` is the implicit coupling that prevents the C4
container boundaries the diagrams draw from being enforced. The renderer
"reads `E`" — but `E` is also where tabs, panes, DAP, and config live, so the
renderer can read (and in some places write — e.g., `g_editor_drawing_current_line_highlight`
in [screen.c:76](src/render/screen.c#L76)) anything. There's no compiler
mechanism that says "Renderer is not allowed to write `E.dap_running`."

**Recommended actions.** This is intentionally Phase 12 in
[PLAN.md](PLAN.md#L713-L733), and that order is right (don't shrink the
header before the domains exist). But two lower-cost interim steps would
help:
1. Group the fields in `editorConfig` by container with comments or empty
   typedefs (`struct editorConfigDap { ... } dap;` etc.) — this is purely
   organizational but it makes the next refactor much easier to land.
2. Identify the truly *global* fields (`window_rows`, `window_cols`,
   `orig_attrs`, `keymap`, `theme`) and split them into a smaller
   `editorEnvironment` struct that the rest can depend on without dragging
   the per-tab fields along.

---

### Issue 4 — Documented startup-loop diagram shows cleanup that doesn't exist on most exits [Medium]

**What the docs say.** [startup-loop.puml](docs/diagrams/src/startup-loop.puml)
ends with `cleanup tasks, LSP, DAP, syntax, terminal pane PTYs, terminal
mode`.

**What the code does.** [main()](src/rotide.c#L247-L254) has an unconditional
`while (1) { … }`. The only exits are:
- The user's `quit` action,
  [actions_file_tab.c:82-91](src/input/actions_file_tab.c#L82-L91), which
  *does* shut down DAP, LSP, syntax worker, recovery cleanup, then
  `exit(EXIT_SUCCESS)`.
- A termination signal handler,
  [terminal.c:518-547](src/support/terminal.c#L518-L547), which restores the
  terminal and re-raises the signal so the kernel cleans up. **It does not
  call `editorLspShutdown` / `editorDapShutdown` / `editorSyntaxBackgroundStop`.**
- The `atexit` handler [terminal.c:514](src/support/terminal.c#L514) only
  restores terminal mode.

**Why it matters.** Mostly correctness-of-documentation, not correctness-of-
program: when rotide is killed (SIGHUP/SIGINT/SIGTERM/SIGQUIT) the LSP
servers, DAP adapters, and PTY children are not gracefully shut down. The
kernel will close the pipes and reap them shortly after, so this is *usually*
fine. But:
- Long-running language servers (gopls indexers) may not get the
  `shutdown`/`exit` JSON-RPC handshake they expect; some servers warn
  loudly or leave caches in an inconsistent state.
- If a SIGTERM arrives mid-edit, the recovery snapshot may not be flushed
  because `editorRecoveryMaybeAutosaveOnActivity`
  ([dispatch.c:2058](src/input/dispatch.c#L2058)) runs at end-of-keypress,
  not on signal. Combined with the 5-second autosave debounce, a worst-case
  loss is ≈5s of edits.

**Recommended actions.**
1. Either fix the diagram to show the actual exit paths (one clean, one
   signal-driven) or move the LSP/DAP/syntax shutdown calls into the signal
   handler with appropriate async-signal-safety care.
2. Consider a recovery snapshot from the signal handler. `_exit` and the
   pre-allocated snapshot path mean this is feasible if the write is bounded
   in size and uses pre-allocated buffers.

---

### Issue 5 — "Drawer is a view" is true but `pane_focus` overloads the pane vocabulary [Low]

**What the docs say.** [architecture.md:138-141](docs/developer/architecture.md#L138-L141):
"The drawer is a view over project tree entries…it does not own file text."
The pane layout is described as the binary tree of editor/terminal leaves.

**What the code does.** Consistent with the docs — the drawer is not a pane
in the layout tree. *But* `E.pane_focus` is `enum editorPaneFocus
{ EDITOR_PANE_TEXT, EDITOR_PANE_DRAWER }`
([rotide.h:143-146](src/rotide.h#L143-L146)) — a separate "focus is on text
vs drawer" toggle that uses the word "pane" alongside the actual pane tree
(`E.focused_leaf`). Readers will conflate the two.

**Why it matters.** Pure clarity. The two concepts are independent (a click on
the drawer changes `pane_focus`; arrow-pane focus changes `focused_leaf`),
but they share a noun. A future contributor adding a third focusable surface
(e.g., a search results pane) will have to invent a third overloaded name.

**Recommended action.** Rename `editorPaneFocus` →
`editorPrimaryFocus` (or similar) and update the docs accordingly. Cheap and
worth doing as part of Phase 12.

---

### Issue 6 — File watching is poll-based, undocumented [Low]

**What the docs say.** No mention of file-watching mechanism. The
architecture container diagram shows
`Rel(workspace, files, "Open, save, recover, watch")`
([architecture-container.puml:34](docs/diagrams/src/architecture-container.puml#L34))
without committing to a mechanism.

**What the code does.** [watch.c:419-447](src/workspace/watch.c#L419-L447)
polls every `EDITOR_WATCH_FILE_POLL_MS` (for editor) and
`EDITOR_WATCH_GIT_POLL_MS` (for git) by stat()ing each tab's file. There is
no inotify/kqueue.

**Why it matters.** Modest cost: with many open tabs in a large project, each
poll cycle stats every tab. For a terminal editor with O(10) tabs this is
fine. But the user-facing watch loop is not documented, so contributors will
have to read the code to understand reload behavior.

**Recommended action.** A two-line note in
[architecture.md](docs/developer/architecture.md) under "Save and Recovery"
that file watching is poll-based, with the constants. No code change.

---

### Issue 7 — `screen.c`'s frame-row cache and global render flags [Low / Medium]

**What the code does.** [screen.c:74-78](src/render/screen.c#L74-L78) holds
several module-level globals (`g_file_row_frame_cache`,
`g_editor_drawing_current_line_highlight`, etc.). The render path also
touches `editorRefreshScreen`, which performs the following on each frame:

```c
editorLspPumpNotifications();
editorDapPumpNotifications();
editorViewportUpdateForFrame();
```

([screen.c:1277-1281](src/render/screen.c#L1277-L1281))

**Why it matters.** "Renderer reads state only; pane/drawer/tab state
ownership stays in Workspace" is one of the explicit guardrails in
[PLAN.md](PLAN.md#L131). Pumping LSP/DAP notifications inside the renderer
violates that — at minimum it couples frame timing to JSON-RPC processing.
[workflows.md:37-41](docs/developer/workflows.md#L37-L41) does call this
out ("`editorRefreshScreen` is where LSP, DAP, and terminal-pane pumping
happens each frame"), so it's documented, but the choice to make the
renderer the pump for everything is worth a sentence of justification in
architecture.md. The trade-off is real: low-latency UI events trigger
refreshes, so pumping at refresh time is convenient. But it means a slow
LSP server can slow paint.

**Recommended action.** Either move LSP/DAP/viewport prep into the main loop
before `editorRefreshScreen` (keeping `screen.c` as a pure painter, which
matches the container diagram), or document the deliberate exception in
architecture.md explaining why the renderer is the pump.

---

### Issue 8 — Layout serialization loses pane kind [Low, documented]

[layout.h:359-366](src/workspace/layout.h#L359-L366) is explicit:
"kind is not preserved — terminals lapse to editor leaves on restore." This
is *documented*, but it's an architectural smell to flag: the layout tree is
the persisted thing, but kind_state (terminal panes) is session-bound. The
implication is that restoring a multi-pane workspace with a terminal pane
results in an empty editor leaf where the terminal used to be. Users may not
expect this.

**Recommended action.** Either document this in user-facing docs (README
Configuration section) or persist *something* (e.g., the working directory)
to re-launch a default shell on restore.

---

## Test Coverage

Reading the test layout: ~25k lines of tests, 14 test binaries, organized by
subsystem with shared `test_case.h`/`test_helpers.{c,h}` infrastructure. CI
runs `make`, `make test`, and `make test-sanitize`
([README.md:159-165](README.md#L159-L165)). The test-API surface
([editor_test_api.h](tests/editor_test_api.h)) is narrow: just rebuild/splice
counters for verifying invariants.

This is unusually disciplined for a project this size. The one observation:
the larger test files mirror the larger production files (4k-line tests for
4k-line modules), and Phase 13 in [PLAN.md](PLAN.md#L735-L759) plans to split
them along the new boundaries. That ordering is right.

---

## Documentation Quality

Strengths:
- Diagrams are PlantUML + committed SVG, so the markdown renders in plain
  viewers but the source is editable. The renderer (`make docs-diagrams`)
  is wired into the build.
- The two-file split (architecture for ownership, workflows for sequencing)
  is the right factoring; many projects do only one and produce a confusing
  blob.
- [README.md](docs/developer/README.md) has explicit "Documentation Rules"
  that reinforce the canonical invariants. This is a good defense against
  doc drift.
- [PLAN.md](PLAN.md) is an honest, in-progress refactor log, not aspirational
  marketing.

Gaps:
- No `error_handling.md` or `concurrency.md`. The syntax background worker
  ([syntax_worker.c:7-22](src/language/syntax_worker.c#L7-L22)) is the only
  thread in the system, and it has a documented snapshot-and-revision-check
  protocol — that's worth its own page. So is the allocation-failure
  policy: most code paths return `0` on OOM and `editorSetAllocFailureStatus`,
  but it's worth stating that as an explicit invariant.
- The LSP "one process at a time" limitation (Issue 1) needs to be in the
  docs.
- The signal-handler exit path (Issue 4) is not documented.
- File watching mechanism (Issue 6) is not documented.

---

## Risks (forward-looking)

1. **Multi-language LSP demand will force Issue 1.** As soon as a real user
   workflow involves alternating Go and TypeScript files, the kill/respawn
   cycle becomes visible. This will surface as bug reports framed as "LSP
   slow / loses diagnostics on tab switch," not as the architectural issue it
   is.
2. **The `editorTabState` / `E` duplication will block the Phase 6 / Phase 9
   splits.** Both phases want to push state ownership into smaller modules,
   but the active-buffer-as-copy model means every helper that touches `E.*`
   is implicitly tab-coupled. Without Issue 2 fixed first, those refactors
   will produce smaller files that share the same coupling.
3. **Single-rooted workspace state.** Workspace state is keyed by `cwd`
   ([workspace_state.c:83-101](src/workspace/workspace_state.c#L83-L101)).
   Opening rotide from a parent directory and from a subdirectory produces
   two unrelated state files. Acceptable for a small editor; mention in docs.
4. **Vendored libvterm, vendored Tree-sitter.** Both have warning-suppression
   carve-outs in the Makefile
   ([architecture.md:189-190](docs/developer/architecture.md#L189-L190),
   [build-and-tests.md:62-77](docs/developer/build-and-tests.md#L62-L77)).
   This is fine, but worth a CI job that periodically rebuilds with strict
   warnings on those subtrees to catch upstream regressions.
5. **DAP and LSP both spawn via `/bin/sh -c`**
   ([lsp_transport.c:452](src/language/lsp_transport.c#L452)). The doc note
   about `.rotide.toml` not being read from the project
   ([README.md:122-123](README.md#L122-L123)) is the right call. As long as
   only global config supplies these commands, this is safe. Don't relax
   that without a hard look.

---

## Recommendations, Prioritized

| # | Action | Effort | Impact |
| --- | --- | --- | --- |
| 1 | Document the single-LSP-client limitation in `architecture.md` and `lsp-flow.puml`. | XS | Prevents user/contributor confusion now. |
| 2 | Plan and execute an LSP registry keyed by `(server_kind, workspace_root)`. | L | Removes biggest visible product limitation. |
| 3 | Introduce `struct editorBuffer` and replace the field-by-field tab capture/load with pointer swap. | M | Unblocks Phases 6, 9, 12; eliminates ≈300 lines of duplication. |
| 4 | Group `struct editorConfig` fields by container (cheap) and extract a small `editorEnvironment`. | S | Lowers cognitive load for every future change. |
| 5 | Fix the startup-loop diagram or wire LSP/DAP/syntax shutdown into the signal handler. | S | Honesty + slightly cleaner exit on Ctrl-C in a real terminal. |
| 6 | Decide: renderer-as-pump (document it) vs. main-loop pump (move calls out of `editorRefreshScreen`). | S–M | Restores the "renderer is read-only" guardrail. |
| 7 | Rename `editorPaneFocus` → `editorPrimaryFocus`. | XS | Vocabulary clarity. |
| 8 | Add `error_handling.md` and `concurrency.md` pages. | S | Two real invariants (OOM policy, syntax worker revisions) deserve their own page. |
| 9 | Document file-watching as poll-based. | XS | One-line note. |
| 10 | Continue Phase 6 → 13 of [PLAN.md](PLAN.md) in the documented order. | L | Already in flight. |

---

## Overall Score: 7 / 10

**What this score means.** RotIDE is in the top quartile for terminal editors
in this size class. The fundamentals (canonical document model, action-based
input, single edit pipeline, atomic save, document-first recovery, layout
tree, vendored libvterm/Tree-sitter with sensible carve-outs) are right and
documented. The doc set itself is unusually good and clearly co-evolves with
the code via the PLAN.md ratchet.

**What keeps it from 8–9.** Two structural issues — the
single-LSP-per-editor design and the `editorTabState` / `E` field-by-field
copy model — are doing real work and are not surfaced honestly in the docs.
The first is a product limitation; the second is an internal one that will
quietly tax every future refactor. Neither is hard to fix on its own; both
need a deliberate decision rather than another phase of moving files around.

**What would get it to 9–10.** Issues 1 and 2 resolved; the renderer becomes
a true read-only painter; `rotide.h` shrinks below 400 lines as Phase 12
intends; `concurrency.md` and `error_handling.md` exist. At that point the
documented architecture and the implemented architecture are the same thing,
which is the only definition of "well-architected" that matters.
