# AGENTS.md instructions for /home/mk/Development/rotide

## Project context 	

- `rotide` is a terminal text editor inspired by kilo.
- Priorities: deterministic behavior, readable control flow, and strong regression coverage.
- Current architecture is document-first:
  - canonical text storage: `editorDocument` (`src/text/document.c`) backed by rope chunks (`src/text/rope.c`)
  - row arrays (`struct erow`) are derived render/cache state, rebuilt from the document
- Keep refactors incremental and preserve user-facing behavior unless explicitly requested.

## Repository layout

- `src/`: first-party runtime source tree.
- `src/rotide.c` / `src/rotide.h`: process lifecycle, startup config loading, global state/type definitions.
- `src/support/`: low-level support code such as terminal I/O, allocation wrappers, save syscalls, and file/path helpers.
- `src/text/`: canonical text primitives such as document storage, rope chunks, UTF-8 helpers, and row rendering helpers.
- `src/editing/`: document edit orchestration, row-cache rebuild, save path, selection, and undo/redo integration.
- `src/workspace/`: tabs, drawer, recovery, and task-log workspace behavior.
- `src/input/`: prompt flows, action dispatch, movement/search/go-to-line/go-to-definition, mouse behavior.
- `src/render/`: rendering pipeline for tabs, drawer, text area, status, and message bars.
- `src/config/`: keymap defaults, TOML parsing, config precedence, and editor/theme/LSP config loading.
- `src/language/`: Tree-sitter syntax integration and Go/C/C++ LSP lifecycle/document sync/definition handling.
- `src/text/`: shared UTF-8, grapheme, and row/render helpers.
- `tests/rotide_tests.c`: main unit/behavior regression suite.
- `tests/test_helpers.c` / `tests/test_helpers.h`: fixture helpers, path resolution, assertion helpers.
- `tests/alloc_test_hooks.c` + `tests/save_syscalls_test_hooks.c`: hook shims for tests.
- `tests/syntax/`: syntax fixture tree (`supported/` + `planned/` placeholders).
- `vendor/tree_sitter/`: vendored runtime/grammars + query files and pinned version metadata.
- `scripts/refresh_tree_sitter_vendor.sh`: refresh pinned Tree-sitter vendor artifacts.
- `.github/workflows/ci.yml`: build/test and sanitizer CI.

## Build and validation

- Build with `make`.
- Run tests with `make test`.
- Run sanitizer suite with `make test-sanitize` when touching:
  - memory/UB-sensitive paths
  - syntax/highlighting internals
  - save/recovery or parser/query behavior
  - build/CI-related changes
- If LeakSanitizer is flaky locally, rerun with:
  - `ASAN_OPTIONS=detect_leaks=0 make test-sanitize`
  - mention that limitation in your report.
- Treat warnings as blockers (`-Werror` is enabled).

## Coding guidance

- Match project style:
  - C2x
  - tabs for indentation
  - section markers (`/*** ... ***/`)
- Keep changes minimal and explicit.
- Preserve core invariants:
  - `editorDocument` is canonical writable text state.
  - `row->chars` remains NUL-terminated for derived row cache rows.
  - text mutations update `E.dirty`; navigation/search/view changes do not.
  - key behavior routes through `enum editorAction` + keymap paths.
  - syntax/LSP state remains tab-local and must not leak across tabs.
  - task-log tabs stay read-only and non-savable.
- Do not revert unrelated local changes.

## Behavior-sensitive smoke checks

When input/output/buffer behavior changes, run interactive smoke:

1. `./rotide README.md`
2. Verify:
   - insert/delete/newline and cursor movement
   - search prompt flow (`Ctrl-F`, arrows, Enter/Esc)
   - save (`Ctrl-S`) and quit (`Ctrl-Q`)
   - tab switching and drawer interactions (click, keyboard, resize)

## Skills

### Available skills

- `rotide-maintainer`: general code/docs/test changes across the repository.
- `rotide-domain-refactor`: domain-oriented refactors, module ownership cleanup, header narrowing, and dead-weight removal.
- `rotide-document-maintainer`: canonical document/rope/edit-history/recovery changes.
- `rotide-search-maintainer`: search prompt, active match navigation/highlight behavior.
- `rotide-syntax-maintainer`: Tree-sitter language/query/incremental parse/highlighting changes.
- `rotide-lsp-maintainer`: Go/C/C++ LSP lifecycle/sync/definition/install-prompt/task-log flows.
- `rotide-docs-maintainer`: README/AGENTS/skill/reference documentation quality and consistency.

### How to use skills

- Use `rotide-maintainer` by default for most implementation or review tasks.
- Use specialized skills when task scope matches their area to reduce context drift.
- Read the selected `SKILL.md` first, then open only the relevant reference file.
- If multiple domains are touched, combine skills deliberately and keep ownership boundaries clear.
