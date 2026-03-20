---
name: rotide-maintainer
description: Maintain and evolve the RotIDE terminal editor across core modules, keymap/config loading, and Tree-sitter syntax integration. Use when working in this repository on bug fixes, feature changes, refactors, code review, docs, and validation with strict compiler flags.
---

# Rotide Maintainer

## Quick Start Workflow

1. Read `AGENTS.md`, `README.md`, `Makefile`, and touched module files (`rotide.c`, `terminal.c`, `buffer.c`, `output.c`, `input.c`, `keymap.c`, `syntax.c`, `alloc.c`, `save_syscalls.c`).
2. Check workspace state with `git status --short`; keep unrelated user changes intact.
3. Implement the smallest viable change while matching current code style (tabs, C2x, sectioned layout).
4. Update or add tests in `tests/rotide_tests.c` when behavior changes; update `tests/test_helpers.c` or test-hook shims when needed.
5. Run `make`; treat all warnings as blockers because `-Werror` is enabled.
6. Run `make test`; all tests must pass.
7. If touching build/CI, sanitizer-sensitive paths, or syntax/highlighting internals, run `make test-sanitize` (if LeakSanitizer is flaky locally, use `ASAN_OPTIONS=detect_leaks=0 make test-sanitize` and note it).
8. Summarize behavior impact, residual risks, and validation performed.

## Project-Specific Guardrails

- Keep the lightweight module architecture unless the user asks for major structural changes.
- Preserve editor row invariants:
  - Keep `row->chars` NUL-terminated with `row->size` as visible length.
  - Recompute rendered content after row text mutations (current mutation paths use `editorRebuildRowRender()`, and `editorUpdateRow()` is a wrapper).
- Preserve dirty-state behavior: increment `E.dirty` on text-buffer mutations, not view-only/search-only actions.
- Keep keyboard behavior stable unless explicitly asked to change mappings; route behavior through `enum editorAction` + keymap lookup paths.
- Keep syntax behavior stable: preserve per-tab/per-buffer syntax state ownership and incremental parse flows.
- Keep status messaging user-facing and concise through `editorSetStatusMsg()`.

## Validation Steps

- Always run `make` after code edits.
- Always run `make test` after code edits.
- If changing build/CI, sanitizer-sensitive paths, or syntax/query behavior, run `make test-sanitize` (if LeakSanitizer is blocked locally, rerun with `ASAN_OPTIONS=detect_leaks=0` and call out the limitation).
- If behavior changes, ensure tests are updated or added to cover the new behavior.
- If editing input, cursor movement, rendering, save, search, tabs, or drawer behavior, run an interactive smoke check:
  1. `./rotide README.md`
  2. Test movement/editing/search/tab+drawer interactions and scrolling.
  3. Test save (`Ctrl-S`) and quit (`Ctrl-Q`).
- If interactive execution is not possible, call out the limitation and rely on compile + test validation.

## References

- Load `references/code-map.md` for a quick map of major functions and common change recipes.
