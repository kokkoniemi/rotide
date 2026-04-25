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
- PHP (`.php`, `.phtml`, `.php3`–`.php8`, `.phps`, plus extensionless shebang detection for `php` / `php8`); uses the `php/` sub-grammar (HTML-mixed variant), not `php_only/`.
- Rust (`.rs`)
- Java (`.java`)
- C# (`.cs`, `.csx`) — grammar name `c_sharp`; extern parser is `tree_sitter_c_sharp`
- Haskell (`.hs`, `.lhs`) — extern parser `tree_sitter_haskell`; grammar ships an indentation-aware external scanner plus a vendored `unicode.h` next to `parser.c`/`scanner.c`
- Ruby (`.rb`, `.rake`, `.gemspec`, `.ru`, plus `Rakefile`/`Gemfile`/`Guardfile`/`Capfile`/`Vagrantfile`, extensionless shebang detection for `ruby`) — extern parser `tree_sitter_ruby`; grammar ships an external scanner alongside `parser.c`/`scanner.c`
- OCaml (`.ml`) — extern parser `tree_sitter_ocaml`; upstream ships sub-grammars under `grammars/<name>/` (`ocaml`, `interface`, `type`); only `ocaml/` is vendored. Shared `common/scanner.h` lives at the repo root and `src/scanner.c` includes it as `"../../../common/scanner.h"`; the refresh script vendors the shared `common/` next to `src/` and patches the include down to `"../common/scanner.h"`. Top-level `queries/` is staged into `grammars/ocaml/queries/` before sync so highlights ship with the grammar.
- Regex (`.regex`)

## Per-grammar `common/` convention

Some upstream grammar repos (tree-sitter-typescript, tree-sitter-php) ship a shared `common/scanner.h` that their `src/scanner.c` includes as `"../../common/scanner.h"`. Different grammars' commons are not compatible, so each one is vendored **inside its own grammar directory** at `vendor/tree_sitter/grammars/<lang>/common/`, and the refresh script patches the `src/scanner.c` include from `../../common/scanner.h` down to `../common/scanner.h`. When vendoring a new grammar that uses this pattern, follow the same shape — do not reintroduce a shared `vendor/tree_sitter/grammars/common/` dir.

## TypeScript vendoring notes

- The repo has `typescript/` and `tsx/` sub-grammars; only `typescript/` is vendored (handles both `.ts` and `.tsx`).
- `grammar.js` requires `../common/define-grammar` → `tree-sitter-javascript` dep; the refresh script links JS grammar source via `link_grammar_dep` before regenerating.
- `scanner.c` includes `"../common/scanner.h"` (patched from upstream's `../../common/`); the shared `common/` from the TS repo is vendored to `vendor/tree_sitter/grammars/typescript/common/`.
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
