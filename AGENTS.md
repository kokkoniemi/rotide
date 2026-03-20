# AGENTS.md instructions for /home/mk/Development/rotide

## Project context

- `rotide` is a small terminal text editor inspired by kilo.
- Core code is split across focused modules (`terminal.c`, `buffer.c`, `output.c`, `input.c`, `keymap.c`, `syntax.c`, `lsp.c`, `rotide.c`) with supporting utility/save backend modules (`alloc.c`, `save_syscalls.c`) and vendored Tree-sitter sources.
- Prioritize readability and simple control flow over micro-optimizations.

## Repository layout

- `rotide.c`/`rotide.h`: process lifecycle, editor initialization, and shared state/types.
- `terminal.c`/`terminal.h`: terminal mode, key decoding, terminal size helpers, mouse/OSC52 handling.
- `buffer.c`/`buffer.h`: buffer model, UTF-8/grapheme helpers, file open/save, undo/redo, tabs/drawer state transitions.
- `output.c`/`output.h`: rendering pipeline (tab bar, drawer, text viewport, status/message bars) and screen refresh.
- `input.c`/`input.h`: prompt flow, search/go-to-line, keypress processing, and mapped action dispatch.
- `keymap.c`/`keymap.h`: keymap defaults, TOML config parsing, and lookup/format helpers.
- `syntax.c`/`syntax.h`: Tree-sitter integration, incremental parse wrappers, and syntax query helpers.
- `lsp.c`/`lsp.h`: JSON-RPC client glue for Go LSP (`gopls`) lifecycle, sync notifications, and definition requests.
- `alloc.c`/`alloc.h`: allocation wrappers and test-hook integration.
- `save_syscalls.c`/`save_syscalls.h`: save-related syscall wrappers and failure injection hooks.
- `tests/rotide_tests.c`: unit and behavior test cases.
- `tests/test_helpers.c`/`tests/test_helpers.h`: shared test helpers/assertions.
- `tests/alloc_test_hooks.c` + `tests/save_syscalls_test_hooks.c`: hook shims used by the test build.
- `vendor/tree_sitter/`: vendored Tree-sitter runtime and grammars used by build/test.
- `scripts/refresh_tree_sitter_vendor.sh`: maintainer helper to refresh pinned Tree-sitter artifacts.
- `Makefile`: canonical build command and compiler flags.
- `README.md`: brief project description.
- `.github/workflows/ci.yml`: CI build/test workflow plus sanitizer coverage (`make test-sanitize`).

## Build and validation

- Build with `make`.
- Run tests with `make test`.
- Run sanitizer checks with `make test-sanitize` when touching build/CI, memory safety, undefined-behavior-sensitive code paths, or syntax/highlighting internals.
- Treat warnings as errors (`-Werror` is enabled via Makefile flags).
- After code edits, always run `make`.
- After code edits, always run `make test`.
- If LeakSanitizer is flaky locally, rerun sanitizer checks with `ASAN_OPTIONS=detect_leaks=0 make test-sanitize` and call out the limitation.
- If behavior changes, add or update tests in `tests/rotide_tests.c` so coverage follows the change.
- When behavior changes in input/render/save/search/drawer paths, run an interactive smoke test:
  1. `./rotide README.md`
  2. Verify insert/delete/newline, cursor movement, scrolling, save (`Ctrl-S`), quit (`Ctrl-Q`), search prompt flow (`Ctrl-F` + arrows), and basic tab/drawer navigation.

## Coding guidance

- Match existing style:
  - C2x
  - tabs for indentation
  - section markers like `/*** Output ***/`
- Keep architecture lightweight unless user explicitly asks for splitting into more files.
- Preserve core invariants:
  - Row mutations refresh render cache (current mutation paths call `editorRebuildRowRender()`, and `editorUpdateRow()` is a wrapper).
  - Buffer mutations update `E.dirty`; search/navigation-only actions should not.
  - `row->chars` remains NUL-terminated.
  - Key behavior routes through keymap actions (`enum editorAction`) instead of ad-hoc raw key branches.
  - Syntax state remains per tab/buffer and must not leak across tabs.
- Do not revert unrelated local changes.

## Skills

### Available skills

- rotide-maintainer: Use for bug fixes, feature work, refactors, and reviews in this repository, especially changes in `rotide.c` with strict validation. (file: /home/mk/Development/rotide/skills/rotide-maintainer/SKILL.md)
- rotide-search-maintainer: Use for search prompt/match/highlight changes and search regression fixes. (file: /home/mk/Development/rotide/skills/rotide-search-maintainer/SKILL.md)
- rotide-syntax-maintainer: Use for Tree-sitter language onboarding, syntax query tuning, and syntax highlight/render regressions. (file: /home/mk/Development/rotide/skills/rotide-syntax-maintainer/SKILL.md)

### How to use skills

- Trigger `rotide-maintainer` when the task is to implement, review, test, or document changes in this project.
- Trigger `rotide-search-maintainer` for search-specific work (`Ctrl-F` flow, prompt navigation, active match highlight, related tests).
- Trigger `rotide-syntax-maintainer` for syntax-specific work (Tree-sitter grammar/query integration, syntax detection changes, syntax highlight regressions, syntax performance/budget behavior).
- Read the selected `SKILL.md` first, then load only the relevant reference (`references/code-map.md`, `references/search-playbook.md`, or `references/syntax-playbook.md`) when deeper implementation mapping is needed.
