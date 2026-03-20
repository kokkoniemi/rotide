# Syntax Playbook

## Primary touchpoints

- `rotide.h`
  - `enum editorSyntaxLanguage` language list
  - syntax highlight class enums used by render/theme mapping
  - per-tab/per-editor syntax state fields (`syntax_language`, `syntax_state`)
- `syntax.c` / `syntax.h`
  - Tree-sitter language declarations (`tree_sitter_<lang>()`)
  - language-object dispatch (`editorSyntaxLanguageObject`)
  - query cache entries, query path arrays, and builtin fallback queries
  - capture-name -> semantic-class mapping (`editorSyntaxClassFromCaptureName`)
  - language detection helpers (`editorSyntaxDetectLanguageFromFilename*`)
  - injection language mapping (`editorSyntaxLanguageFromInjectionName`) when needed
- `buffer.c`
  - active syntax setup/reconfigure (`editorSyntaxReconfigureForFilename`, `editorSyntaxParseFullActive`)
  - incremental edit path (`editorSyntaxApplyIncrementalEditActive`)
  - visible-row syntax span cache invalidation/rebuild
- `output.c`
  - syntax span paint path (`editorDrawRenderSliceWithSyntax` / `editorDrawRenderSlice`)
  - precedence interactions between syntax, selection, and search highlights
- `Makefile`
  - Tree-sitter include paths
  - grammar parser/scanner compilation list in `TREE_SITTER_SRCS`
- `vendor/tree_sitter/*`
  - vendored runtime + grammar `src/` and `queries/` trees
  - pin metadata in `VERSIONS.env` and `VERSIONS.md`
- `scripts/refresh_tree_sitter_vendor.sh`
  - pinned grammar/runtime download + parser regeneration workflow
- `tests/rotide_tests.c`
  - syntax activation/detection, incremental edit validity, render highlighting, perf budget, and tab isolation tests

## Language onboarding checklist

1. Add a language enum value in `rotide.h` (`EDITOR_SYNTAX_<LANG>`).
2. Wire parser support in `syntax.c`:
   - declare `extern const TSLanguage *tree_sitter_<lang>(void);`
   - map enum -> parser in `editorSyntaxLanguageObject(...)`
3. Add query wiring in `syntax.c`:
   - add query cache entry global
   - add query-file path array(s)
   - add builtin highlight query string fallback (and optional locals/injections query fallbacks)
   - add language branch in query-cache ensure/lookup switches
4. Add filename (and optional shebang) detection in `editorSyntaxDetectLanguageFromFilename*`.
5. Update build plumbing in `Makefile`:
   - add `-Ivendor/tree_sitter/grammars/<lang>/src`
   - add parser/scanner `.c` files to `TREE_SITTER_SRCS`
6. Vendor grammar sources:
   - add `vendor/tree_sitter/grammars/<lang>/` files (`src`, `queries`, metadata)
   - if using the refresh flow, extend `scripts/refresh_tree_sitter_vendor.sh` and `vendor/tree_sitter/VERSIONS.env` + `VERSIONS.md`
7. Update user-facing language support docs in `README.md`.
8. Add/adjust tests in `tests/rotide_tests.c`.

## Query-only change checklist

Use this smaller path when you are not adding a new language:

1. Update vendored query files under `vendor/tree_sitter/grammars/<lang>/queries/`.
2. Update builtin fallback query strings in `syntax.c` when fallback behavior must match.
3. Validate capture names still map correctly via `editorSyntaxClassFromCaptureName`.
4. Add/update render tests for affected tokens.

## Injection/local-scope checklist

Use when adding embedded-language support or local-scope predicate behavior:

1. Add/update injection query paths/fallbacks.
2. Update `editorSyntaxLanguageFromInjectionName(...)` for new injection language aliases.
3. Validate included-range construction and incremental parse updates for host + injected trees.
4. Add tests that verify host/injection highlighting and no style leakage across boundaries.

## Regression test anchors

Mirror existing syntax test families when adding coverage:

- Activation/detection:
  - `test_editor_syntax_activation_for_*`
  - `test_editor_save_as_*_updates_syntax`
- Incremental validity:
  - `test_editor_syntax_incremental_edits_keep_*_tree_valid`
- Render highlighting:
  - `test_editor_refresh_screen_applies_syntax_highlighting_for_*`
  - priority tests where selection/search override syntax colors
- Budget/degraded behavior:
  - `test_editor_syntax_query_budget_match_limit_is_graceful`
  - `test_editor_syntax_parse_budget_is_graceful`
- State isolation:
  - `test_editor_tabs_keep_*_syntax_states`

## Validation baseline

- `make`
- `make test`
- `make test-sanitize`
- If LeakSanitizer is flaky locally: `ASAN_OPTIONS=detect_leaks=0 make test-sanitize`

## Common pitfalls

- Adding enum/detection but forgetting `Makefile` parser/scanner objects.
- Updating query files without updating builtin fallback query strings when fallback parity is expected.
- Breaking plain-text fallback (`EDITOR_SYNTAX_NONE`) for unsupported files.
- Accidentally sharing one syntax state across tabs/buffers.
- Changing syntax colors but forgetting precedence with selection/search overlays.
