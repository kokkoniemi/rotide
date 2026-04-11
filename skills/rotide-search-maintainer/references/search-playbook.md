# Search Playbook

## Primary touchpoints

- `input.c`
  - `editorFind()`
  - `editorFindCallback()`
  - `editorMoveCursorToSearchMatch()`
  - `editorRestoreCursorToSavedSearchPosition()`
- `buffer.c`
  - `editorBufferFindForward()`
  - `editorBufferFindBackward()`
  - text-source-backed line range helpers
- `output.c`
  - active match highlight rendering in row draw path
- `rotide.h`
  - search fields in `editorConfig` / `editorTabState`

## Search state model (current)

Search is offset-first:

- query text: `search_query`
- active match: `search_match_offset` + `search_match_len`
- saved cursor for cancel: `search_saved_offset`
- navigation direction: `search_direction`

Avoid introducing row-index-only search state.

## Behavioral baseline

- `Ctrl-F` opens prompt and starts incremental matching.
- Typing updates active match immediately.
- `Right/Down` moves to next match.
- `Left/Up` moves to previous match.
- Enter keeps current match/cursor.
- Esc restores the cursor to `search_saved_offset`.
- Empty query restores saved cursor and clears active match.
- No-match query should be non-fatal and stable.

## Rendering baseline

- Highlight only the active match.
- Ensure VT100 attributes are restored immediately after the match span.
- Keep precedence stable with selection and syntax overlays.

## UTF-8 and cursor safety

- Search itself may be byte-substring based.
- Cursor placement after jumps must clamp to valid character/grapheme boundaries.
- Keep offset <-> `(cy,cx)` mappings consistent with document helpers.

## Test checklist

- `Ctrl-F` first match selection.
- prompt arrow traversal and wrap-around.
- Esc restore behavior.
- Enter keep behavior.
- no-match behavior and status message stability.
- highlight escape sequence presence in refresh output.

Validation:
- `make`
- `make test`
