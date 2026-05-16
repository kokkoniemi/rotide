# Buffer Refactor Playbook

Operational handbook for executing a single phase of [BUFFER_REFACTOR_PLAN.md](../../../BUFFER_REFACTOR_PLAN.md). Read the plan first; this file tells you *how* to ship a phase, not *what* the phases are.

Follow the comment policy in project's AGENTS.md.

## How a phase ships

1. **Identify the active phase.** Find the first unchecked entry in the "Phase summary checklist" at the bottom of [BUFFER_REFACTOR_PLAN.md](../../../BUFFER_REFACTOR_PLAN.md). That's your phase. If the previous phase's box is unchecked, finish it first; don't start a new phase out of order.
2. **Read the phase's tasks** in BUFFER_REFACTOR_PLAN.md. Treat them as the scope contract for the PR.
3. **Run baseline checks** before editing code:
   ```
   make
   make test
   ASAN_OPTIONS=detect_leaks=0 make test-sanitize
   ```
   If the tree is already red, fix that first or stop.
4. **Implement only the tasks listed for that phase.** Resist:
   - "while I'm here" cleanups in unrelated files
   - pre-implementing work that's listed in a later phase
   - renaming symbols that will be renamed in a later phase anyway
5. **Run validation** (see "Validation per phase" in [SKILL.md](../SKILL.md)) and the phase-specific gate below.
6. **Tick the checkbox** in BUFFER_REFACTOR_PLAN.md's "Phase summary checklist." That checklist is the single source of truth for phase status. If a phase is partially done, leave its box unchecked; ticking it means *every task in that phase has shipped*.
7. **Commit message** names the phase: e.g. `buffer: Phase 3 — multi-leaf B-tree with O(log n) descent`.

## Phase-specific gates

### Phase 0 — Safety net hardening
- New property-test variants must run inside the existing harness and use the same `refDoc` reference doc — do not introduce a parallel reference.
- Microbenchmark target lives in the Makefile and is invoked manually; record numbers in the PR description, don't commit the baseline numbers.

### Phase 1 — `editorTextSummary` + merge
- Summary functions live in new files (`src/text/text_summary.{h,c}`); no edits to [src/text/rope.c](../../../src/text/rope.c) or [src/text/document.c](../../../src/text/document.c) that change behavior.
- Associativity test for `editorTextSummaryMerge` is mandatory: for random `a, b, c`, `merge(merge(a,b), c) == merge(a, merge(b,c))` byte-equal.
- The cached summary on `editorRope` exists but is unused by consumers — confirm with grep.

### Phase 2 — Single-leaf tree wrapper
- Pure rename. Diff should be dominated by name churn, not logic.
- After this phase, `grep -rn 'editorRope' src/ tests/` returns zero hits.
- `editorDocument`'s public header signatures are unchanged.

### Phase 3 — Real B-tree
- Split and merge in *separate* helpers (`editorTextTreeSplitNode`, `editorTextTreeMergeNode`); each gets its own unit test.
- Debug build: enable an assertion that recomputes the root summary from scratch after each edit and compares to the maintained value. Disable in release.
- Benchmark gate: random single-char inserts on a ≥1 MB document must improve by at least an order of magnitude vs. the Phase 0 baseline. If they don't, the descent path is wrong.

### Phase 4 — Retire `line_starts`
- Delete in one PR. Do not leave the old `line_starts` field as a fallback or "for compatibility" — that creates dual-source-of-truth.
- Run `test_syntax_incremental_equiv` explicitly; record the result in the PR.
- The ~250 lines removed from [src/text/document.c](../../../src/text/document.c) should appear in the diff as straight deletions.

### Phase 5 — Pieces from shared buffers
- Refcount discipline: every retain/release pair has a matching test.
- Run the full property-test corpus under ASan. Refcount-on-free is a release-time abort, not a silent corruption.
- The save path (`editorDupActiveTextSource`) is the easiest cross-check: open a file, edit it, save, diff. If the saved bytes don't match the in-memory document, piece slicing is wrong.

### Phase 6 — `max_line_bytes` summary
- Confirm `editorBufferMaxRenderCols` no longer reduces over `E.rows`. Grep for the change.
- Drop `E.max_render_cols_valid` and every line that sets it to 0. The flag becomes dead code.

### Phase 7 — Coalescing
- Add a piece-count invariant to the property tests: after `K` random ops, `tree.stats.piece_count < f(K, K_inserts_at_same_offset)`.
- Don't over-coalesce. A 64-byte threshold for the typing fast path is a reasonable start; tune via benchmark.

### Phase 8 — Retire `erow.chars` (optional, separate effort)
- This is a render-path refactor more than a storage refactor. If you find yourself doing it during the storage phases, stop and split the PR.

## Things to NOT do

- Do not modify [tests/test_text_invariants.c](../../../tests/test_text_invariants.c) between phases. That harness is fixed after Phase 0. If you find you "need" to change it to make a phase pass, the phase is wrong.
- Do not introduce snapshot/CoW now. Zed has it; Rotide doesn't need it; designing for it costs time and locks decisions. Phase 5's piece-from-buffer model leaves the door open — that's enough.
- Do not put tab-expanded display columns into the tree summary. They are position-dependent; the summary must be associative.
- Do not change `editorApplyDocumentEdit`'s control flow. Internals of `editorDocumentReplaceRange` change; the pipeline shape doesn't.
- Do not bundle the diff-based Undo Graph (separate TODO entry) into this refactor. They are orthogonal.

## When something goes wrong mid-phase

- Property tests start failing: revert to last green commit; bisect the failing op sequence; the test prints `op_idx` for that reason.
- Summary drift assertion fires: the merge path missed a code path. Don't disable the assertion; find the missing path.
- Benchmark shows a regression: check that descent is actually `O(log n)` — if a single op walks the whole leaf-piece array, the tree shape is right but the descent function isn't being called. Look for a leftover linear walk.
- Tree-sitter starts producing wrong highlights: byte→point computation in `editorBuildSyntaxEditForDocumentEdit` is off. Diff against the previous phase's implementation.

## Cross-references in the codebase

Files most likely to be touched in this refactor, by phase:

| Phase | Files |
|---|---|
| 0 | [tests/test_text_invariants.c](../../../tests/test_text_invariants.c), [Makefile](../../../Makefile) |
| 1 | new `src/text/text_summary.{h,c}`, new `tests/test_text_summary.c`, [src/text/rope.c](../../../src/text/rope.c) (cached summary) |
| 2 | new `src/text/text_tree.{h,c}`, [src/text/document.{h,c}](../../../src/text/document.c), delete `src/text/rope.{h,c}` |
| 3 | `src/text/text_tree.c` (B-tree internals) |
| 4 | [src/text/document.{h,c}](../../../src/text/document.c) (large deletions), [src/editing/document_bridge.c](../../../src/editing/document_bridge.c) (stat semantics) |
| 5 | new `src/text/text_buffer.{h,c}`, `src/text/text_tree.c` (pieces) |
| 6 | `src/text/text_tree.c` (summary fields), [src/editing/buffer_core.c](../../../src/editing/buffer_core.c) (`editorBufferMaxRenderCols`), [src/editing/edit_pipeline.c](../../../src/editing/edit_pipeline.c) (drop invalidation) |
| 7 | `src/text/text_tree.c` (coalescing) |
| 8 | [src/editing/row_cache.c](../../../src/editing/row_cache.c), [src/rotide.h](../../../src/rotide.h) (`struct erow`), and every reader of `erow.chars` |

Always run `make test-sanitize` before declaring a phase done.
