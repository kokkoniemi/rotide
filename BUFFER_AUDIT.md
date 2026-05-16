# Buffer Audit

Scope: the text-storage stack used by editor tabs — [src/text/rope.c](src/text/rope.c), [src/text/document.c](src/text/document.c), [src/editing/edit_pipeline.c](src/editing/edit_pipeline.c), [src/editing/row_cache.c](src/editing/row_cache.c), [src/editing/history.c](src/editing/history.c), and the `editorTextSource` callbacks consumed by tree-sitter/LSP.

References for comparison: the Wikipedia rope article and Zed's SumTree write-up.

## TL;DR

**No, we do not use a rope.** What is called `editorRope` is a flat dynamic array of fixed-size byte chunks (1024 B each). There is no tree, no balancing, no per-node weight, no `O(log n)` anything. All point queries (`Read`, `CopyRange`, `LocateBoundary`) are `O(chunks) = O(N / 1024)` linear scans; every replace also does a chunk-array `memmove`. The implementation is closer to a "block list" than to either a rope or a gap buffer, and a single contiguous `char *` with `memmove` would have similar asymptotic behavior for most operations (and is in fact what the property tests use as the reference — see [tests/test_text_invariants.c:23-78](tests/test_text_invariants.c#L23-L78)).

The system *around* this storage — single canonical edit pipeline, incremental line index, incremental row-cache splicing, zero-copy `editorTextSource` for tree-sitter, undo/redo with insert-coalescing, property tests with diff vs. reference — is well-built. The data structure itself is the weakest link.

## What is actually there

### The "rope"

[src/text/rope.h:11-21](src/text/rope.h#L11-L21):

```c
struct editorRopeChunk {
    char *bytes;
    size_t len;
};

struct editorRope {
    struct editorRopeChunk *chunks;
    int chunk_count;
    int chunk_capacity;
    size_t length;
};
```

`EDITOR_ROPE_CHUNK_BYTES = 1024` ([rope.c:8](src/text/rope.c#L8)). Every operation visits chunks via a linear walk: `editorRopeRead` ([rope.c:294-319](src/text/rope.c#L294-L319)), `editorRopeCopyRange` ([rope.c:321-341](src/text/rope.c#L321-L341)), `editorRopeLocateBoundary` ([rope.c:99-127](src/text/rope.c#L99-L127)). There is no cumulative-byte index, no tree, no skip list — just a `for (i = 0; i < chunk_count; i++)` accumulator.

A real rope (or Zed-style SumTree) keeps a byte-count (and typically a line-count, UTF-16 length, etc.) on every internal node, so all locate/read operations are `O(log n)`. We keep nothing per chunk except `len`, and no aggregate index.

### The document layer

[src/text/document.h:11-16](src/text/document.h#L11-L16):

```c
struct editorDocument {
    struct editorRope rope;
    size_t *line_starts;
    int line_count;
    int line_capacity;
};
```

`line_starts` is a parallel index updated alongside every replace. The update logic ([document.c:212-366](src/text/document.c#L212-L366)) splits the old line array into prefix / middle (rescanned via `editorDocumentCollectLineStartsInRange`) / shifted-suffix, with a full-rebuild fallback in `editorDocumentReplaceRange` ([document.c:490-512](src/text/document.c#L490-L512)) if the incremental path returns false. Byte→line lookups use binary search on this array ([document.c:553-587](src/text/document.c#L553-L587)) — that *is* `O(log lines)`, but only because the index is external; the rope itself can't answer line questions.

### The row cache

A *second* copy of every line lives in `E.rows: struct erow[]` ([rotide.h:90-101](src/rotide.h#L90-L101)): `chars` (raw bytes), `render` (tab-expanded display string), and a wrap-segment cache. The row array is spliced incrementally by `editorSpliceRowCache` ([row_cache.c:263-348](src/editing/row_cache.c#L263-L348)) so only affected rows are rebuilt — but the bytes are still duplicated in full. Effective memory cost ≈ 2× file size plus render expansion.

### The edit pipeline

All mutations route through one function: `editorApplyDocumentEdit` ([edit_pipeline.c:113-199](src/editing/edit_pipeline.c#L113-L199)). One call:

1. Validates and computes `old_end_offset`.
2. Dups the removed bytes for undo ([edit_pipeline.c:138-144](src/editing/edit_pipeline.c#L138-L144)).
3. Pre-computes the row-cache splice region.
4. Builds the tree-sitter `TSInputEdit` (byte + point triplets).
5. Calls `editorDocumentReplaceRange` (rope + line index).
6. Splices the row cache.
7. Re-syncs cursor.
8. Records the entry in history (with INSERT-merge attempt).
9. Notifies post-edit (tree-sitter incremental parse, LSP `didChange`).

Routing every change through a single function is the strongest part of the design: undo, redo, save, paste, indent, LSP-applied edits all share this path.

### Tree-sitter / LSP read fan-out

Tree-sitter's `TSInput.read` is wired directly to the rope via `editorTextSource` ([editing/text_source.c:15-34](src/editing/text_source.c#L15-L34)) and `editorSyntaxSourceRead` ([language/queries.c:199-214](src/language/queries.c#L199-L214)). Each rope chunk's pointer is handed back without copying — tree-sitter just gets pointer + `bytes_read` and re-asks at the next offset. That part is the one place where the chunked layout pays off vs. a flat buffer: zero-copy streaming reads for the parser.

### Undo/redo

Ring buffer of `ROTIDE_UNDO_HISTORY_LIMIT` entries ([history.c:17-56](src/editing/history.c#L17-L56)). Each entry stores `removed_text` + `inserted_text` as fully materialized `malloc`'d byte strings. Consecutive INSERT entries get coalesced into one history record via `editorHistoryTryMergeInsert` ([history.c:119-174](src/editing/history.c#L119-L174)). Undo/redo replays the saved edit through the same `editorApplyDocumentEdit` (inverse) — so the structural invariants are the same as for normal editing.

## What's good

- **One canonical mutation path** ([edit_pipeline.c:113](src/editing/edit_pipeline.c#L113)). Everything — typing, paste, indent, undo, LSP `didChange` application — funnels through `editorApplyDocumentEdit`. This is the single biggest reason syntax/row-cache/history stay coherent.
- **Incremental updates everywhere they matter**: line index ([document.c:212-366](src/text/document.c#L212-L366)), row cache ([row_cache.c:158-261](src/editing/row_cache.c#L158-L261)), tree-sitter `TSInputEdit` ([edit_pipeline.c:72-111](src/editing/edit_pipeline.c#L72-L111)). Counters (`editorRowCacheTestFullRebuildCount`, `editorDocumentStatsFullRebuildCount`) make it trivial to assert in tests that incremental paths haven't regressed to full rebuilds.
- **Defensive full-rebuild fallback** when the incremental line-index path returns false ([document.c:507-511](src/text/document.c#L507-L511)).
- **Zero-copy reader interface** (`editorTextSource`) consumed uniformly by tree-sitter, LSP serialization, search, save. The same interface lets `editorDocumentResetFromTextSource` ([document.c:419-454](src/text/document.c#L419-L454)) populate a document from another document or from a flat buffer.
- **Property tests with a reference implementation** ([tests/test_text_invariants.c](tests/test_text_invariants.c)). A differential fuzzer compares `editorDocument` against a plain `char*`-and-`memmove` `refDoc` ([tests/test_text_invariants.c:23-78](tests/test_text_invariants.c#L23-L78)). This is the right way to test a storage layer.
- **Integer-overflow hygiene** throughout: `editorSizeAdd` / `editorSizeMul` / `editorIntToSize` are used before every allocation size computation (e.g. [rope.c:42-47](src/text/rope.c#L42-L47), [row_cache.c:31-37](src/editing/row_cache.c#L31-L37)).
- **Allocation-failure paths return cleanly** — every malloc is checked and the in-progress structure is `Free`d on failure ([rope.c:159-163](src/text/rope.c#L159-L163), [rope.c:401-405](src/text/rope.c#L401-L405)).
- **Hard cap at `INT_MAX` bytes** ([rotide.h:26](src/rotide.h#L26)) keeps signed-int arithmetic in line-count code safe.
- **Undo coalescing for typing** ([history.c:119-174](src/editing/history.c#L119-L174)) avoids one-character-per-undo-entry, including the trailing-newline auto-close case.

## What's bad

### 1. It is not a rope. The name is actively misleading.

[src/text/rope.h:7-10](src/text/rope.h#L7-L10) calls it "chunked byte storage," which is honest — but the type, the function prefix, and the file name all say "rope," which sets an expectation of `O(log n)` operations and structural sharing. We have neither. A future contributor reading "we have a rope" will assume edits, point reads, and substring queries are cheap; they are not.

Concretely:

| Operation | Real rope | This implementation |
|---|---|---|
| Locate byte offset | `O(log n)` | `O(n / 1024)` linear |
| Read at offset | `O(log n)` chunk | `O(n / 1024)` |
| Replace range | `O(log n + new_len)` | `O(n / 1024 + new_len)` plus a chunk-array `memmove` |
| Split | `O(log n)` | `O(n / 1024)` with `memmove` of chunk pointers |
| Line at offset | `O(log lines)` (built into nodes) | `O(log lines)` (external `line_starts` array) |
| Concat | `O(log n)` shared | `O(n)` byte copy |

For a 10 MB buffer there are ~10,000 chunks; every random point read walks ~5,000 of them on average. We get away with this in practice because real editing is local and the chunks are small enough that the walks vectorize well, but the structure itself does nothing the name implies.

### 2. No cumulative-byte index across chunks.

The cheapest single fix: keep a parallel `size_t *cum_bytes` (prefix sums) and binary-search it, or attach `cum_bytes` to each chunk and rebuild it on edits. Even a `cum_bytes` array refreshed on every edit would give `O(log chunks)` reads at the cost of `O(chunks)` per edit — which is what we already pay anyway. It would, however, only get us partway to a real rope. The full step is the next item.

### 3. The "rope" should be a tree (SumTree-of-pieces).

The Zed write-up describes the design we should aim for: a B-tree of immutable text pieces where each internal node carries summary statistics (byte count, line count, UTF-16 length, max line width). With that, the separate `line_starts: size_t[]` array in `editorDocument` and the binary-search-on-edit logic in [document.c:212-366](src/text/document.c#L212-L366) (which is ~150 lines of off-by-one-prone code) disappear: `Line@n` and `Offset→Line` become tree descents on the summary. Today we maintain two data structures (rope + `line_starts`) that have to be kept consistent across every edit — and the incremental path is complex enough that there is a full-rebuild fallback. A SumTree collapses both into one structure with one update path.

### 4. Chunks fragment over time; nothing coalesces them.

`editorRopeReplaceRange` always builds a fresh chunk array for the inserted text and splices it in ([rope.c:373-408](src/text/rope.c#L373-L408)). A one-character insert in the middle of a buffer produces:

- a split of the host chunk into two new chunks (2 mallocs + 2 memcpys of up to 1024 bytes total),
- one new 1-byte chunk for the inserted character,
- two `memmove`s of the chunk-pointer array.

After thousands of mid-buffer keystrokes the chunk array fills with 1-byte and few-byte chunks. There is no compaction pass and no "if the new chunk is small and adjacent, append into the neighbor" fast path. Every chunk is also a separate `malloc`, so this fragmentation is real heap pressure — 10k chunks ≈ 10k allocator headers (~160-320 KB overhead on glibc) on top of the data.

A `memmove`-on-a-flat-buffer implementation would not pay this overhead at small sizes; the only reason to live with it is to enable the `O(log n)` operations we don't actually have.

### 5. The row cache double-stores the text.

[src/rotide.h:90-101](src/rotide.h#L90-L101): every line lives in `erow.chars` (raw) and `erow.render` (tab-expanded), in addition to the rope. The row cache exists because rendering wants random per-line access to the *rendered* bytes (column widths, wrap segments, syntax highlights), and re-deriving render strings on every frame would be expensive. Fair. But the *raw* `chars` field is a pure duplicate of the rope bytes. With a SumTree carrying line summaries and chunk-local rendering, the raw copy could be retired. Today we pay ~2× memory.

### 6. `editorRopeBuildChunkArrayFromText` does N small mallocs.

[rope.c:129-174](src/text/rope.c#L129-L174). Inserting a 1 MB blob (paste) allocates ~1,024 chunks individually. A single bulk allocation of `total + N * sizeof(chunk_header)` would be much cheaper. Same complaint applies to `editorRopeAppend` — each 1024-byte chunk is its own `editorMalloc` ([rope.c:218](src/text/rope.c#L218)).

### 7. `editorRopeReplaceRange` doesn't try to merge the inserted text into the boundary chunks.

If the insert is small and the chunks on either side have room, we should append/prepend into them instead of producing standalone tiny chunks. Today every insert produces its own chunk array, period.

### 8. `editorDocumentRebuildLineIndex` walks the whole text on resets.

[document.c:38-78](src/text/document.c#L38-L78). Called from `editorDocumentResetFromString` / `…FromTextSource` (open-file path). Unavoidable at open time, but it'd be free in a SumTree because the load already builds the summaries.

### 9. The incremental line-index update is complex.

`editorDocumentApplyReplaceLineRegion` ([document.c:255-366](src/text/document.c#L255-L366)) is ~110 lines manipulating prefix / middle / shifted suffix offsets. It's correct enough to pass the property tests, but it is the single hairiest piece of bookkeeping in the buffer layer, and it exists only because the line index is external to the rope. Folding line summaries into the storage tree (Zed's approach) removes this code entirely.

### 10. `INT_MAX`-byte cap.

`ROTIDE_MAX_TEXT_BYTES = INT_MAX` ([rotide.h:26](src/rotide.h#L26)) means ~2 GiB max file. Fine for IDE workloads, but worth knowing it's enforced (and that the code relies on `int` arithmetic for line indices, chunk counts, and row counts).

## Verdict

The data structure called `editorRope` is doing the job of a buffer for this editor, but it is not the structure its name claims, and it is not the structure a senior contributor reading the code would expect. Performance is dominated by linear scans over the chunk array; the only operations that are not `O(N/1024)` are the ones that go through the external `line_starts` index.

The layers built **on top** of it — the canonical edit pipeline, the incremental line/row updates, the zero-copy text-source interface, the differential property tests — are good and would carry over to any replacement.

### Recommended direction (not commitments)

1. **Rename for honesty.** `editorBlockBuffer` / `editorChunkBuffer`. The current name promises a contract we don't fulfill. If the eventual plan is to replace this with an actual rope, leave a comment in the header pointing at the planned target.
2. **Cheap quick win:** add a prefix-sum / `cum_bytes` array refreshed on each edit. Turns `editorRopeRead` and `editorRopeLocateBoundary` from `O(chunks)` into `O(log chunks)` without changing the storage model.
3. **Coalesce adjacent small chunks** in `editorRopeReplaceRange` to bound the worst-case chunk count.
4. **Real fix:** replace the flat chunk array with a SumTree-of-pieces (B-tree, summaries = byte count + line count + max line width). Retire the separate `line_starts` array. Retire `erow.chars` once the tree can answer per-line raw-byte queries efficiently. This is a multi-week project and orthogonal to the diff-based Undo Graph item in the TODO; both can land independently.

The property-test harness in [tests/test_text_invariants.c](tests/test_text_invariants.c) is the right scaffolding for that replacement: keep the test, swap the implementation, watch it fuzz.
