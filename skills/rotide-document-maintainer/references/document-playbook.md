# Document Playbook

## Primary touchpoints

- `document.c` / `document.h`
  - line index rebuild
  - offset <-> line/column conversions
  - range read/copy/dup/replace
- `rope.c` / `rope.h`
  - chunk storage, append/read/copy/replace mechanics
- `buffer.c`
  - `editorApplyDocumentEdit(...)`
  - row-cache rebuild from document
  - cursor/selection/search offset updates
  - undo/redo operation replay
  - recovery restore normalization
- `rotide.h`
  - state fields (`cursor_offset`, `document`, history entries)

## Core invariants

- Canonical text is document-backed.
- Rows are derived cache only.
- Offset mapping helpers must be consistent in both directions.
- Cursor updates should preserve valid UTF-8/grapheme boundaries after deriving `(cy,cx)`.
- History operations should carry enough inverse data to undo/redo without full snapshots.

## Change checklist

1. Validate bounds and overflow on all byte-range operations.
2. Ensure replace path updates line index correctly for:
   - insert
   - delete
   - replace
   - multiline edits
3. Keep active text-source consumers working (`editorBuildActiveTextSource`).
4. Ensure row cache rebuild is deterministic after each document change.
5. Confirm history before/after cursor offsets remain coherent.
6. Confirm recovery open/restore still hydrates from document path.

## Tests to touch

- document/rope unit tests
- offset roundtrip tests
- edit behavior tests (insert/newline/delete/range delete)
- undo/redo behavior tests
- recovery roundtrip + legacy compatibility tests

Validation:
- `make`
- `make test`
- `make test-sanitize` for storage/index-sensitive changes
