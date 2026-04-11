---
name: rotide-document-maintainer
description: Maintain RotIDE canonical document/rope/edit-history/recovery pipelines and offset mapping invariants.
---

# Rotide Document Maintainer

## Scope

Use for changes in:
- `document.c` / `document.h`
- `rope.c` / `rope.h`
- document-backed edit application in `buffer.c`
- offset mapping and cursor offset invariants
- operation-based history entries
- recovery normalization and document restore paths

## Workflow

1. Read `references/document-playbook.md`.
2. Identify whether the change affects:
   - storage semantics
   - offset/line mapping
   - edit application and row-cache rebuild
   - history behavior
   - recovery serialization/restore
3. Apply minimal changes, preserving canonical document-first ownership.
4. Update tests in `tests/rotide_tests.c` (or helper tests) for changed behaviors.
5. Run `make` and `make test`.
6. Run sanitizer suite when touching memory/replace-range/index logic.

## Guardrails

- `editorDocument` remains canonical text source.
- Row arrays are derived; do not introduce row-authoritative write paths.
- Keep `cursor_offset` and offset mapping helpers consistent.
- Keep edit descriptors/history entries internally coherent:
  - start offset, removed/inserted slices, before/after cursor offsets, dirty before/after.
- Preserve recovery compatibility behavior for legacy row snapshots.

## References

- `references/document-playbook.md`
