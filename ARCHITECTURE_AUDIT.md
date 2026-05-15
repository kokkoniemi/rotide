# RotIDE Architecture Audit — Revision 3

**Date:** 2026-05-15 (Revision 3, after Phase 10 close-out)
**Scope:** [docs/developer/](docs/developer/) and [PLAN.md](PLAN.md) treated as
source of truth; current source under [src/](src/) verified against doc
claims. Diagram sources at [docs/diagrams/src/](docs/diagrams/src/).
**Score history:** Rev 1: 7/10 · Rev 2: 8.5/10 · **Rev 3: 9 / 10.**

## What Changed Since Revision 2

Phase 10 is now closed out. Beyond the registry, mock, features, and
documents splits noted in Rev 2, the implicit `g_lsp_client` /
`g_lsp_eslint_client` singletons are now fully gone: per-request and
per-notification code captures an explicit `struct editorLspClient *`
via `editorLspEnsureClientForFile(...)` / `editorLspEnsureEslintClientForFile(...)`
at the top of each function, then uses `client->...` for every fd, version,
and cleanup call. The `#define g_lsp_client (*editorLspPrimaryClient())`
shim is deleted; the bare `editorLspSendRawJson(json)` wrapper that hid the
"active client fd" is deleted.

Cumulative impact on the hotspots is now:

| File | Rev 1 baseline | Current | Δ |
| --- | --- | --- | --- |
| `src/render/screen.c` | 5,159 | 1,514 | −71% |
| `src/input/dispatch.c` | 4,773 | 2,059 | −57% |
| `src/editing/buffer_core.c` | 2,584 | 616 | −76% |
| `src/workspace/drawer.c` | 2,984 | 316 | −89% |
| `src/language/lsp.c` | 2,209 | **766** | **−65%** |
| `src/language/lsp_protocol.c` | 2,013 | 785 | −61% |
| `src/language/syntax.c` | 3,042 | 3,042 | unchanged (Phase 11 pending) |

New focused modules under `src/language/`: `lsp_responses.c` (1,121),
`lsp_documents.c` (643), `lsp_features.c` (564), `lsp_mock.{c,h}`
(233 + 49), `lsp_registry.{c,h}` (177 + 17), plus growth of `lsp_json.c`
(461 → 580) for the promoted shared parsing helpers.

Source tree size is roughly flat (~47k lines) — the work was reorganization,
not deletion. That's the right outcome: behavior preserved, ownership
clarified.

---

## Status of Revision 1 Issues

### Issue 1 — Single LSP process per editor → **Fully resolved**

A real `(server_kind, workspace_root)` registry is now in place at
[lsp_registry.c](src/language/lsp_registry.c) with a 16-client cap.
Switching between a Go and a C tab reuses both live servers instead of
killing and respawning; opening two workspaces of the same language uses
two clients.

**The Rev 2 transitional concern is also resolved.** The
`g_lsp_client` / `g_lsp_eslint_client` macros that hid the "active client"
singleton are deleted. Per-request and per-notification code now captures
an explicit client pointer at the top of each function:

```c
struct editorLspClient *client = editorLspEnsureClientForFile(filename, language);
if (client == NULL) {
    return -1;
}
... client->next_request_id ...
editorLspSendRawJsonToFd(client->to_server_fd, payload);
editorLspClientCleanup(client, 0);
```

See e.g. [lsp_features.c:57-105](src/language/lsp_features.c#L57-L105),
[lsp_documents.c:35-110](src/language/lsp_documents.c#L35-L110). All ~50
former `g_lsp_client.*` use sites have been rewritten; the bare
`editorLspSendRawJson(json)` wrapper that read the active client's fd
implicitly is also gone.

The registry still tracks "primary_active" / "eslint_active" pointers
internally, but they are now an implementation detail of
`editorLspPrimaryClient()` / `editorLspEslintClient()`, called only by
state-query accessors (e.g. "is completion supported on the currently
active server?"). Every code path that actually performs a request or
notification is explicit about which client it's talking to.

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

- **Phase 11**: split `syntax.c` (3,042 lines, biggest single remaining file).
- **Phase 12**: config/theme boilerplate.
- **Phase 13**: `rotide.h` shrink including grouping `editorConfig`, extracting
  `editorEnvironment`, renaming `editorPaneFocus`.
- **Phase 14**: split oversized test files.
- **Optional**: a further split of the remaining `lsp.c` (766 lines) into a
  narrower `lsp_client.{c,h}`. Coherent at current size; lower priority
  than Phase 11.
- **Cross-cutting**: doc sync (LSP registry + `editorBuffer` + Phase 10
  splits), signal-handler shutdown decision, watch poll docs (done),
  render-pump decision.

---

## Updated Risk Register

1. **`syntax.c` still 3k lines** — Phase 11's deferral until Phase 8 was the
   right call, but it's now the single largest file in the codebase and
   handles parse, captures, injections, and budgeting in one place. No
   functional risk; just the next obvious refactor target.
2. **Doc drift** — the gap between docs and code is bigger than it was at
   Rev 1, because the code moved and the docs only partially caught up
   (watch poll mechanism is now documented; LSP registry, `editorBuffer`,
   and the Phase 10 splits are not yet reflected in architecture.md or
   the diagrams).
3. **Vendored libraries** — unchanged (still libvterm 0.3.x and pinned
   Tree-sitter grammars, warning-relaxed). No new issues.

---

## Updated Recommendations, Prioritized

| # | Action | Effort | Impact | Status vs Rev 2 |
| --- | --- | --- | --- | --- |
| 1 | Doc-sync slice: LSP registry, `editorBuffer`, Phase 10 module map, pane-layout diagram. | S | Closes the standing doc gap; no longer "one revision behind" — now two. | Carried, more urgent. |
| 2 | Phase 11 syntax core split. | M | Largest remaining file; same playbook as `screen.c`. | Unchanged. |
| 3 | Phase 13 `rotide.h` shrink, `editorPaneFocus` rename, `editorEnvironment` extraction. | M | Continues clarifying the global root. | Unchanged. |
| 4 | Decide & document the `editorRefreshScreen` pumping question. | S | Restores renderer-as-painter guardrail or formalizes the exception. | Unchanged. |
| 5 | Wire LSP/DAP/syntax shutdown into signal handler OR fix startup-loop diagram. | S | Honesty + graceful exit. | Unchanged. |
| 6 | Add LSP registry LRU + an eviction test. | S | Locks in the registry contract; matters more now that multi-client coexistence is the steady state. | Unchanged. |
| 7 | Add `concurrency.md` (syntax worker) and `error_handling.md` (OOM policy). | S | Two real invariants without a home. | Unchanged. |
| 8 | Phase 14 test-file split. | M | After production split stabilizes. | Unchanged. |

---

## Updated Score: 9 / 10

**What changed since the 8.5.**

Phase 10 is now fully closed: the explicit-client-pointer refactor is
done, the `g_lsp_client` and `g_lsp_eslint_client` macros are deleted, and
the now-redundant `editorLspSendRawJson` wrapper is gone. Every per-request
and per-notification code path in `lsp_features.c` and `lsp_documents.c`
captures its client explicitly via `editorLspEnsureClientForFile(...)` /
`editorLspEnsureEslintClientForFile(...)`. The implicit "active client"
pointer that Rev 1 flagged and Rev 2 noted as a transitional smell is gone
from every code path that actually performs a request — it survives only as
an internal detail of the registry's primary/eslint pointers, accessed
through `editorLspPrimaryClient()` / `editorLspEslintClient()` and used
only by genuine state-query accessors.

Phase 10 also delivered three more file splits since Rev 2 —
`lsp_mock.{c,h}`, `lsp_features.c`, and `lsp_documents.c` — taking `lsp.c`
from 2,192 to 766 lines (−65% versus Rev 1 baseline) while preserving 771/771
test pass under `make test` and the sanitizer build.

**Why not 10 yet.**

- Docs lag two revisions of code now (Phase 10's registry, the
  `editorBuffer`, and the new module map are still absent from
  `architecture.md` and the diagrams). One focused doc-sync slice closes
  this; no code change required.
- `syntax.c` (Phase 11), the `editorRefreshScreen` pumping decision, the
  signal-handler shutdown decision, and `editorPaneFocus` rename are all
  still standing.
- `concurrency.md` and `error_handling.md` still don't exist.

**What gets it to 10.**

The doc-sync slice plus Phase 11 (syntax) plus the four small standing
items. None is hard; all are cheap. At that point the documented
architecture, the diagrams, and the implemented architecture agree
exactly, and the only oversized file in the tree is part of a finished
plan.
