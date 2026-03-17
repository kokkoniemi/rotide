# Search Playbook

## Primary touchpoints

- `input.c`
  - `editorProcessKeypress()` for key dispatch (`Ctrl-F`, `Ctrl-N`, `Ctrl-P`)
  - prompt flow (`editorPrompt`) for live query updates
  - cursor positioning and movement helpers
- `output.c`
  - row draw path for active-match highlight rendering
  - keep status/message bar behavior stable
- `rotide.h`
  - editor state fields needed by search flow/highlight flow
- `tests/rotide_tests.c`
  - search behavior tests and rendering checks

## Recommended state model

Keep search state explicit and owned by editor state:

- active query buffer (or pointer)
- active match row/column span
- last navigation direction (`+1` forward, `-1` backward)
- saved cursor position for prompt cancel rollback

Avoid storing search-only state in row structs.

## Prompt and callback strategy

Current prompt API is synchronous and save uses it directly.
Prefer additive evolution:

1. Add callback-capable prompt internals.
2. Keep `editorPrompt(const char *prompt)` as compatibility wrapper.
3. Implement search prompt on top of callback path for live updates.

This avoids regressions in save and keeps API churn low.

## Match and navigation behavior

Default behavior:

- `Ctrl-F`: start prompt and jump to first match while typing.
- Enter: confirm current match.
- Esc: cancel and restore pre-search cursor position.
- `Ctrl-N`: move to next match after a search exists.
- `Ctrl-P`: move to previous match after a search exists.

Navigation rules:

- wrap from end to start and start to end
- remain deterministic for repeated keypresses
- treat no-match query as non-fatal and surface status feedback

## Highlight behavior

Use VT100 inverted colors around the active visible match span only.

- start highlight before visible match bytes
- reset colors immediately after highlighted bytes
- avoid leaking styles across subsequent text/rows

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
- `Ctrl-N` and `Ctrl-P` traverse matches with wrap-around.
- refresh output includes highlight escape sequences for active match.
- no-match query does not crash and leaves stable state.

Run:

- `make`
- `make test`

For search rendering regressions, run interactive smoke check:

- `./rotide README.md`
- verify typing, search, movement, scrolling, save, quit.
