# AGENTS.md instructions for /home/mk/Development/rotide

## Project context

- `rotide` is a small terminal text editor inspired by kilo.
- Main code lives in a single source file: `rotide.c`.
- Prioritize readability and simple control flow over micro-optimizations.

## Repository layout

- `rotide.c`: editor implementation (terminal control, buffer model, rendering, input loop).
- `Makefile`: canonical build command and compiler flags.
- `README.md`: brief project description.
- `ask`: scratch/test text file.

## Build and validation

- Build with `make`.
- Treat warnings as errors (`-Werror` is enabled via Makefile flags).
- After code edits, always run `make`.
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
