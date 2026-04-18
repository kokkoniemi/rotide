---
name: rotide-maintainer
description: Maintain and evolve RotIDE across core editor modules, document/rope storage, rendering, keymap/config loading, syntax, LSP, docs, and tests.
---

# Rotide Maintainer

Use this for general RotIDE code, docs, and test work when a narrower skill is not the best fit.

## First Inspect

1. `AGENTS.md`
2. touched modules
3. `references/task-routing.md`

## Guardrails

- Treat `editorDocument` as canonical writable text state.
- Treat `struct erow` rows as derived render/cache state.
- Keep cursor/search/selection logic offset-safe and boundary-safe for UTF-8/graphemes.
- Keep key behavior action-driven via `enum editorAction` and keymap lookups.
- Keep task-log tabs generated/read-only/non-savable.
- Keep syntax and LSP state tab-local.
- Do not regress dirty-state semantics.

## Validation

- Always: `make`, then `make test`.
- Add sanitizer run (`make test-sanitize`) when touching:
  - memory/UB-sensitive paths
  - syntax/query/parse internals
  - save/recovery/history/document storage
  - build/CI plumbing
- If LSAN is flaky locally, use:
  - `ASAN_OPTIONS=detect_leaks=0 make test-sanitize`

## References

- `references/task-routing.md`
