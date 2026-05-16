---
name: rotide-buffer-refactor
description: Drive the multi-phase migration from the flat editorRope chunk array to a Zed-style SumTree-of-pieces text storage, following BUFFER_REFACTOR_PLAN.md.
---

# Rotide Buffer Refactor

Use this skill for any work that implements a phase of [BUFFER_REFACTOR_PLAN.md](../../BUFFER_REFACTOR_PLAN.md) — i.e. work that touches text storage internals along the path from `editorRope` to `editorTextTree` / `editorTextSummary` / `editorTextPiece` / `editorTextBuffer`, retires `line_starts[]` in `editorDocument`, or moves `max_line_bytes` into the tree summary.

For day-to-day work on the document layer that is *not* part of this migration, use `rotide-document-maintainer` instead.

## First Inspect

1. Read [BUFFER_REFACTOR_PLAN.md](../../BUFFER_REFACTOR_PLAN.md). The "Phase summary checklist" at the bottom is the canonical status; the first unchecked box is the active phase.
2. Read [`references/buffer-refactor-playbook.md`](references/buffer-refactor-playbook.md) for the per-phase operational handbook.
3. Read [BUFFER_AUDIT.md](../../BUFFER_AUDIT.md) only if you need the problem statement; it is background, not the plan.
4. Inspect [tests/test_text_invariants.c](../../tests/test_text_invariants.c) — that harness is the gate at every phase boundary.

## Guardrails

- **Stay inside the active phase.** Don't pre-implement work from later phases. One phase per PR. Phases land independently.
- **Don't skip phases.** The order in BUFFER_REFACTOR_PLAN.md is load-bearing: each phase keeps the editor working and the property tests green so the next phase has a safety net.
- **Property tests are the gate.** [tests/test_text_invariants.c](../../tests/test_text_invariants.c) must remain green at every phase boundary. Only modify the test harness during Phase 0. After Phase 0, treat it as fixed.
- **Public API stability.** Across all phases, these contracts do not change:
  - `editorDocument*` function signatures in [src/text/document.h](../../src/text/document.h).
  - `editorTextSource.read(source, byte_index, &bytes_read)` semantics (the tree-sitter contract).
  - The shape of `editorApplyDocumentEdit` in [src/editing/edit_pipeline.c](../../src/editing/edit_pipeline.c).
- **Tree-sitter byte/point identity.** After any phase that changes how byte→position is computed (Phase 4 especially), `test_syntax_incremental_equiv` must still pass — incremental parses must remain byte-identical to full reparses.
- **Summary is bytes, not display columns.** Max-line metric in `editorTextSummary` is byte count. Display-column width is position-dependent (tab expansion) and stays out of the tree summary. Cases that truly need display columns reduce over the row cache.
- **Debug-build summary assertion.** In Phases 3+, builds with assertions enabled must verify `recompute_summary_from_leaves(root) == root.summary` after edits. If you skip this check, summary drift will silently corrupt position queries.
- **No `editorRope*` symbols after Phase 2.** Phase 2's rename leaves the codebase honest. If a phase reintroduces the old name, it's wrong.
- **Tick the BUFFER_REFACTOR_PLAN.md checklist when a phase ships.** That checklist is the single source of truth for phase status; the next contributor will read it to find the active phase.
- **Don't bundle the refactor with unrelated work.** No drive-by cleanups, no opportunistic renames in adjacent code.

## Validation per phase

- `make` and `make test` always.
- `ASAN_OPTIONS=detect_leaks=0 make test-sanitize` always — this is storage code.
- After Phase 3+: `make test` with the differential property tests run at the larger sizes added in Phase 0.
- After Phase 4: `test_syntax_incremental_equiv` explicitly.
- After Phase 5: run under ASan with the full property-test corpus to catch refcount errors on shared text buffers.
- Capture and compare benchmark numbers from Phase 0's microbenchmark target before merging Phase 3, Phase 5, Phase 6, Phase 7.

## References

- [BUFFER_REFACTOR_PLAN.md](../../BUFFER_REFACTOR_PLAN.md) — the authoritative plan
- [BUFFER_AUDIT.md](../../BUFFER_AUDIT.md) — the problem statement
- [`references/buffer-refactor-playbook.md`](references/buffer-refactor-playbook.md) — per-phase operational guidance
