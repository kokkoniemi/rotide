# Rotide Code Map

## Major Files

- `rotide.c`: global editor state (`E`), initialization, and `main()`.
- `terminal.c`: raw mode setup, key decoding, window sizing, panic/terminal reset helpers.
- `buffer.c`: row/buffer mutations, UTF-8 and grapheme helpers, open/save, status message updates.
- `output.c`: write buffer, draw rows/status/message, scrolling, screen refresh.
- `input.c`: prompt handling, movement logic, key dispatch and quit flow.
- `tests/rotide_tests.c`: test cases.
- `tests/test_helpers.c`: reusable test fixtures/assertions and fd redirection helpers.

## Common Change Recipes

### Add or modify key handling

1. Update parsing in `editorReadKey()` if a new escape sequence is needed.
2. Handle behavior in `editorProcessKeypress()`.
3. Keep post-keypress `quit_confirmed = 0` reset behavior unless intentionally changing quit flow.
4. Add/adjust tests covering the new key path in `tests/rotide_tests.c`.

### Modify text mutation behavior

1. Touch one of `editorInsertCharAt()`, `editorDelCharAt()`, `editorInsertRow()`, `editorDeleteRow()`, or `editorRowAppendString()`.
2. Ensure `editorUpdateRow()` is called for affected rows.
3. Ensure `E.dirty` increments for user-visible buffer changes.
4. Add/adjust tests for row content, cursor effects, and dirty state.

### Adjust rendering or cursor behavior

1. Verify `E.cx` (character index) to `E.rx` (render index) conversion remains consistent via `editorRowCxToRx()`.
2. Check offset logic in `editorScroll()` for horizontal and vertical movement.
3. Validate status bar and message bar output width handling in `editorDrawStatusBar()` and `editorDrawMessageBar()`.
4. Add/adjust tests that capture `editorRefreshScreen()` output and scroll offsets.

## Validation Baseline

- Run `make`.
- Run `make test`.
- Keep tests updated when behavior changes.

## Known Constraints

- Cursor movement and deletion are grapheme-cluster-aware for combining marks, variation selectors, emoji modifiers, ZWJ emoji sequences, and RI flag pairs.
- TODO in source: `editorSave()` should ideally write to a temp file and rename on success.
