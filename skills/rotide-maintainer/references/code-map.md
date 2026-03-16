# Rotide Code Map

## Major Sections in rotide.c

- `/*** Terminal ***/`: raw mode setup, key decoding, window sizing, panic/quit behavior.
- `/*** File io ***/`: row storage, row rendering cache updates, open/save logic, insert/delete operations.
- `/*** Write buffer ***/`: append-only screen buffer helper used before terminal writes.
- `/*** Output ***/`: screen drawing, status bar, message bar, scroll offsets, cursor placement.
- `/*** Input ***/`: prompt handling, movement logic, key dispatch, editor initialization, main loop.

## Common Change Recipes

### Add or modify key handling

1. Update parsing in `editorReadKey()` if a new escape sequence is needed.
2. Handle behavior in `editorProcessKeypress()`.
3. Keep post-keypress `quit_confirmed = 0` reset behavior unless intentionally changing quit flow.

### Modify text mutation behavior

1. Touch one of `editorInsertCharAt()`, `editorDelCharAt()`, `editorInsertRow()`, `editorDeleteRow()`, or `editorRowAppendString()`.
2. Ensure `editorUpdateRow()` is called for affected rows.
3. Ensure `E.dirty` increments for user-visible buffer changes.

### Adjust rendering or cursor behavior

1. Verify `E.cx` (character index) to `E.rx` (render index) conversion remains consistent via `editorRowCxToRx()`.
2. Check offset logic in `editorScroll()` for horizontal and vertical movement.
3. Validate status bar and message bar output width handling in `editorDrawStatusBar()` and `editorDrawMessageBar()`.

## Known Constraints

- UTF-8 cursor column/render width handling exists, but editing operations still work on bytes rather than grapheme clusters.
- TODO in source: `editorSave()` should ideally write to a temp file and rename on success.
