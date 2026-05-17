# Phase 8 Refactor Plan: Retire `erow.chars`

Follow-up to [BUFFER_REFACTOR_PLAN.md](BUFFER_REFACTOR_PLAN.md) Phase 8. That phase
was deliberately scoped out of the main refactor — every caller of `row->chars`
or `row->size` has to move to a document-line query first. Six subphases below,
each ending in a green test suite so the work can land incrementally (or stall
safely between phases).

## End state

- `struct erow` holds **only** render-related fields:
  ```c
  struct erow {
      int rsize;
      int render_display_cols;
      char *render;
      int wrap_cache_body_cols;
      int wrap_cache_segment_count;
      int wrap_cache_indent_cols;
      int wrap_cache_capacity;
      int *wrap_cache_segments;
  };
  ```
  `chars` and `size` are gone.
- All cursor math, search, selection, autocomplete, syntax injection, mouse
  routing, history, input, and recovery read raw line bytes through the
  document API.
- Row cache becomes pure render metadata; building a row no longer allocates a
  per-line byte copy.
- File memory footprint on a 1 MB document drops roughly in half (was: original
  buffer + tree pieces + per-row `chars` copy + per-row `render`; now: original
  buffer + tree pieces + per-row `render`).

## Inventory (as of this branch)

27 files reference `row->chars` or `row->size` directly:
- 14 source files: [edit.c](src/editing/edit.c),
  [buffer_core.c](src/editing/buffer_core.c),
  [row_cache.c](src/editing/row_cache.c),
  [selection.c](src/editing/selection.c),
  [actions_edit.c](src/input/actions_edit.c),
  [dispatch.c](src/input/dispatch.c),
  [mouse.c](src/input/mouse.c),
  [text_pairs.c](src/input/text_pairs.c),
  [autocomplete.c](src/language/autocomplete.c),
  [syntax_worker.c](src/language/syntax_worker.c),
  [terminal_view.c](src/render/terminal_view.c),
  [row.c](src/text/row.c),
  [tabs.c](src/workspace/tabs.c),
  [watch.c](src/workspace/watch.c).
- 13 test files spread across the same areas.

~310+ direct reads. Most are cursor math (`row->chars[i]`,
`row->chars + cx`) or size comparisons (`cx >= row->size`).

## API additions

Two new document-level primitives carry the load:

```c
/* Length of line `line_idx` in bytes, excluding the trailing '\n'. */
size_t editorDocumentLineLength(const struct editorDocument *document, int line_idx);

/* Best-effort zero-copy read of line bytes. Returns a borrowed pointer + length
 * when the line lives in a single tree piece; returns NULL when the line
 * straddles pieces (caller falls back to editorDocumentLineDup).
 */
const char *editorDocumentLineBytes(const struct editorDocument *document, int line_idx,
        size_t *len_out);

/* Always-copy read; caller owns the returned NUL-terminated buffer. */
char *editorDocumentLineDup(const struct editorDocument *document, int line_idx,
        size_t *len_out);
```

A lightweight view type lets cursor helpers stay decoupled from `erow`:

```c
struct editorLineView {
    const char *data;
    int size;
    /* When `owned` is non-NULL, caller must `free(owned)` after use. NULL
     * means `data` is borrowed (tree-piece pointer) and must not be freed.
     */
    char *owned;
};

int editorDocumentLineView(const struct editorDocument *document, int line_idx,
        struct editorLineView *view_out);
void editorLineViewRelease(struct editorLineView *view);
```

This pattern (borrow when possible, allocate on miss) keeps the hot path
zero-copy while preserving correctness when a line spans pieces.

---

## Phased plan

Each phase ends green. Property tests in
[test_text_invariants.c](tests/test_text_invariants.c) gate every boundary.

### Phase 8.1 — Document-line-read APIs (~2 days)

Storage-only changes. No callers yet.

- [ ] Implement `editorDocumentLineLength`, `editorDocumentLineBytes`,
  `editorDocumentLineDup`, `editorDocumentLineView`, `editorLineViewRelease` in
  [src/text/document.{h,c}](src/text/document.c).
- [ ] Implement `editorDocumentLineBytes` by descending to the leaf, locating
  the piece containing line start, and checking whether the line's end byte
  falls in the same piece. If yes, return `piece->buf->bytes + piece->offset +
  (line_start - piece_byte_start)`. If no, return NULL.
- [ ] Implement `editorDocumentLineView` as a thin wrapper: try Bytes, fall
  back to Dup when Bytes returns NULL.
- [ ] Unit tests in [test_document_text_editing.c](tests/test_document_text_editing.c):
  - Single-piece doc — every line should hit the zero-copy path.
  - Doc with mid-line piece boundaries — those lines should fall back to copy.
  - Empty doc, doc ending in `\n`, empty trailing line.
- [ ] Property test: assert `LineDup` always matches `LineView.data[0..size)`
  and matches the bytes extracted by `editorDocumentCopyRange(start, end)`.

**Exit**: storage tests pass; zero production-code changes outside of new APIs.

**Risk**: zero-copy detection has to be cheap. The cost should be exactly one
descent for the line start and one comparison of the line-end byte against the
containing piece's bounds.

### Phase 8.2 — Migrate `row.c` helpers (~1 day)

Decouple the cursor-math primitives from `struct erow` so they can accept
either an `erow` (today) or an `editorLineView` (tomorrow).

- [ ] Add a `editorBytesCxToRx(const char *bytes, int size, int cx)` (and
  similar `_RxToCx`, `_NextCharIdx`, `_PrevCharIdx`, `_NextClusterIdx`,
  `_PrevClusterIdx`, `_ClampCxToCharBoundary`,
  `_ClampCxToClusterBoundary`, `_CxToRenderIdx`) — same logic, raw bytes
  signature.
- [ ] Reduce existing `editorRow*` wrappers to thin shims that call
  `editorBytes*` with `row->chars, row->size`.
- [ ] Existing call sites unchanged.

**Exit**: tests pass; row helpers are byte-driven internally.

**Risk**: `editorRowBuildRender` already takes `const char *chars, int size`,
which is the template — apply the same pattern to the rest.

### Phase 8.3 — Migrate non-render readers (~3 days)

For each consumer of `row->chars` / `row->size`, replace with an
`editorDocumentLineView` call. One file per commit so reviews stay small.

Order roughly by blast radius (smallest first):

- [ ] [src/input/text_pairs.c](src/input/text_pairs.c) — bracket/quote
  matching.
- [ ] [src/input/mouse.c](src/input/mouse.c) — coord → cx conversion.
- [ ] [src/input/dispatch.c](src/input/dispatch.c) — input dispatch helpers.
- [ ] [src/editing/selection.c](src/editing/selection.c) — selection bounds.
- [ ] [src/editing/edit.c](src/editing/edit.c) — cursor math for edits.
- [ ] [src/input/actions_edit.c](src/input/actions_edit.c) — input handlers.
- [ ] [src/language/autocomplete.c](src/language/autocomplete.c) — word
  prefix lookup.
- [ ] [src/language/syntax_worker.c](src/language/syntax_worker.c) — syntax
  injection that currently reads row bytes (verify; may already use the
  text source).
- [ ] [src/workspace/tabs.c](src/workspace/tabs.c),
  [src/workspace/watch.c](src/workspace/watch.c) — first-line detection and
  buffer reinit.
- [ ] [src/editing/buffer_core.c](src/editing/buffer_core.c) — last holdouts,
  including the `first_line = E.rows[0].chars` syntax-detection probe.

Pattern at each call site:

```c
struct editorLineView line = {0};
if (!editorDocumentLineView(E.document, cy, &line)) {
    /* alloc failure — set status, bail. */
    return;
}
/* …use line.data, line.size… */
editorLineViewRelease(&line);
```

For inner loops that re-read the same line many times, hoist the view above the
loop.

**Exit**: every direct `row->chars` / `row->size` consumer outside the render
layer goes through the document. `grep -rn '->chars\|->size' src/` returns only
render and row-cache uses.

**Risk**: lifetime bugs from borrowed pointers staying valid across an edit.
Mitigation: views are scoped to a single function call; edits invalidate
nothing because views are short-lived. Property tests in
[test_text_invariants.c](tests/test_text_invariants.c) catch correctness drift.

### Phase 8.4 — Migrate tests (~1 day)

Tests assert directly on `row->chars` content. Either:
- (a) replace with `editorDocumentLineDup` reads and free, or
- (b) add a tiny test helper `ASSERT_ROW_TEXT_EQ(cy, expected)` that does the
  document read internally.

Option (b) keeps test diffs tight.

- [ ] Add `ASSERT_ROW_TEXT_EQ` / `editor_test_row_text(cy)` in
  [tests/test_helpers.h](tests/test_helpers.h).
- [ ] Sweep all `ASSERT_EQ_STR(..., E.rows[r].chars)` / `tab->rows[r].chars`
  call sites across the 13 test files.

**Exit**: tests pass without referencing `erow.chars` / `erow.size`.

### Phase 8.5 — Delete the storage (~1 day)

The compiler does the work.

- [ ] Stop populating `chars` / `size` in
  [editorBuildRowsFromDocumentRange](src/editing/row_cache.c) — only `render`
  and wrap-cache fields are set.
- [ ] Remove `free(buffer->rows[i].chars)` from
  [editorFreeRowArray](src/editing/row_cache.c) (or wherever the lifecycle
  lives) and from `editorBufferFreeRows` in
  [src/workspace/tabs.c](src/workspace/tabs.c).
- [ ] Remove `chars` and `size` from `struct erow` in
  [src/rotide.h](src/rotide.h).
- [ ] Delete the `editorRow*` shims added in Phase 8.2 (now unused); only the
  `editorBytes*` helpers remain.

**Exit**: clean build. Any straggler reference crashes at compile time and is
fixed.

### Phase 8.6 — Verification + benchmarks (~1 day)

- [ ] Run `make test` and `make test-sanitize`; expect 821/821 green.
- [ ] Run `tests/bench_text_storage`; expect open_reset MB/s unchanged
  (storage path didn't change) and confirm no random-insert regression.
- [ ] Add a memory-footprint check (one-shot) measuring `RSS` after loading a
  large file; expect drop proportional to the file size that previously lived
  in row chars.
- [ ] Update [BUFFER_REFACTOR_PLAN.md](BUFFER_REFACTOR_PLAN.md) Phase 8 entry
  to `[x]` and link to this file's history.

**Exit**: tests + sanitizer green; benchmark documents the memory win.

---

## Risk register

| Risk | Phase | Mitigation |
|---|---|---|
| Borrowed-pointer use-after-edit | 8.3 | Views are short-lived; never held across `editorDocumentReplaceRange`. Property tests catch any drift. |
| Cross-piece lines always copy | 8.3, 8.6 | Phase 7 coalescing keeps typing runs in single pieces. Bulk-loaded original-buffer lines are always in one piece. Measure miss rate; if high in practice, add a small per-line cache (one most-recent line). |
| Test harness churn explodes the diff | 8.4 | Helper macro keeps each test file change to a single line per assertion. |
| Render path silently broken | 8.5 | `render` is independent and unchanged. The chrome/render tests gate this. |
| Syntax worker reads row chars on background thread | 8.3 | Confirm worker reads via text source (zero-copy) before this phase; if not, fix worker first. |

## Open questions

1. **Per-line cache for repeat lookups?** Cursor math inside a single keystroke
   often reads the same line many times. If `editorDocumentLineView` shows up
   in profiles, a single-entry cache keyed by line index would absorb the
   repeat traffic. Defer until profile justifies.
2. **Should `editorBytes*` helpers take `editorLineView` directly?** Currently
   designed to take `(bytes, size)` so they're trivially testable. Wrapping
   them in `LineView`-aware overloads is cheap if it makes call sites tidier.
3. **What about `editorDocumentLineEndByte` consumers that compute `size`?**
   `editorDocumentLineLength` is a thin wrapper around `End - Start`; existing
   `editorDocumentLineEndByte` callers might prefer to keep using bounds and
   never go through the new API. Both work; choose at the call site.

## What this plan does NOT change

- The storage tree, summaries, piece coalescing — all stable post-Phase 7.
- `editorTextSource` interface — tree-sitter and LSP unaffected.
- `render` / `wrap_cache_*` fields on `erow` — render layer keeps its
  display-text cache. Only the *raw byte copy* goes away.
- Save/recovery paths — already stream via `editorTextSource`, not `erow`.
- Undo/redo — already stores byte offsets and string snapshots, not row refs.

## Estimated effort

~1 week of focused work, landable in 6 commits (one per subphase), each
reviewable in isolation. The biggest blast radius is Phase 8.3; the rest are
mechanical.
