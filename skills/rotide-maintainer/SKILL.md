---
name: rotide-maintainer
description: Maintain and evolve the rotide terminal editor project. Use when working in this repository on rotide.c, Makefile, or README tasks such as bug fixes, feature changes, refactors, code review, and validation with the project's strict compiler flags.
---

# Rotide Maintainer

## Quick Start Workflow

1. Read `README.md`, `Makefile`, and the touched area in `rotide.c`.
2. Check workspace state with `git status --short`; keep unrelated user changes intact.
3. Implement the smallest viable change while matching current code style (tabs, C2x, sectioned layout).
4. Run `make`; treat all warnings as blockers because `-Werror` is enabled.
5. Summarize behavior impact, residual risks, and validation performed.

## Project-Specific Guardrails

- Keep the lightweight single-source architecture unless the user asks for structural splitting.
- Preserve editor row invariants:
  - Keep `row->chars` NUL-terminated with `row->size` as visible length.
  - Recompute rendered content with `editorUpdateRow()` after row text mutations.
- Preserve dirty-state behavior: increment `E.dirty` on text-buffer mutations.
- Keep keyboard behavior stable unless explicitly asked to change key mappings.
- Keep status messaging user-facing and concise through `editorSetStatusMsg()`.

## Validation Steps

- Always run `make` after code edits.
- If editing cursor movement, rendering, or file operations, run an interactive smoke check:
  1. `./rotide README.md`
  2. Test movement keys, insert/delete/newline, and scrolling.
  3. Test save (`Ctrl-S`) and quit (`Ctrl-Q`).
- If interactive execution is not possible, call out the limitation and rely on compile validation.

## References

- Load `references/code-map.md` for a quick map of major functions and common change recipes.
