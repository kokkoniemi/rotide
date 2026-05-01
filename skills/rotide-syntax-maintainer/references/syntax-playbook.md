# Syntax Playbook

## Primary touchpoints

- `src/rotide.h`
  - `enum editorSyntaxLanguage`
  - highlight class enums
  - tab/editor syntax state fields
- `src/language/syntax.h`
  - public syntax state API
  - parse/edit/capture structs
  - budget, query-unavailable, and limit-event consumers
- `src/language/syntax.c`
  - host parse, incremental edits, injection orchestration, performance budgets
  - capture-name to semantic-class mapping at paint time
- `src/language/languages.c` / `src/language/languages.h`
  - table-driven language registry (`struct editorSyntaxLanguageDef`)
  - parser factories (`tree_sitter_*`)
  - filename extension, basename, shebang, and injection-alias lookup
- `src/language/queries.c`
  - query cache + fallback query strings (highlights, locals, injections)
  - predicate/locals handling and injection pattern metadata
- `src/language/syntax_internal.h`
  - private structs shared by `syntax.c` and `queries.c`
  - injection limits, query budgets, parser/query progress helpers
- `src/editing/buffer_core.c`
  - syntax activation/reconfiguration
  - full and incremental parse entrypoints
  - visible-row syntax span cache invalidation/rebuild
- `src/render/screen.c`
  - syntax span paint path and overlay precedence
- `Makefile` + `vendor/tree_sitter/*` + refresh script when grammar assets change
- `tests/test_syntax.c` and `tests/test_render_terminal.c`
  - activation, incremental parse validity, render highlights, budgets, tab isolation
- `tests/syntax/`
  - fixture files under `supported/`; `planned/` is reserved scaffolding and currently empty

## Current module layout

- `syntax.c` owns parser state transitions: state creation/destruction, full parse, incremental `ts_tree_edit`, injection tree lifecycle, range/capture merging, and enqueueing observable limit events.
- `queries.c` owns all compiled-query state: embedded query materialization from `src/language/syntax_query_data.h`, per-language/per-kind query caches, capture-role population, predicates, locals-query support, and regex caches for `#match?`.
- `languages.c` owns the registry. Every runtime language is described by one `struct editorSyntaxLanguageDef` entry with parser factory, embedded query bundles, detection metadata, and optional injection aliases.
- `syntax_internal.h` is the private contract between `syntax.c` and `queries.c`; do not include it from editor, render, or test code unless a test-only hook genuinely needs module-private state.
- `scripts/queries_manifest.txt` is the source list for generated embedded query data. Keep its query symbols aligned with the `editor_query_*` parts referenced from `languages.c`.

## Currently supported syntax families

- C / C++
- Go
- Shell (bash language backend)
- HTML (nested `<script>` JavaScript and `<style>` CSS injections via the generic injection tree registry)
- JavaScript (`.js`, `.mjs`, `.cjs`, `.jsx`) — JSDoc doc comments remain JavaScript `(comment)` nodes in the host tree, then RotIDE parses each visible `/** ... */` comment with the vendored `tree-sitter-jsdoc` parser and overlays its `tag_name`/`type` captures.
- TypeScript (`.ts`, `.cts`, `.mts`) — grammar from tree-sitter-typescript `typescript/` sub-grammar; shared `common/` lives at `vendor/tree_sitter/grammars/typescript/common/`. Uses the same vendored `tree-sitter-jsdoc` doc-comment overlay as JavaScript.
- TSX (`.tsx`) — grammar from tree-sitter-typescript `tsx/` sub-grammar; shared `common/` lives at `vendor/tree_sitter/grammars/tsx/common/`. Its query bundle combines JavaScript/JSX and TypeScript-compatible captures so JSX nodes and TypeScript types highlight against the TSX parser.
- JSDoc — vendored from `tree-sitter/tree-sitter-jsdoc` as `vendor/tree_sitter/grammars/jsdoc/`; it is parser-backed comment highlighting for JS/TS, not a standalone file detection mode.
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
- Julia (`.jl`) — extern parser `tree_sitter_julia`; standard layout (no shared `common/`, no sub-grammars). Grammar root is `source_file`. Upstream `highlights.scm` uses many capture names (`@variable`, `@variable.member`, `@function.call`, `@function.macro`, `@type.builtin`, `@keyword.conditional`, etc.) that don't match rotide's prefix table; the builtin fallback query targets a recognized subset (`@comment`, `@string`, `@number`, `@constant`, `@function`, `@type`, `@keyword`, `@operator`, `@punctuation`).
- Scala (`.scala`, `.sc`) — extern parser `tree_sitter_scala`; standard layout. Grammar root is `compilation_unit`. Upstream `highlights.scm` uses unrecognized capture names (`@parameter`, `@namespace`, `@function.call`, `@method.call`, `@method`, `@constructor`, `@none`, `@type.definition`); the builtin fallback query targets a recognized subset using node-pattern matches.
- EJS (`.ejs`) and ERB (`.erb`) — extern parser `tree_sitter_embedded_template` (shared upstream grammar `tree-sitter-embedded-template`, vendored as `embedded_template/`; the same parser would back ETLUA when added). Grammar root is `template`. Parser-only — no `scanner.c`, so the Makefile only compiles `parser.c`. Upstream ships per-dialect injection queries (`injections-ejs.scm`, `injections-erb.scm`, `injections-etlua.scm`) plus a single `highlights.scm` that marks tag delimiters and `(comment_directive)`. RotIDE loads the EJS/ERB injection queries through the generic injection registry: template `content` renders as HTML, EJS `code` renders as JavaScript, ERB `code` renders as Ruby, and injected HTML can recursively inject `<script>` JavaScript and `<style>` CSS.
- Markdown (`.md`, `.markdown`) — vendored from `tree-sitter-grammars/tree-sitter-markdown` (note the org is `tree-sitter-grammars`, not `tree-sitter`). The repo ships two sub-grammars in one tree: `tree-sitter-markdown/` (block: headings, lists, fenced code blocks, paragraphs that emit `(inline)` leaves) and `tree-sitter-markdown-inline/` (inline: emphasis, strong, code spans, autolinks, etc.). Both are vendored separately (`grammars/markdown/`, `grammars/markdown_inline/`); each ships its own `parser.c` + `scanner.c`. The repo-root `common/` is grammar.js dependency only and is not vendored. Upstream highlights queries use `@text.*` capture names that don't map to RotIDE's prefix table, so RotIDE ships rotide-curated `highlights.scm` for both at `src/language/queries/markdown/` and `src/language/queries/markdown_inline/`. The block grammar's `injections.scm` (vendored, used as-is) routes fenced code blocks via the dynamic `(language) @injection.language` pattern and overlays the inline grammar onto every `(inline)` node via static `#set! injection.language "markdown_inline"`. `EDITOR_SYNTAX_MARKDOWN_INLINE` is injection-only — no extension/basename detection, only registry/`injection_aliases` resolution. **Important**: `editorSyntaxInjectionWorkAppendRangeExcludingChildren` walks **named** children only — anonymous syntax tokens (e.g. the literal backticks the block grammar lays inside an `(inline)` node) must remain inside the injected content so the inline parser can see its delimiters. Walking all children fragments the included range and breaks injection grammars whose tokens depend on adjacent syntactic context.
- Regex (`.regex`)

## Per-grammar `common/` convention

Some upstream grammar repos (tree-sitter-typescript, tree-sitter-php) ship a shared `common/scanner.h` that their `src/scanner.c` includes as `"../../common/scanner.h"`. Different grammars' commons are not compatible, so each one is vendored **inside its own grammar directory** at `vendor/tree_sitter/grammars/<lang>/common/`, and the refresh script patches the `src/scanner.c` include from `../../common/scanner.h` down to `../common/scanner.h`. When vendoring a new grammar that uses this pattern, follow the same shape — do not reintroduce a shared `vendor/tree_sitter/grammars/common/` dir.

## TypeScript vendoring notes

- Both `typescript/` and `tsx/` sub-grammars are vendored separately so `.ts`/`.cts`/`.mts` use `tree_sitter_typescript()` and `.tsx` uses `tree_sitter_tsx()`.
- `grammar.js` requires `../common/define-grammar` → `tree-sitter-javascript` dep; the refresh script links JS grammar source via `link_grammar_dep` before regenerating.
- Each TypeScript-family sub-grammar has its own `common/` copy under the vendored grammar directory. `scanner.c` includes `"../common/scanner.h"` after the refresh script patches upstream's `../../common/` include.
- TS grammar requires JS grammar at the same semver family; pin JS and TS together (currently JS v0.23.1 + TS v0.23.2).

## Language onboarding checklist

1. Add enum value in `rotide.h` if introducing a new runtime syntax language.
2. Add or refresh grammar runtime files under `vendor/tree_sitter/grammars/<lang>/`, including parser/scanner/common wiring in the `Makefile` when needed.
3. Add query files to `scripts/queries_manifest.txt`; use repo-local fallback queries only when upstream captures do not map cleanly to RotIDE highlight classes.
4. Add one `struct editorSyntaxLanguageDef` entry in `src/language/languages.c` with `.id`, `.name`, `.ts_factory`, query parts, and detection metadata.
5. Add extensions, basenames, shebang matcher, and `injection_aliases` arrays in `languages.c` only where the language actually supports those entry points.
6. If the language has injections or locals, set `.injection_parts` / `.locals_parts`; capability checks read those registry fields.
7. Add/refresh fixtures under `tests/syntax/supported/<lang>/`.
8. Add behavior tests: activation, incremental parse validity, rendering, and registry consistency when metadata changes.
9. Update README syntax-support documentation.

## Query-only change checklist

1. Update vendored query files (`vendor/tree_sitter/grammars/<lang>/queries/*.scm`) as needed.
2. Keep builtin fallback query strings aligned when fallback parity matters.
3. Confirm capture names still map to intended semantic classes.
4. Add/adjust render tests for representative tokens.

## Injection notes

- Generic injections are stored as tab-local injected parse trees in `editorSyntaxState`; do not reintroduce one-off language fields for new static injections.
- Supported predicates / settings: `@injection.content`, static `#set! injection.language`, dynamic `@injection.language` capture (resolved per-match), `#set! injection.combined`, `#set! injection.include-children`, and `#offset!` adjustments for content/language captures.
- Host languages with built-in injection queries today: HTML, JavaScript, TypeScript, TSX, PHP, C++, Haskell, Julia, EJS, ERB. The set is derived from registry entries whose `.injection_parts` field is non-NULL.
- Injection target names resolve through `editorSyntaxLookupLanguageByInjectionName`: first the registry `.name`, then case-insensitive entries in `.injection_aliases`.
- Injection recursion is intentionally bounded: `ROTIDE_SYNTAX_MAX_INJECTION_DEPTH` is 3 and `ROTIDE_SYNTAX_MAX_INJECTION_TREES` is 16. When the next level or slot would be needed, the syntax state queues `EDITOR_SYNTAX_LIMIT_EVENT_INJECTION_DEPTH_EXCEEDED` or `EDITOR_SYNTAX_LIMIT_EVENT_INJECTION_SLOTS_FULL` and drops only that deeper/extra injection.
- Unsupported injection target languages should be skipped without disabling the host tree or reporting noisy status.

## Event channel notes

- Budget events are consumed with `editorSyntaxStateConsumeBudgetEvents`.
- Query availability failures are consumed with `editorSyntaxStateConsumeQueryUnavailableEvent` and carry the language plus highlight/injection query kind.
- Limit events are consumed with `editorSyntaxStateConsumeLimitEvent`; current event kinds cover capture truncation, injection depth, injection slot exhaustion, parse failure, and parse trees containing errors.
- Event producers should rate-limit at the syntax-state level when a condition can repeat every frame or row. Preserve rendering fallback where possible: host captures should still paint when a nested injection is skipped.

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
