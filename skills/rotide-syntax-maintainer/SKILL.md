---
name: rotide-syntax-maintainer
description: Add, evolve, and debug RotIDE Tree-sitter language support (detection, parser/query wiring, render spans, and tests). Use when onboarding new syntax languages, refreshing grammar/query behavior, or fixing syntax highlight regressions.
---

# Rotide Syntax Maintainer

## Overview

Maintain the full syntax pipeline: language detection -> parse state -> query captures -> render spans -> user-visible highlighting.
Prefer minimal, incremental changes that keep non-target languages and plain-text fallback stable.

## Workflow

1. Read `references/syntax-playbook.md` before editing.
2. Identify the exact scope: new language, query refresh, detection tweak, performance regression, or rendering bug.
3. Apply changes in this order:
   - language enum + parser/query plumbing in `rotide.h`/`syntax.c`/`syntax.h`
   - activation/detection hooks used by `buffer.c`
   - rendering/highlight precedence checks in `output.c`
   - build/vendor updates (`Makefile`, `vendor/tree_sitter/*`, `scripts/refresh_tree_sitter_vendor.sh`, `vendor/tree_sitter/VERSIONS.*`) when grammar sources change
4. Update tests in `tests/rotide_tests.c` for detection, incremental edits, and render output.
5. Run `make` and `make test`.
6. Run `make test-sanitize`; if LeakSanitizer is flaky, rerun with `ASAN_OPTIONS=detect_leaks=0 make test-sanitize` and note the limitation.
7. If behavior changed, run a smoke check with `./rotide README.md` and at least one sample file in the target language.

## Guardrails

- Preserve per-tab syntax ownership (`syntax_language` + `syntax_state` in tab/editor state).
- Keep unsupported-language fallback stable (`EDITOR_SYNTAX_NONE`).
- Keep query-file loading with builtin-query fallback intact.
- Preserve query/parse budget and degraded-mode behavior.
- Keep style reset behavior intact so syntax colors do not leak across rows.
- Do not regress save/open/restore flows that reinitialize syntax state.

## Implementation Defaults

- Prefer loading highlight queries from `vendor/tree_sitter/grammars/<lang>/queries/*.scm` with builtin string fallback in `syntax.c`.
- Map capture names to existing semantic classes unless a new class is explicitly requested.
- Use extension-first language detection; add shebang or secondary heuristics only when intentional.
- Keep parser/query changes additive and deterministic.

## Test Expectations

- Add or update tests for:
  - file detection + syntax activation (open/save-as)
  - incremental parse/edit stability for the target language
  - render highlighting for representative tokens
  - syntax-state isolation across tabs
  - graceful behavior when syntax/query budget is exceeded (if relevant)
- Keep existing non-syntax tests passing.

## References

- Load `references/syntax-playbook.md` for code-level touchpoints and language onboarding checklist.
