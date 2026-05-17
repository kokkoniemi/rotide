# Buffer Refactor Plan: SumTree-of-Pieces

Companion to [BUFFER_AUDIT.md](BUFFER_AUDIT.md). Concrete, phased plan for replacing the flat 1024-byte chunk array in [src/text/rope.c](src/text/rope.c) with a Zed-style B-tree of text pieces whose internal nodes carry summary statistics.

The audit gave the direction; this document gives the path.

## Goals (end state)

1. One storage tree, no parallel line index. `editorDocument` collapses from `{ rope + line_starts[] }` to `{ tree }`.
2. All position queries (byte→line, line→byte, locate, read) are `O(log n)` via summary-guided descent.
3. Edits are `O(log n + |insert|)` with no `memmove` over the whole chunk array.
4. Tree leaves are pieces — slices into shared immutable byte buffers — so file loads and most reads are zero-copy.
5. Tree-sitter and LSP integration unchanged: they consume `editorTextSource`, which keeps its current contract.
6. ~250 lines of incremental line-index code retired ([src/text/document.c:38-366](src/text/document.c#L38-L366)).
7. `editorBufferMaxRenderCols` answered from the tree summary, not by walking every row.
8. Property tests in [tests/test_text_invariants.c](tests/test_text_invariants.c) keep passing at every phase boundary; that harness is the safety net for the whole migration.

## Target architecture (sketch)

### Types

```c
/* Mergeable summary carried on every node and every piece. */
struct editorTextSummary {
    size_t bytes;             /* total bytes in subtree                  */
    int newlines;             /* count of '\n' in subtree                */
    /* Max-line tracking. "Line" = bytes between two '\n' (or doc edges). */
    size_t first_line_bytes;  /* bytes from subtree start to first '\n'  */
    size_t last_line_bytes;   /* bytes from last '\n' to subtree end     */
    size_t max_line_bytes;    /* longest complete line *inside* subtree  */
};

/* Immutable byte buffer with refcount. Two kinds in practice:
 *   - "original" buffers, one per loaded file (lifetime = tab lifetime);
 *   - one growing "add" buffer per document, appended-to on inserts.
 */
struct editorTextBuffer {
    char *bytes;
    size_t len;
    size_t capacity;
    int ref_count;
};

/* A leaf entry: a slice of an immutable buffer. */
struct editorTextPiece {
    struct editorTextBuffer *buf;
    size_t offset;
    size_t len;
    struct editorTextSummary summary;   /* over [offset, offset+len)     */
};

/* B-tree node. Internal nodes hold child pointers + per-child summaries.
 * Leaf nodes hold pieces + per-piece summaries (same as piece.summary).
 */
#define ROTIDE_TEXT_TREE_FANOUT 16
struct editorTextNode {
    unsigned char is_leaf;
    unsigned char count;                /* # of valid children/pieces    */
    struct editorTextSummary summary;   /* over this entire subtree      */
    union {
        struct editorTextNode  *children[ROTIDE_TEXT_TREE_FANOUT];
        struct editorTextPiece  pieces[ROTIDE_TEXT_TREE_FANOUT];
    } u;
};

struct editorTextTree {
    struct editorTextNode *root;        /* never NULL; empty doc = empty leaf */
};
```

### Summary merge

Associative combine `merge(left, right) -> out`:

```
out.bytes   = left.bytes + right.bytes
out.newlines = left.newlines + right.newlines

boundary    = left.last_line_bytes + right.first_line_bytes  /* spans the seam */

out.first_line_bytes =
    left.newlines == 0 ? left.bytes + right.first_line_bytes
                       : left.first_line_bytes

out.last_line_bytes =
    right.newlines == 0 ? left.last_line_bytes + right.bytes
                        : right.last_line_bytes

out.max_line_bytes = max3(left.max_line_bytes,
                          right.max_line_bytes,
                          boundary)
```

At the root, `max_line_bytes_overall = max(root.max_line_bytes, root.first_line_bytes, root.last_line_bytes)` — first/last lines are complete at document level.

### Descent operations (new APIs on the tree)

| API | Semantics | Replaces |
|---|---|---|
| `editorTextTreeLocateByte(t, b, &leaf, &leaf_idx, &local_off)` | find piece containing byte `b` | `editorRopeLocateBoundary` / `editorRopeRead` walk |
| `editorTextTreeLocateLine(t, line_idx, &byte)` | start byte of line `line_idx` | `editorDocumentLineStartByte` reading `line_starts` |
| `editorTextTreeLineForByte(t, b, &line_idx)` | line containing byte `b` | `editorDocumentLineIndexForByteOffset` binary search |
| `editorTextTreeRead(t, b, &bytes_avail)` | zero-copy pointer at byte `b` | `editorRopeRead` |
| `editorTextTreeCopyRange(t, lo, hi, dst)` | bulk copy | `editorRopeCopyRange` |
| `editorTextTreeReplaceRange(t, lo, old_len, src, new_len)` | edit | `editorRopeReplaceRange` |
| `editorTextTreeSummary(t)` | root summary | (new) — used by `editorBufferMaxRenderCols`, line count, length |

The tree-sitter integration ([src/editing/text_source.c:15-19](src/editing/text_source.c#L15-L19), [src/language/queries.c:199-214](src/language/queries.c#L199-L214)) continues to consume the existing `editorTextSource.read(source, offset, &bytes_read)`. Implementation just delegates to `editorTextTreeRead`. **No tree-sitter / LSP code changes.**

## Phased plan

Each phase ends in a green test suite. Property tests in [tests/test_text_invariants.c](tests/test_text_invariants.c) are the canary; if they pass, the phase ships.

### Phase 0 — Safety net hardening (1–2 days)

Before touching storage, broaden the existing harness so each subsequent phase can be validated mechanically.

- [x] Extend `test_text_invariants.c` to exercise larger documents (≥1 MB) and longer op sequences (≥10k operations).
- [x] Add line-index invariants to the differential test: for every offset `b` returned by random ops, assert `byte→line→start_byte ≤ b < line_end_byte` and `start_byte == ref-walked-newlines(b)`.
- [x] Add a "max line width" invariant: after every op, compute max line bytes from the reference doc by scanning, and assert against `editorBufferMaxRenderCols` or a dedicated accessor.
- [x] Add a microbenchmark target (Makefile) measuring: open 1 MB file, do 10k random single-char inserts, time it. Capture baseline numbers; we'll watch them shrink.
- [x] Add `editor*StatsRecord*` counters mirroring those in [src/editing/document_bridge.c](src/editing/document_bridge.c) for the new layer so tests can assert "no full rebuilds."

**Intermediate state**: zero production code changes. Tests are stricter.

**Exit criterion**: tests pass; baseline benchmark numbers recorded.

### Phase 1 — Introduce `editorTextSummary` and merge (~1 day)

- [x] New header `src/text/text_summary.h` with `struct editorTextSummary`, `editorTextSummaryZero`, `editorTextSummaryFromBytes(const char *, size_t)`, `editorTextSummaryMerge(left, right, out)`.
- [x] Unit tests in a new `tests/test_text_summary.c`: associativity (merge associative), identity (zero), randomized cross-checks against a naive byte-scan baseline.
- [x] Compute and cache a `struct editorTextSummary summary` on `struct editorRope` (alongside existing fields). Invalidate/recompute on every mutating op for now (cheap — only at the rope boundary).
- [x] Route `editorRopeLength` to read `rope->summary.bytes` (already redundant, harmless).
- [x] Add `editorRopeSummary(const struct editorRope *)` accessor.

**Intermediate state**: no behavior change. The summary is computed but not consumed yet. Confidence: high.

**Exit criterion**: summary-property tests pass; existing property tests pass.

### Phase 2 — Tree skeleton with a single leaf (~3–4 days)

Introduce the new tree type as a thin wrapper around the existing chunk array, so we can move all callers onto the new API surface before changing internals.

- [x] New module `src/text/text_tree.{h,c}` with `struct editorTextTree`, `struct editorTextNode`, `editorTextTreeInit/Free/Length/Summary/Read/CopyRange/DupRange/ReplaceRange/Append/ResetFromString/ResetFromTextSource`.
- [x] Implementation: the tree contains exactly one leaf, the leaf holds owned byte chunks (effectively reusing the existing chunk layout). Internally, ops still do linear walks — this phase is about the **interface**, not the asymptotics.
- [x] Replace `struct editorRope rope` in `struct editorDocument` ([src/text/document.h:11-16](src/text/document.h#L11-L16)) with `struct editorTextTree tree`.
- [x] Rewrite the [src/text/document.c](src/text/document.c) bodies to call `editorTextTree*` instead of `editorRope*`. No semantic changes.
- [x] Delete `src/text/rope.{h,c}` and the `editorRope*` symbols. (Callers outside `text/document.c` already reach storage through the document API per the audit — verify the grep is clean before deleting.)
- [x] Update [src/editing/text_source.c:18](src/editing/text_source.c#L18) read callback to call `editorTextTreeRead` via `editorDocumentRead` (already does; this is just confirming no direct rope reference leaked).
- [x] Property tests still pass unchanged.

**Intermediate state**: rope is gone, tree is in place but acts as a single-leaf container. `O(...)` complexity is unchanged. Naming is now honest.

**Exit criterion**: property tests pass; no `editorRope*` symbols remain.

**Risk**: this phase touches many files for purely mechanical reasons. Plan it as one focused change; do not mix with logic edits.

### Phase 3 — Real B-tree (multi-leaf, balanced) (~1 week)

This is the heart of the refactor. After this phase, all position queries become `O(log n)` and edits stop doing whole-array `memmove`s.

- [x] Implement leaf split: when a leaf's piece count would exceed `ROTIDE_TEXT_TREE_FANOUT`, split into two leaves under a (possibly new) parent.
- [x] Implement internal-node split with the same fanout policy.
- [x] Implement leaf/node merge when a node falls below `FANOUT/2` after a delete (B-tree underflow handling).
- [x] Implement `editorTextTreeLocateByte`: descent using `child_summary.bytes` to pick the right child at each level, accumulating offset. Returns `(leaf, piece_idx, local_offset)`.
- [x] Implement `editorTextTreeReplaceRange` via descent:
  1. Locate `(leaf_lo, piece_idx_lo, local_lo)` and `(leaf_hi, piece_idx_hi, local_hi)`.
  2. Split the boundary pieces if needed (piece-internal split, just slicing `(buf, offset, len)`).
  3. Drop fully-spanned pieces.
  4. Insert new piece(s) for `new_text` (still owning bytes for now — pieces-from-buffers comes in Phase 5).
  5. Walk back up the spine recomputing summaries and rebalancing.
- [x] Maintain the `summary` field on every node/leaf as edits propagate up; never recompute from scratch.
- [x] Switch existing callers: nothing changes externally — `editorTextTree*` keeps the same public signature. Internally `editorTextTreeRead` now uses descent instead of a linear walk.
- [x] Re-run benchmarks; expect single-keystroke inserts on a 1 MB document to drop from ~`O(n/1024)` walks to `O(log n)`.

**Intermediate state**: working B-tree, owned-byte leaves. `line_starts[]` in `editorDocument` is still there and still maintained externally — Phase 4 retires it.

**Exit criterion**: property tests pass; benchmark shows order-of-magnitude improvement on random inserts at scale.

**Risks**:
- Off-by-one errors in node split/merge are the classic B-tree bug. Mitigation: keep splits/merges in one helper each (`editorTextTreeSplitNode`, `editorTextTreeMergeNode`); unit-test them in isolation; differential tests catch the rest.
- Summary recomputation on the way up must use `editorTextSummaryMerge`, never naive sum. Add an assertion in debug builds that root summary recomputed from scratch matches the maintained one.
- Allocation paths must keep their `editorMalloc` failure handling intact ([src/text/rope.c:159-163](src/text/rope.c#L159-L163) is the pattern: roll back partial work, return 0).

### Phase 4 — Retire `line_starts` (~3 days)

With the tree carrying `newlines` summaries, the external line index in `editorDocument` is redundant.

- [x] Reimplement `editorDocumentLineCount` ([src/text/document.c:514-516](src/text/document.c#L514-L516)) as `tree.root.summary.newlines + 1` (with empty-doc special case).
- [x] Reimplement `editorDocumentLineStartByte` ([src/text/document.c:518-526](src/text/document.c#L518-L526)) as `editorTextTreeLocateLine(tree, idx, &byte)`. Implementation: descent using `child_summary.newlines` to pick the right child, then scan within the target leaf piece.
- [x] Reimplement `editorDocumentLineEndByte`, `editorDocumentLineIndexForByteOffset`, `editorDocumentPositionToByteOffset`, `editorDocumentByteOffsetToPosition` on top of the tree-descent primitives.
- [x] **Delete** the entire incremental line-index machinery in [src/text/document.c](src/text/document.c):
  - `editorDocumentEnsureLineCapacity` (~30 lines)
  - `editorDocumentRebuildLineIndex` (~40 lines)
  - `editorDocumentFindContainingLineFromIndex` (~25 lines)
  - `editorDocumentCollectLineStartsInRange` (~75 lines)
  - `editorDocumentPrepareReplaceLineRegion` (~40 lines)
  - `editorDocumentApplyReplaceLineRegion` (~110 lines)
  - `editorDocumentApplySignedDelta` (~15 lines)
  - The `line_starts`, `line_count`, `line_capacity` fields on `struct editorDocument`.
- [x] Update `editorDocumentReplaceRange` ([src/text/document.c:490-512](src/text/document.c#L490-L512)) to only call `editorTextTreeReplaceRange`.
- [x] Keep `editorDocumentStatsFullRebuildCount` / `editorDocumentStatsRecordFullRebuild` ([src/editing/document_bridge.c:98-112](src/editing/document_bridge.c#L98-L112)) as the "document state reset" and edit-pipeline counters that existing tests rely on; their original line-index meaning is gone but the bookkeeping points are still meaningful.

**Intermediate state**: ~250 lines deleted, `editorDocument` is now `{ tree }` plus nothing else. Storage and line metadata are a single structure.

**Exit criterion**: property tests pass — including all line-index invariants added in Phase 0.

**Risk**: `editorBuildSyntaxEditForDocumentEdit` ([src/editing/edit_pipeline.c:72-111](src/editing/edit_pipeline.c#L72-L111)) calls `editorDocumentByteOffsetToPosition` to build tree-sitter `TSInputEdit` points. The new tree-backed implementation must return identical points. Run the existing `test_syntax_incremental_equiv` ([tests/test_syntax_incremental_equiv.c](tests/test_syntax_incremental_equiv.c)) to confirm incremental parses match full parses.

### Phase 5 — Pieces from shared immutable buffers (~1 week)

Currently every leaf owns its bytes. Move to a piece-table model so file loads are zero-copy and inserts append to a per-document "add" buffer.

- [ ] Introduce `struct editorTextBuffer` with `editorTextBufferAlloc(capacity)`, `editorTextBufferRetain`, `editorTextBufferRelease`, `editorTextBufferAppend(buf, bytes, len) -> offset`.
- [ ] `struct editorTextPiece` becomes `{ buf*, offset, len, summary }`. Leaves no longer own bytes directly.
- [ ] `editorTextTreeResetFromString` and `editorTextTreeResetFromTextSource` build one "original" buffer holding all bytes, then pieces slice it. Single allocation for the file's contents.
- [ ] Each tree gets an "add buffer" (`tree.add_buf`) that starts empty and grows on inserts; inserts write the new text at the end of `add_buf` and create one piece pointing into it.
- [ ] When an edit deletes a piece (or part of one), the piece's slice is no longer referenced; the underlying buffer keeps living until its refcount hits zero. Buffers are never compacted (Zed accepts this; for an IDE the add buffer is bounded by edit activity within a session).
- [ ] `editorTextTreeRead(t, b, &avail)` returns a direct pointer into `piece->buf->bytes + piece->offset + (b - piece_start)`. True zero-copy. The `avail` cap is now the remainder of the piece, not the chunk.
- [ ] Update [src/editing/text_source.c](src/editing/text_source.c) — same interface, but reads will now be larger contiguous spans, reducing tree-sitter's `read` callback invocations.
- [ ] **Open question**: when to coalesce (Phase 7) vs. when to let pieces accumulate. Pure piece-table = never coalesce; Zed does coalesce small pieces. Start with "never coalesce in Phase 5, add coalescing in Phase 7."

**Intermediate state**: pieces, shared buffers, zero-copy reads. Memory model is now: original file buffer (long-lived) + add buffer (grows during session) + tree of pieces.

**Exit criterion**: property tests pass; benchmark shows fewer tree-sitter read callbacks and lower memcpy volume on file open.

**Risks**:
- Lifetime bugs in refcounting are the new failure mode. Mitigation: every piece copy bumps the buffer's refcount; every piece drop decrements it; assert refcount == 0 in `editorTextBufferRelease`. Run under ASan for the full property-test corpus.
- Save path ([src/editing/text_source.c:36-80 `editorDupActiveTextSource`](src/editing/text_source.c#L36-L80)) currently allocates `length + 1` bytes and uses `editorTextSourceCopyRange`. That path keeps working unchanged — `CopyRange` iterates pieces and `memcpy`s each into the destination.
- LSP `didChange` payloads pass `edit->new_text` which is caller-owned; we copy it into the add buffer. No lifetime issue.

### Phase 6 — `max_line_bytes` summary live (~2 days)

Make the long-line metric a tree query instead of a per-row walk.

- [ ] Wire `first_line_bytes`, `last_line_bytes`, `max_line_bytes` through `editorTextSummaryFromBytes` for new pieces.
- [ ] Confirm `editorTextSummaryMerge` produces correct results across piece boundaries (covered by Phase 1 unit tests; add cross-boundary cases if missing).
- [ ] Replace `editorBufferMaxRenderCols` ([src/editing/buffer_core.c](src/editing/buffer_core.c)) implementation with a tree-summary read: `max(root.max_line_bytes, root.first_line_bytes, root.last_line_bytes)`. Drop `E.max_render_cols_valid` and the cache-invalidation calls in [src/editing/edit_pipeline.c:173](src/editing/edit_pipeline.c#L173).
- [ ] **Open question (call it out in code)**: this gives max line *bytes*, not display columns. Display columns depend on tab expansion, which depends on the column at which each tab sits. Options:
  - (a) Accept "max bytes" as the metric — practical for horizontal scrolling, slightly conservative for cursor clamping. The audit's recommendation lines up with this.
  - (b) Keep `erow.render_display_cols` and take `max` across rows as a fallback for the cases that genuinely need display columns.
  - Recommendation: go with (a) for the summary; let any code that truly needs display columns reduce over the row cache.

**Exit criterion**: `editorBufferMaxRenderCols` is `O(1)`; no full-row reduction left.

### Phase 7 — Coalescing and small-piece compaction (~3 days)

After heavy editing, a document accumulates many tiny pieces. Add bounded coalescing to keep traversal cheap.

- [ ] On `editorTextTreeReplaceRange`, if the newly inserted piece is small (e.g., `< 64` bytes) and the previous piece in the same leaf has spare capacity in its underlying buffer (it's the add buffer, and its end offset aligns with the new write), append into the previous piece instead of creating a new one. This is the "typing fast path."
- [ ] On any leaf operation that leaves a leaf with `count == 1` and a small piece, try to merge with a sibling.
- [ ] Add a `editorTextTreeStats` accessor exposing piece count, average piece size, max depth. Make property tests assert that after `K` random edits, piece count stays within a sensible factor of `K`.

**Exit criterion**: piece count after a 10k-keystroke run is bounded by a small constant multiple of expected insert-merge fast-path hits.

### Phase 8 (optional, separate effort) — Retire `erow.chars` (~1 week)

The audit calls out that `erow.chars` is a second copy of each line's bytes. Once the tree can answer per-line reads cheaply, this duplicate can go.

- [ ] Inventory every read of `E.rows[r].chars` (cursor math, search, render, etc.) via `grep -rn 'rows\[.*\]\.chars\|->chars'`.
- [ ] Replace direct `chars` access with `editorDocumentLineDupRange(document, line_idx)` or a new `editorDocumentLineRead(document, line_idx, &len) -> const char *` (which can return a tree piece pointer when a line lives entirely in one piece, or copy on demand otherwise).
- [ ] Keep `erow.render` (tab-expanded, display-encoded) — that's not redundant; rendering needs it.
- [ ] Delete `erow.chars`, `erow.size`, and their lifecycle in [src/editing/row_cache.c](src/editing/row_cache.c).

This phase is large and orthogonal to the storage refactor; it is listed for completeness but should be its own follow-up.

**Exit criterion**: `erow.chars` is gone; row cache is render-only metadata; memory footprint halves on text content.

## What this plan does NOT change

- Public APIs of `editorDocument`. The structure shrinks but the function signatures hold.
- `editorTextSource` interface and tree-sitter / LSP integration. Read callback semantics unchanged.
- The single canonical edit pipeline `editorApplyDocumentEdit` ([src/editing/edit_pipeline.c:113](src/editing/edit_pipeline.c#L113)). It still validates, dups removed text, calls `editorDocumentReplaceRange`, splices the row cache, syncs cursor, records history, notifies post-edit. Internals of `editorDocumentReplaceRange` change; its shape does not.
- Undo/redo ([src/editing/history.c](src/editing/history.c)). Entries store byte offsets and full `removed_text` / `inserted_text` strings; tree storage is invisible to them. The TODO entry for "diff-based Undo Graph similar to Fred" is unrelated and lands on its own track.
- Workspace persistence and recovery ([src/workspace/recovery.c](src/workspace/recovery.c)). Reads/writes via `editorTextSource`.
- Property-test infrastructure ([tests/test_text_invariants.c](tests/test_text_invariants.c)). It's the reason this refactor is feasible at all — do not modify it during a phase, only between phases.

## Risk register

| Risk | Phase | Mitigation |
|---|---|---|
| B-tree split/merge off-by-ones | 3 | Helpers in isolation + unit tests + differential property tests |
| Summary drift after edits | 3, 6 | Debug-build assertion: `recompute_summary_from_leaves(root) == root.summary` |
| Tree-sitter `TSInputEdit` points diverge | 4 | `test_syntax_incremental_equiv` already verifies incremental == full reparse |
| Piece refcount leaks / use-after-free | 5 | ASan + property tests; assert refcount == 0 on buffer release |
| Add-buffer unbounded growth | 5 | Acceptable for a session; document the property; revisit if telemetry says otherwise |
| Performance regression at small N | 3, 5 | Tree overhead vs. linear-walk crossover may be small. Benchmark; if needed, keep a small-doc fast path that skips descent for `bytes < 4K` |
| `erow.chars` consumers tangle the migration | 8 | Defer to a separate effort after Phases 1–7 land |
| Misnamed types post-refactor | 2 | Phase 2's rename leaves no `editorRope*` symbols. Audit's first recommendation is satisfied implicitly |

## Open questions

1. **Fanout**: 16 is a reasonable default. Validate empirically once Phase 3 lands.
2. **Max line metric**: bytes vs. display columns (see Phase 6 note). Recommendation: bytes in the summary, display columns from the row cache when truly needed.
3. **Snapshot/COW**: Zed's SumTree supports cheap structural sharing for collaboration features. Rotide doesn't need it now; designing for it would slow the migration. Defer; don't preclude.
4. **Persistence format**: nothing currently serializes the rope/tree directly (recovery serializes text). Stays true post-refactor.
5. **Tab-expanded width in summary**: ruled out for this refactor (position-dependent; non-associative). Revisit only if a feature demands it.

## Phase summary checklist

- [x] **Phase 0** — Harden property tests, add benchmarks, line/width invariants.
- [x] **Phase 1** — `editorTextSummary` + merge, computed but not consumed.
- [x] **Phase 2** — Single-leaf tree wrapper; delete `editorRope*` symbols.
- [x] **Phase 3** — Real B-tree with multi-leaf descent and `O(log n)` edits.
- [x] **Phase 4** — Drop `line_starts[]`; ~250 lines deleted from `document.c`.
- [ ] **Phase 5** — Pieces from shared immutable buffers; zero-copy reads.
- [ ] **Phase 6** — `max_line_bytes` summary live; `editorBufferMaxRenderCols` becomes `O(1)`.
- [ ] **Phase 7** — Coalescing of small pieces; bound piece count under heavy editing.
- [ ] **Phase 8** *(optional)* — Retire `erow.chars`; row cache becomes render-only.

Each phase ships independently. The differential property tests in [tests/test_text_invariants.c](tests/test_text_invariants.c) gate every phase boundary.
