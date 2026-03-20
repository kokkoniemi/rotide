---
name: rotide-search-maintainer
description: Maintain and refine RotIDE incremental search, including Ctrl-F prompt flow, live match navigation, prompt-arrow traversal, and active-match highlighting. Use when editing or reviewing search behavior in input/render/state paths, adding tests for search interactions, or stabilizing regressions in search cursor movement and highlight rendering.
---

# Rotide Search Maintainer

## Overview

Maintain search behavior without breaking existing editor invariants.
Prefer minimal, additive changes that preserve save/prompt compatibility, keymap-driven actions, and UTF-8 cursor semantics.

## Workflow

1. Read `references/search-playbook.md` for touchpoints and acceptance behavior.
2. Inspect current search flow in `input.c` (`editorFind()`, `editorFindCallback()`, prompt callback path) before changing behavior.
3. Implement search changes in small steps:
   - state transitions
   - prompt and live navigation
   - active match highlight rendering
   - status/cancel/confirm behavior
4. Update or add tests in `tests/rotide_tests.c` with behavior-focused assertions.
5. Run `make` and `make test`.
6. If render/search behavior changes, run an interactive smoke check (`./rotide README.md`) and verify:
   - `Ctrl-F` prompt updates matches live
   - Enter confirms match and Esc cancels prompt
   - prompt arrow navigation (`Up/Down` or `Left/Right`) moves between matches with wrap-around
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

- Existing prompt strategy:
  - reuse callback-capable prompt internals (`editorPromptWithCallback(...)`)
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
  - prompt arrow traversal and wrap-around
  - highlight escape sequences in captured refresh output
- Keep existing non-search tests passing.

## References

- Load `references/search-playbook.md` before search feature/regression work.
