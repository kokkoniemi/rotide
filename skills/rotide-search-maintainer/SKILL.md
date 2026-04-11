---
name: rotide-search-maintainer
description: Maintain RotIDE search behavior (Ctrl-F prompt flow, offset-based match tracking, navigation, and rendering highlights).
---

# Rotide Search Maintainer

## Scope

Use for search regressions/improvements in:
- prompt lifecycle and callbacks
- next/previous match traversal
- cancel/confirm behavior
- active match highlight rendering
- search state correctness across tabs/undo/edits

## Workflow

1. Read `references/search-playbook.md`.
2. Inspect `input.c` search path:
   - `editorFind()`
   - `editorFindCallback()`
   - cursor restore helpers
3. Inspect `buffer.c` search functions:
   - `editorBufferFindForward()`
   - `editorBufferFindBackward()`
4. Inspect highlight rendering in `output.c`.
5. Update/add tests in `tests/rotide_tests.c`.
6. Run `make` and `make test`.

## Guardrails

- Search state is offset-based (`search_match_offset`, `search_saved_offset`).
- Keep cursor placement UTF-8/grapheme-safe after match jumps.
- Search actions must not mark buffer dirty.
- Preserve prompt semantics:
  - Enter confirms current state
  - Esc restores saved cursor
  - empty query clears active match and restores cursor
- Keep highlight attribute resets tight to avoid style leakage.

## References

- `references/search-playbook.md`
