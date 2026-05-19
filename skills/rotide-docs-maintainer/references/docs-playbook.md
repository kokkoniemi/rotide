# Docs Playbook

## Source-of-truth order

When documenting behavior, verify in this order:

1. `src/rotide.h` (types, enums, state fields)
2. module implementation under `src/`:
   - editing core: `src/editing/buffer_core.c`, `src/editing/edit.c`, `src/editing/selection.c`, `src/editing/history.c`
   - text storage: `src/text/document.c`, `src/text/text_tree.c`,
     `src/text/text_buffer.c`, `src/text/text_summary.c`,
     `src/editing/row_cache.c`
   - input/render: `src/input/dispatch.c`, `src/input/actions_*.c`,
     `src/input/mouse.c`, `src/input/prompt.c`, `src/render/screen.c`,
     `src/render/pane_view.c`, `src/render/*_view.c`
   - language: `src/language/syntax.c`, `src/language/queries.c`, `src/language/languages.c`, `src/language/lsp.c`
   - config/workspace: `src/config/*.c`, `src/workspace/*.c`
3. split test suites in `tests/` (`test_syntax.c`, `test_lsp.c`, `test_render_terminal.c`, `test_document_text_editing.c`, `test_save_recovery.c`, `test_input_search.c`, `test_workspace_config.c`) for behavior contracts
4. existing docs (README/AGENTS/skills) for wording continuity

## Required consistency checks

- README feature list matches actual shipped behavior.
- README terminology aligns with canonical model:
  - document/text-tree canonical storage
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
