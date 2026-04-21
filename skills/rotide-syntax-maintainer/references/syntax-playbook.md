# Syntax Playbook

## Primary touchpoints

- `rotide.h`
  - `enum editorSyntaxLanguage`
  - highlight class enums
  - tab/editor syntax state fields
- `syntax.c` / `syntax.h`
  - language objects (`tree_sitter_*`)
  - query cache + fallback query strings
  - capture-name to semantic-class mapping
  - filename/shebang detection helpers
  - injection and locals/predicate handling
- `buffer.c`
  - syntax activation/reconfiguration
  - full and incremental parse entrypoints
  - visible-row syntax span cache invalidation/rebuild
- `output.c`
  - syntax span paint path and overlay precedence
- `Makefile` + `vendor/tree_sitter/*` + refresh script when grammar assets change
- `tests/rotide_tests.c`
  - activation, incremental parse validity, render highlights, budgets, tab isolation
- `tests/syntax/`
  - fixture files under `supported/` and placeholder scaffolding under `planned/`

## Currently supported syntax families

- C / C++
- Go
- Shell (bash language backend)
- HTML (with JS/CSS injections)
- JavaScript (`.js`, `.mjs`, `.cjs`, `.jsx`)
- TypeScript (`.ts`, `.tsx`, `.cts`, `.mts`) — grammar from tree-sitter-typescript `typescript/` sub-grammar; shared `common/` lives at `vendor/tree_sitter/grammars/common/`
- CSS (including `.scss` detection path)
- JSON (`.json`, `.jsonc`)
- Python (`.py`, `.pyi`, `.pyw`, plus extensionless shebang detection for `python` / `python3`)
- PHP (`.php`, `.phtml`, `.php3`–`.php8`, `.phps`, plus extensionless shebang detection for `php` / `php8`); uses the `php/` sub-grammar (HTML-mixed variant), not `php_only/`. Shared `common/` from the PHP repo is vendored per-grammar to `vendor/tree_sitter/grammars/php/common/`; the refresh script also patches `src/scanner.c`'s `../../common/scanner.h` include down to `../common/scanner.h` so it doesn't collide with tree-sitter-typescript's unrelated `common/scanner.h` at `vendor/tree_sitter/grammars/common/`.

## TypeScript vendoring notes

- The repo has `typescript/` and `tsx/` sub-grammars; only `typescript/` is vendored (handles both `.ts` and `.tsx`).
- `grammar.js` requires `../common/define-grammar` → `tree-sitter-javascript` dep; the refresh script links JS grammar source via `link_grammar_dep` before regenerating.
- `scanner.c` includes `../../common/scanner.h`; the shared `common/` from the TS repo is copied to `vendor/tree_sitter/grammars/common/`.
- TS grammar requires JS grammar at the same semver family; pin JS and TS together (currently JS v0.23.1 + TS v0.23.2).

## Language onboarding checklist

1. Add enum value in `rotide.h` if introducing a new runtime syntax language.
2. Wire parser in `syntax.c` (`tree_sitter_<lang>()` dispatch).
3. Wire query cache paths/fallback query strings.
4. Update detection (`editorSyntaxDetectLanguageFromFilenameAndFirstLine` and helpers).
5. Update build/vendor wiring when grammar runtime/parser/scanner files are needed.
6. Add/refresh fixtures under `tests/syntax/supported/<lang>/`.
7. Add behavior tests (activation + incremental + rendering).
8. Update README syntax-support documentation.

## Query-only change checklist

1. Update vendored query files (`vendor/tree_sitter/grammars/<lang>/queries/*.scm`) as needed.
2. Keep builtin fallback query strings aligned when fallback parity matters.
3. Confirm capture names still map to intended semantic classes.
4. Add/adjust render tests for representative tokens.

## Performance and degraded-mode checklist

1. Keep query/parse budget signaling graceful.
2. Avoid hard-disable except true failure paths (OOM/parser/query failure/length overflow).
3. Verify large-file behavior in tests and status messaging.
4. Confirm incremental edits still parse correctly after performance-mode transitions.

## Regression anchors

- activation/detection tests (`test_editor_syntax_activation_for_*`)
- incremental validity tests (`test_editor_syntax_incremental_edits_keep_*_tree_valid`)
- render highlight tests (`test_editor_refresh_screen_applies_syntax_highlighting_for_*`)
- degraded/budget tests
- tab isolation tests

Validation:
- `make`
- `make test`
- `make test-sanitize` (or LSAN-disabled variant if necessary)
