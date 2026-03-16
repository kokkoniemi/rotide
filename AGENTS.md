# AGENTS.md instructions for /home/mk/Development/rotide

## Project context

- `rotide` is a small terminal text editor inspired by kilo.
- Core code is split across focused modules (`terminal.c`, `buffer.c`, `output.c`, `input.c`, `rotide.c`).
- Prioritize readability and simple control flow over micro-optimizations.

## Repository layout

- `rotide.c`: entry point and editor initialization.
- `terminal.c`/`terminal.h`: terminal mode, key decoding, terminal size helpers.
- `buffer.c`/`buffer.h`: buffer model, UTF-8/grapheme helpers, file open/save.
- `output.c`/`output.h`: rendering pipeline and screen refresh.
- `input.c`/`input.h`: prompt and keypress processing.
- `tests/rotide_tests.c`: test cases.
- `tests/test_helpers.c`/`tests/test_helpers.h`: shared test helpers/assertions.
- `Makefile`: canonical build command and compiler flags.
- `README.md`: brief project description.
- `.github/workflows/ci.yml`: CI build + test workflow.

## Build and validation

- Build with `make`.
- Run tests with `make test`.
- Treat warnings as errors (`-Werror` is enabled via Makefile flags).
- After code edits, always run `make`.
- After code edits, always run `make test`.
- If behavior changes, add or update tests in `tests/rotide_tests.c` so coverage follows the change.
- When behavior changes in input/render/save paths, run an interactive smoke test:
  1. `./rotide README.md`
  2. Verify insert/delete/newline, cursor movement, scrolling, save (`Ctrl-S`), quit (`Ctrl-Q`).

## Coding guidance

- Match existing style:
  - C2x
  - tabs for indentation
  - section markers like `/*** Output ***/`
- Keep architecture lightweight unless user explicitly asks for splitting into more files.
- Preserve core invariants:
  - Row mutations refresh render cache via `editorUpdateRow()`.
  - Buffer mutations update `E.dirty`.
  - `row->chars` remains NUL-terminated.
- Do not revert unrelated local changes.

## Skills

### Available skills

- rotide-maintainer: Use for bug fixes, feature work, refactors, and reviews in this repository, especially changes in `rotide.c` with strict validation. (file: /home/mk/Development/rotide/skills/rotide-maintainer/SKILL.md)

### How to use skills

- Trigger `rotide-maintainer` when the task is to implement, review, test, or document changes in this project.
- Read `SKILL.md` first, then load `references/code-map.md` only when deeper implementation mapping is needed.
