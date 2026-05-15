# RotIDE Architecture Audit — Revision 2

**Date:** 2026-05-15 (Revision 2, after Phases 6–10)
**Scope:** [docs/developer/](docs/developer/) and [PLAN.md](PLAN.md) treated as
source of truth; current source under [src/](src/) verified against doc
claims. Diagram sources at [docs/diagrams/src/](docs/diagrams/src/).
**Previous score:** 7/10 (Revision 1).
**New score: 8.5 / 10.**

## What Changed Since Revision 1

The team has landed 48 commits since the original audit. The two **High** issues
from Revision 1 — single LSP process and `editorTabState` / `E` field-copy
duplication — are now structurally addressed in code. Phases 6, 7, 8, 9 are
complete and Phase 10 is in-progress with the LSP registry already merged. The
hotspots have shrunk dramatically:

| File | Rev 1 baseline | Current | Δ |
| --- | --- | --- | --- |
| `src/render/screen.c` | 5,159 | 1,514 | −71% |
| `src/input/dispatch.c` | 4,773 | 2,059 | −57% |
| `src/editing/buffer_core.c` | 2,584 | 616 | −76% |
| `src/workspace/drawer.c` | 2,984 | 316 | −89% |
| `src/language/lsp.c` | 2,209 | 2,192 | −0.8% (but registry is in) |
| `src/language/syntax.c` | 3,042 | 3,042 | unchanged (Phase 11 pending) |

Source tree size is roughly flat (~47k lines) — the work was reorganization,
not deletion. That's the right outcome: behavior preserved, ownership
clarified.

---

## Status of Revision 1 Issues

### Issue 1 — Single LSP process per editor → **Resolved (transitional)**

A real `(server_kind, workspace_root)` registry is now in place at
[lsp_registry.c](src/language/lsp_registry.c) with a 16-client cap. The
critical fix is at
[lsp.c:531-551](src/language/lsp.c#L531-L551):

```c
struct editorLspClient *client =
        editorLspRegistryAcquireClient(server_kind, workspace_root_path);
...
editorLspRegistrySetActiveClient(server_kind, client);
...
if (client->initialized && client->server_kind == server_kind &&
        editorLspProcessAlive(client) &&
        editorLspWorkspaceRootsMatch(client->workspace_root_path,
                workspace_root_path)) {
    free(workspace_root_path);
    return 1;          // reuse live client; no kill/respawn
}
```

Switching between a Go and a C tab now reuses both live servers instead of
killing and respawning. Opening two workspaces of the same language uses two
clients. That was the entire intent.

**Transitional smell, not a blocker.** The per-call code paths
(`editorLspSendRawJson`, `editorLspNotifyDidChange`, completion) still talk
through the `g_lsp_client` macro, which is now
`*editorLspPrimaryClient()` — i.e., whatever the registry's `primary_active`
pointer last got set to
([lsp_transport.h:49-50](src/language/lsp_transport.h#L49-L50)). Because every
notify/request path calls `editorLspEnsureRunningForFile(...)` first — which
calls `editorLspRegistrySetActiveClient` for the file's `(server_kind, root)`
— the right client is selected before each operation. This is correct under
the editor's single-threaded model. But the implicit "active client" pointer
is now the next thing to clean up: pass the client explicitly to
didChange/didSave/completion, then remove `g_lsp_client` entirely. Phase 10
already lists `lsp_client.{c,h}` as a target, so this slot is the natural
home for that follow-up.

### Issue 2 — `editorTabState` / `E` field-by-field copy → **Resolved**

This is the cleanest fix of the batch. [rotide.h:587-683](src/rotide.h#L587-L683)
declares one X-macro field list:

```c
#define EDITOR_ACTIVE_BUFFER_FIELDS(X) \
    EDITOR_ACTIVE_BUFFER_CORE_FIELDS(X) \
    EDITOR_ACTIVE_BUFFER_LSP_FIELDS(X) \
    EDITOR_ACTIVE_BUFFER_SEARCH_FIELDS(X) \
    EDITOR_ACTIVE_BUFFER_EDIT_FIELDS(X)

struct editorBuffer {
    EDITOR_ACTIVE_BUFFER_FIELDS(EDITOR_DECLARE_FIELD)
};

struct editorTabState {
    union {
        struct editorBuffer buffer;
        struct { EDITOR_ACTIVE_BUFFER_FIELDS(EDITOR_DECLARE_FIELD) };
    };
};

struct editorConfig {
    ...
    union {
        struct editorBuffer active_buffer;
        struct { EDITOR_ACTIVE_BUFFER_FIELDS(EDITOR_DECLARE_FIELD) };
    };
    ...
};
```

Capture and load are now [single struct moves](src/workspace/tabs.c#L194-L230):

```c
static void editorTabStateCaptureActive(struct editorTabState *tab) {
    editorTabStateFree(tab);
    ...
    editorBufferMove(&tab->buffer, &E.active_buffer);
}

static void editorTabStateLoadActive(struct editorTabState *tab) {
    editorBufferMove(&E.active_buffer, &tab->buffer);
    ...
}
```

Three near-identical 60-field copy functions collapsed to one struct
assignment per direction. The compatibility union preserves `E.cx` /
`tab->cx` access, so the migration didn't require touching every call site.
Direct `tab->...` access is now confined to `tabs.c` itself
(`grep tab->cx` returns nine hits, all in
[tabs.c:902-915](src/workspace/tabs.c#L902-L915)), and cross-domain access
goes through [`editorTabBufferHandleAt*`](src/workspace/tabs.c#L150-L165)
accessors. Adding a per-tab field is now one line in the X-macro list.

This is a model resolution: the duplication is *gone*, but legacy call-site
syntax (`tab->cx`, `E.cx`) keeps working through the union. The compatibility
union should eventually disappear after Phase 13, but it carries no cost.

### Issue 3 — `editorConfig` overloading → **Largely deferred to Phase 13, partly mitigated**

The macro grouping (`EDITOR_ACTIVE_BUFFER_CORE_FIELDS`,
`..._LSP_FIELDS`, `..._SEARCH_FIELDS`, `..._EDIT_FIELDS`) gives readers the
container structure for the per-tab portion of `E`. The non-tab portion of
`editorConfig` is unchanged. PLAN.md Phase 13 still owns the rest.

### Issue 4 — Startup-loop diagram vs signal-exit reality → **Still open**

[`main()`](src/rotide.c) still has the unconditional `while(1)`; signal
handling still doesn't run LSP/DAP/syntax shutdown. PLAN.md acknowledges this
("keep architecture diagrams honest about current exit paths") but no slice
has landed for it. Recommendation unchanged: either fix the diagram or wire
the shutdowns into the signal handler.

### Issue 5 — `pane_focus` vocabulary overload → **Still open**

PLAN.md Phase 13 owns this rename. No change yet.

### Issue 6 — File watching poll mechanism undocumented → **Plan updated, doc not yet**

Phase 9 explicitly says "keep watch poll-based unless changed explicitly,"
but [architecture.md](docs/developer/architecture.md) still doesn't mention
the polling mechanism. One-sentence doc change still pending.

### Issue 7 — `editorRefreshScreen` pumps LSP/DAP/viewport → **Decision deferred, surfaced in PLAN**

Phase 3 audit follow-up in PLAN.md now reads:

> Decide whether frame-time pumping … remains a documented exception inside
> `editorRefreshScreen`, or move it into the main loop before rendering …

The decision is queued, but the code is unchanged and the docs don't reflect
either choice. Same advice as Rev 1: pick one and document it.

### Issue 8 — Layout serialization loses pane kind → **Unchanged, still documented in header**

No new work; not a load-bearing issue.

---

## New Observations From This Revision

### Strength A — The X-macro field list is the right abstraction

This is worth calling out explicitly because it's the kind of choice that
ages well. The temptation when fixing Issue 2 would have been a
heavyweight redesign: introduce an opaque `editorBuffer` type, hide the
fields behind getters/setters, force every call site to change. Instead the
fix is a single declarative list of field types and names that both structs
expand into. The compatibility union means the rest of the codebase didn't
have to be rewritten in the same slice. That's a load-bearing piece of taste.

### Strength B — `buffer_core.c` shrinkage is real, not just renaming

The 2,584 → 616 line drop on `buffer_core.c` came with seven new files
(`document_bridge`, `document_position`, `text_source`, `row_cache`,
`edit_pipeline`, `buffer_search`, `post_edit_notify`, `syntax_runtime.h`),
each with a narrow header. `buffer_core.c` is no longer the catch-all bridge
it used to be. The `editorApplyDocumentEdit` contract has *moved* to
[edit_pipeline.h](src/editing/edit_pipeline.h) where it belongs.

The one design decision worth noting: the Text engine → Language services
relationship is now an explicit narrow bridge
([post_edit_notify.{c,h}](src/editing/), [syntax_runtime.h](src/editing/syntax_runtime.h)).
That's the right factoring — the edit pipeline doesn't directly call into
syntax/LSP modules, it calls one notify function. This is the kind of seam
the C4 container diagram has been promising and it finally exists.

### Strength C — Drawer split is the textbook outcome

`drawer.c` went 2,984 → 316 lines, fragmenting into nine focused files
covering tree, modes (menu/git/lsp/dap), layout, file ops. The renderer
still consumes [drawer_view.c](src/render/drawer_view.c) view entries, not
tree internals — that boundary held throughout.

### New Issue (Low) — LSP request paths still call through `g_lsp_client` macro

This was flagged above under Issue 1's resolution. To restate as a discrete
finding: the registry stores N clients, but the per-call code still
references `g_lsp_client` (the active primary). The next step is to remove
the implicit "active client" and pass the client explicitly into request
helpers. Functionally fine today; structurally still a singleton pretending
otherwise. Phase 10's `lsp_client.{c,h}` extraction is the right home.

**Recommended action.** When `lsp.c` is split into `lsp_client.{c,h}`,
`lsp_requests.{c,h}`, etc., make the request helpers take an
`editorLspClient *` parameter and delete the `g_lsp_client` macro. This is
mostly a sed job once the splits are mechanical.

### New Issue (Low) — LSP registry has a 16-client cap and a thrash mode

[lsp_registry.c:6](src/language/lsp_registry.c#L6) caps clients at 16. When
full, [`editorLspRegistrySelectEvictionCandidate`](src/language/lsp_registry.c#L77-L101)
picks a non-active client to evict, preferring dead or `EDITOR_LSP_SERVER_NONE`
slots, else any non-active client, else the first slot. This is fine for
the editor's intended scale (a user is unlikely to span 16 simultaneous
`(server_kind, workspace_root)` pairs), but two things are worth noting:

1. The eviction policy has no LRU; "first non-active client" is the
   fallback. A pathological case (open files from many roots cyclically)
   would thrash. Probably never hits in practice, but worth a comment in
   the code or a short note in docs.
2. There's no test in [tests/test_lsp.c](tests/test_lsp.c) for the
   eviction path (`grep` for `editorLspRegistry` in tests returns nothing).
   Worth adding one before Phase 10 closes, so the policy is pinned.

**Recommended action.** Add an LRU touch on `editorLspRegistrySetActiveClient`
and one test covering both reuse and eviction. Small effort, locks in the
key behavior.

### New Observation — Docs lag the code by one revision

architecture.md, the LSP diagrams, and the LSP-document-lifecycle state
machine all still describe the pre-registry world ("LSP state is tracked
per tab"). The `editorBuffer` union is invisible in `pane-layout.puml`
(which still says "live focused-pane view" inside `E`). This is mostly
fine — the docs aren't *wrong*, they're outdated — but the gap will grow
if Phase 10 lands the rest of the LSP split without a doc pass.

**Recommended action.** Schedule a single doc-sync slice between Phase 10
and Phase 11. Targets:
- LSP section in [architecture.md](docs/developer/architecture.md): describe
  the registry, the `(server_kind, workspace_root)` keying, and the eviction
  bound.
- [pane-layout.puml](docs/diagrams/src/pane-layout.puml): replace "live
  focused-pane view" prose with `struct editorBuffer` and the
  `E.active_buffer` / `tab->buffer` union.
- [lsp-document-lifecycle.puml](docs/diagrams/src/lsp-document-lifecycle.puml):
  add a note that the underlying *client* can be reused across tabs, and that
  tab-local state still owns version/diagnostics/open flags.

---

## What's Still Pending From PLAN.md

These are queued, not problems:

- **Phase 10 finish**: `lsp_client.{c,h}`, `lsp_requests.{c,h}`,
  `lsp_responses.{c,h}`, `lsp_features.{c,h}` splits, plus delete
  `g_lsp_client` macro.
- **Phase 11**: split `syntax.c` (3,042 lines, biggest single remaining file).
- **Phase 12**: config/theme boilerplate.
- **Phase 13**: `rotide.h` shrink including grouping `editorConfig`, extracting
  `editorEnvironment`, renaming `editorPaneFocus`.
- **Phase 14**: split oversized test files.
- **Cross-cutting**: doc sync after Phase 10, signal-handler shutdown decision,
  watch poll docs, render-pump decision.

---

## Updated Risk Register

1. **Phase 10 mid-flight** — registry is in but request helpers aren't yet
   split. The transitional `g_lsp_client` macro is a soft coupling that will
   complicate future LSP work until removed. Low if Phase 10 lands soon;
   moderate if it stalls.
2. **`syntax.c` still 3k lines** — Phase 11's deferral until Phase 8 was the
   right call, but it's now the single largest file in the codebase and
   handles parse, captures, injections, and budgeting in one place. No
   functional risk; just the next obvious refactor target.
3. **Doc drift** — the gap between docs and code is bigger than it was at
   Rev 1, because the code moved and the docs didn't. Acceptable for a
   week; problematic if it lasts a quarter.
4. **Vendored libraries** — unchanged (still libvterm 0.3.x and pinned
   Tree-sitter grammars, warning-relaxed). No new issues.

---

## Updated Recommendations, Prioritized

| # | Action | Effort | Impact | Status vs Rev 1 |
| --- | --- | --- | --- | --- |
| 1 | Finish Phase 10: split `lsp.c`, pass `editorLspClient *` explicitly, delete `g_lsp_client` macro. | M | Removes last LSP singleton. | New (replaces old #1/#2). |
| 2 | Doc-sync slice: LSP registry, `editorBuffer`, pane-layout diagram. | S | Closes the Rev-1→Rev-2 doc gap. | New. |
| 3 | Phase 11 syntax core split. | M | Largest remaining file; same playbook as `screen.c`. | Unchanged. |
| 4 | Phase 13 `rotide.h` shrink, `editorPaneFocus` rename, `editorEnvironment` extraction. | M | Continues clarifying the global root. | Partially mitigated. |
| 5 | Decide & document the `editorRefreshScreen` pumping question. | S | Restores renderer-as-painter guardrail or formalizes the exception. | Unchanged. |
| 6 | Wire LSP/DAP/syntax shutdown into signal handler OR fix startup-loop diagram. | S | Honesty + graceful exit. | Unchanged. |
| 7 | Document file-watch as poll-based; one-sentence fix. | XS | Closes a documented-gap issue. | Unchanged. |
| 8 | Add LSP registry LRU + an eviction test. | S | Locks in the registry contract. | New. |
| 9 | Add `concurrency.md` (syntax worker) and `error_handling.md` (OOM policy). | S | Two real invariants without a home. | Unchanged. |
| 10 | Phase 14 test-file split. | M | After production split stabilizes. | Unchanged. |

---

## Updated Score: 8.5 / 10

**What changed since the 7.**

The two highest-severity items from Revision 1 have been substantively fixed
in the code (Issue 1 via the registry, Issue 2 via the `editorBuffer`
union/X-macro), and the work landed in small, validated slices with the
`make`/`make test`/`make test-sanitize` cadence preserved throughout. Both
solutions show good taste — the X-macro field list and the (server_kind,
root) keying are exactly what those problems wanted. The bulk refactor work
(Phases 6–9) executed cleanly, with line-count reductions matched by new
focused modules and no behavioral regressions in 771 tests.

**Why not 9 yet.**

- Phase 10 is mid-flight. The registry exists, but request helpers still
  flow through the `g_lsp_client` macro. The architectural intent (multi-
  client coexistence) is in place; the call-site discipline that would let
  you delete the macro is not.
- Docs lag the code by one full revision. architecture.md, the LSP diagrams,
  and `pane-layout.puml` still describe the pre-registry, pre-`editorBuffer`
  world. The gap is honest (PLAN.md acknowledges it) but real.
- Several lower-severity Rev 1 items (signal-handler exit path, watch
  polling, refresh-pumping decision, `editorPaneFocus` rename,
  `concurrency.md`/`error_handling.md`) remain queued.

**What gets it to 9–10.**

Finish Phase 10 (registry usage made explicit at every call site, macro
deleted), land the doc-sync slice, complete `syntax.c` split (Phase 11), and
clear the four small standing items (signal handler, refresh pumping
decision, watch docs, `editorPaneFocus` rename). At that point the
documented architecture, the diagrams, and the implemented architecture
agree, and there are no implicit singletons left in the model.

A 10 would require those plus the optional polish — `concurrency.md`,
`error_handling.md`, the LSP registry eviction test, and a finished
`rotide.h` shrink — none of which is hard, all of which is cheap once the
prerequisites land.
