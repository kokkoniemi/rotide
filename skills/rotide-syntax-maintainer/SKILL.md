---
name: rotide-syntax-maintainer
description: Add, evolve, and debug RotIDE Tree-sitter language support, query behavior, highlighting, and syntax performance modes.
---

# Rotide Syntax Maintainer

Use for Tree-sitter activation, query wiring, incremental parse behavior, and syntax highlighting changes.

## First Inspect

1. Read `references/syntax-playbook.md`.
2. Inspect `src/language/syntax.c` plus the immediate caller/renderer.
3. Check `tests/test_syntax.c` and `tests/test_render_terminal.c`.

## Guardrails

- Keep syntax state tab-local (`syntax_language`, `syntax_state`).
- Preserve fallback behavior for unsupported files (`EDITOR_SYNTAX_NONE`).
- Preserve query/parse budget behavior and degraded modes.
- Keep text-source/document-backed parsing path intact.
- Keep selection/search highlight precedence over syntax colors.

## References

- `references/syntax-playbook.md`
