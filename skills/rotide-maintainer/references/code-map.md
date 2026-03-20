# Rotide Code Map

## Major Files

- `rotide.c`/`rotide.h`: global editor state (`E`), lifecycle, and shared enums/structs.
- `terminal.c`: raw mode setup, key decoding, mouse/OSC52 parsing, window sizing, terminal reset helpers.
- `buffer.c`: row/buffer mutations, UTF-8/grapheme helpers, open/save, undo/redo, tabs/drawer state transitions, syntax-visible range prep.
- `output.c`: draw tab bar/drawer/text/status/message, scrolling, cursor placement, screen refresh, search/selection/syntax overlays.
- `input.c`: prompt handling, search callback flow, go-to-line, movement logic, mapped action dispatch, quit flow.
- `keymap.c`: keymap defaults, TOML parsing/loading, action lookup, formatted binding/help status text.
- `syntax.c`: Tree-sitter state wrappers, incremental parse/edit paths, query capture collection, budget test hooks.
- `alloc.c`: allocator wrappers used by editor code and tests.
- `save_syscalls.c`: syscall wrapper layer for atomic save path and test fault injection.
- `tests/rotide_tests.c`: test cases.
- `tests/test_helpers.c`: reusable test fixtures/assertions and fd redirection helpers.
- `tests/alloc_test_hooks.c` + `tests/save_syscalls_test_hooks.c`: test-only hook shims.
- `vendor/tree_sitter/`: vendored runtime/grammar sources (C/bash/html/javascript/css).
- `scripts/refresh_tree_sitter_vendor.sh`: vendor refresh workflow for pinned parser/runtime sources.

## Common Change Recipes

### Add or modify key handling

1. Update terminal key parsing in `editorReadKey()` only if a new escape sequence is needed.
2. Add/update action wiring in `rotide.h`/`keymap.c`/`keymap.h` when behavior needs a new mapped action.
3. Handle behavior in `editorProcessMappedAction()` / `editorProcessKeypress()`.
4. Keep close/quit confirmation reset behavior intact unless intentionally changing those flows.
5. Add/adjust tests covering the new action path (and config parsing if relevant) in `tests/rotide_tests.c`.

### Modify text mutation behavior

1. Touch one of `editorInsertCharAt()`, `editorDelCharAt()`, `editorInsertRow()`, `editorDeleteRow()`, or `editorRowAppendString()`.
2. Ensure affected rows rebuild render cache (`editorRebuildRowRender()` in current mutation paths; `editorUpdateRow()` is the public wrapper).
3. Ensure `E.dirty` increments for user-visible buffer changes and stays unchanged for view-only behavior.
4. Add/adjust tests for row content, cursor effects, dirty state, and undo/redo as needed.

### Adjust rendering or cursor behavior

1. Verify `E.cx` (character index) to `E.rx` (render index) conversion remains consistent via `editorRowCxToRx()`.
2. Check offset logic in `editorScroll()` for horizontal and vertical movement.
3. Validate tab bar/drawer/status/message output width and style reset handling in `output.c`.
4. Add/adjust tests that capture `editorRefreshScreen()` output and scroll offsets.

### Adjust syntax highlighting or parsing behavior

1. Update syntax integration touchpoints in `buffer.c` and query/parser logic in `syntax.c`.
2. Keep syntax state ownership per buffer/tab and preserve incremental update flows.
3. Validate render-span interactions between syntax captures (`buffer.c`) and paint path (`output.c`).
4. Add/adjust syntax-focused tests and run sanitizer checks.

## Validation Baseline

- Run `make`.
- Run `make test`.
- Run `make test-sanitize` when touching CI/build, syntax/query behavior, or memory/UB-sensitive paths.
- Keep tests updated when behavior changes.
