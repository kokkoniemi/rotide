---
name: rotide-maintainer
description: Maintain and evolve RotIDE across core editor modules, document/rope storage, rendering, keymap/config loading, syntax, LSP, docs, and tests.
---

# Rotide Maintainer

## Quick Start Workflow

1. Read `AGENTS.md`, `README.md`, and touched modules.
2. Check workspace status with `git status --short`; preserve unrelated user edits.
3. Implement the smallest behavior-correct change that matches project style.
4. Update tests when behavior changes (`tests/rotide_tests.c` + helpers/hooks as needed).
5. Run `make`.
6. Run `make test`.
7. Run `make test-sanitize` for sanitizer-sensitive/syntax/storage changes.
8. Summarize impact, risks, and validation.

## Guardrails

- Treat `editorDocument` as canonical writable text state.
- Treat `struct erow` rows as derived render/cache state.
- Keep cursor/search/selection logic offset-safe and boundary-safe for UTF-8/graphemes.
- Keep key behavior action-driven via `enum editorAction` and keymap lookups.
- Keep task-log tabs generated/read-only/non-savable.
- Keep syntax and LSP state tab-local.
- Do not regress dirty-state semantics.

## Validation Defaults

- Always: `make`, then `make test`.
- Add sanitizer run (`make test-sanitize`) when touching:
  - memory/UB-sensitive paths
  - syntax/query/parse internals
  - save/recovery/history/document storage
  - build/CI plumbing
- If LSAN is flaky locally, use:
  - `ASAN_OPTIONS=detect_leaks=0 make test-sanitize`

## References

- `references/code-map.md`
