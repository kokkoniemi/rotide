# Search Playbook

## Primary touchpoints

- `input.c`
  - `editorFind()` + `editorFindCallback()` for query lifecycle and incremental selection
  - prompt flow (`editorPromptWithCallback()` / `editorPrompt()`) for live query updates
  - cursor restore/alignment helpers for cancel/confirm behavior
- `output.c`
  - row draw path (`editorDrawRenderSlice`) for active-match highlight rendering
  - keep status/message bar behavior stable
- `rotide.h`
  - editor state fields used by search flow/highlight flow
- `tests/rotide_tests.c`
  - search behavior tests and rendering checks

## Current state model

Keep search state explicit and owned by editor state (`E`):

- active query pointer (`E.search_query`)
- active match row/start/len (`E.search_match_row`, `E.search_match_start`, `E.search_match_len`)
- current navigation direction (`E.search_direction`)
- saved cursor for prompt cancel rollback (`E.search_saved_cx`, `E.search_saved_cy`)

Avoid storing search-only state in row structs.

## Prompt flow and navigation behavior

Current behavior baseline:

- `Ctrl-F`: start prompt and jump to first match while typing.
- `Down/Right`: move to next match while prompt is active.
- `Up/Left`: move to previous match while prompt is active.
- Enter: confirm current match.
- Esc: cancel and restore pre-search cursor position.
- Empty query: restore cursor and clear active match.

Navigation rules:

- wrap from end to start and start to end
- remain deterministic for repeated keypresses
- treat no-match query as non-fatal and surface status feedback

## Highlight behavior

Use VT100 inverted colors around the active visible match span only.

- start highlight before visible match bytes
- reset colors immediately after highlighted bytes
- avoid leaking styles across subsequent text/rows
- preserve expected precedence interactions with syntax and selection overlays

Compute highlight byte spans from `row->chars` indices and convert as needed for render slicing.

## UTF-8 and cursor safety

Search can be byte-substring based, but cursor moves must remain boundary-safe.

- clamp target `cx` with existing cluster boundary helpers
- keep `E.cx` and `E.rx` semantics intact
- do not split grapheme clusters when positioning cursor

## Test scenarios

Add tests covering at least:

- `Ctrl-F` opens prompt and incremental query updates select matches.
- Enter confirmation keeps matched cursor location.
- Esc cancellation restores original cursor location.
- prompt arrow navigation traverses matches with wrap-around.
- refresh output includes highlight escape sequences for active match.
- no-match query does not crash and leaves stable state.

Run:

- `make`
- `make test`

For search rendering regressions, run interactive smoke check:

- `./rotide README.md`
- verify typing, search prompt navigation, movement, scrolling, save, quit.
