# Docs Playbook

## Source-of-truth order

When documenting behavior, verify in this order:

1. `rotide.h` (types, enums, state fields)
2. module implementation (`buffer.c`, `input.c`, `output.c`, `syntax.c`, `lsp.c`, `keymap.c`)
3. tests (`tests/rotide_tests.c`) for behavior contracts
4. existing docs (README/AGENTS/skills) for wording continuity

## Required consistency checks

- README feature list matches actual shipped behavior.
- README terminology aligns with canonical model:
  - document/rope canonical storage
  - derived row cache
  - offset-first cursor/search/selection state
- AGENTS skill list matches directories under `skills/`.
- Skill descriptions/references are not stale relative to current architecture.
- Config docs reflect real precedence and validation behavior.

## Writing style for this repo

- Prefer explicit module/function references.
- Define internal terms once in README (“Terminology” section).
- Keep user-facing docs practical, contributor docs action-oriented.
- Avoid promising roadmap features as current functionality.

## Validation baseline for docs PRs

- `make`
- `make test`
