---
name: rotide-syntax-maintainer
description: Add, evolve, and debug RotIDE Tree-sitter language support, query behavior, highlighting, and syntax performance modes.
---

# Rotide Syntax Maintainer

## Scope

Use for:
- language detection/activation changes
- parser/query wiring changes
- highlight capture mapping changes
- incremental parse regressions
- injection/predicate/locals behavior
- syntax budget/degraded-mode tuning

## Workflow

1. Read `references/syntax-playbook.md`.
2. Confirm scope (new language vs query tweak vs bug/perf fix).
3. Apply changes in this order:
   - `rotide.h` enums/types if needed
   - `syntax.c` parser/query/capture wiring
   - `buffer.c` activation/parse/incremental-edit integration
   - `output.c` rendering interactions when needed
   - `Makefile` + `vendor/tree_sitter/*` + refresh script if vendor/build changes are required
4. Update tests in `tests/rotide_tests.c`.
5. Run `make`, `make test`, and sanitizer suite for syntax-sensitive changes.

## Guardrails

- Keep syntax state tab-local (`syntax_language`, `syntax_state`).
- Preserve fallback behavior for unsupported files (`EDITOR_SYNTAX_NONE`).
- Preserve query/parse budget behavior and degraded modes.
- Keep text-source/document-backed parsing path intact.
- Keep selection/search highlight precedence over syntax colors.

## References

- `references/syntax-playbook.md`
