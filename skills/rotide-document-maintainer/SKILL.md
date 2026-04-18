---
name: rotide-document-maintainer
description: Maintain RotIDE canonical document/rope/edit-history/recovery pipelines and offset mapping invariants.
---

# Rotide Document Maintainer

Use for canonical document, rope, edit-history, and recovery-path changes.

## First Inspect

1. Read `references/document-playbook.md`.
2. Inspect touched document/storage modules.
3. Check `tests/test_document_text_editing.c` and `tests/test_save_recovery.c` if behavior changes.

## Guardrails

- `editorDocument` remains canonical text source.
- Row arrays are derived; do not introduce row-authoritative write paths.
- Keep `cursor_offset` and offset mapping helpers consistent.
- Keep edit descriptors/history entries internally coherent:
  - start offset, removed/inserted slices, before/after cursor offsets, dirty before/after.
- Preserve recovery compatibility behavior for legacy row snapshots.

## References

- `references/document-playbook.md`
