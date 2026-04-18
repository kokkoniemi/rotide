---
name: rotide-search-maintainer
description: Maintain RotIDE search behavior (Ctrl-F prompt flow, offset-based match tracking, navigation, and rendering highlights).
---

# Rotide Search Maintainer

Use for search prompt flow, active match navigation, and search highlight behavior.

## First Inspect

1. Read `references/search-playbook.md`.
2. Inspect the search caller, the buffer search helpers, and the active highlight path.
3. Check `tests/test_input_search.c` and `tests/test_render_terminal.c` if highlight output changes.

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
