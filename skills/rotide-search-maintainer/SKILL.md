---
name: rotide-search-maintainer
description: Implement and refine incremental search in rotide, including Ctrl-F prompt flow, live match navigation, match highlighting, and follow-up next/previous navigation (Ctrl-N/Ctrl-P). Use when editing or reviewing search behavior in input/render/state paths, adding tests for search interactions, or stabilizing regressions in search cursor movement and highlight rendering.
---

# Rotide Search Maintainer

## Overview

Implement search features without breaking existing editor invariants.
Prefer minimal, additive changes that preserve save/prompt behavior and existing UTF-8 cursor semantics.

## Workflow

1. Read `references/search-playbook.md` for touchpoints and acceptance behavior.
2. Inspect current key and prompt flow in `input.c` before changing search behavior.
3. Add search state and behavior in small steps:
   - state shape
   - prompt and live navigation
   - render highlight
   - next/previous shortcuts
4. Update or add tests in `tests/rotide_tests.c` with behavior-focused assertions.
5. Run `make` and `make test`.
6. If render/search behavior changes, run an interactive smoke check (`./rotide README.md`) and verify:
   - `Ctrl-F` prompt updates matches live
   - Enter confirms match and Esc cancels prompt
   - `Ctrl-N` / `Ctrl-P` move between matches
   - highlight appears only for active match context

## Guardrails

- Preserve row and buffer invariants:
  - keep `row->chars` NUL-terminated
  - preserve cursor boundary clamping by grapheme cluster helpers
  - avoid mutating `E.dirty` for search-only actions
- Keep prompt compatibility:
  - preserve current save prompt behavior (`editorPrompt("Save as: %s")`)
  - avoid API breakage unless all callers and tests are updated
- Keep rendering changes localized:
  - avoid global style state leaking across rows
  - reset VT100 attributes after highlight spans
- Prefer predictable navigation order:
  - forward for next, backward for previous
  - wrap around file boundaries consistently

## Implementation Defaults

- Additive prompt strategy:
  - introduce callback-capable prompt handling internally
  - keep a simple `editorPrompt(...)` wrapper for existing call sites
- Search state location:
  - store active query/match metadata in `editorConfig` (or an equivalent owned state object reachable from input/output code)
- Match algorithm:
  - perform byte-substring search against `row->chars`
  - convert match byte index to cursor/display coordinates via existing row conversion helpers
- Highlight strategy:
  - render only active match highlight in `output.c`
  - insert highlight VT100 sequences around visible match slice and return to normal colors immediately after span

## Test Expectations

- Add or update tests for:
  - opening search prompt and finding first match
  - incremental movement while typing query
  - cancel/confirm behavior restoring or preserving cursor as intended
  - Ctrl-N / Ctrl-P traversal and wrap-around
  - highlight escape sequences in captured refresh output
- Keep existing non-search tests passing.

## References

- Load `references/search-playbook.md` before implementing feature work with this skill.
