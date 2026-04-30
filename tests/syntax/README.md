# Syntax Test Fixtures

`tests/syntax/` stores sample files used by RotIDE's syntax tests.

- `supported/` contains real fixtures for syntaxes the editor supports today.
- `planned/` is reserved scaffolding for future language onboarding; it is
  currently empty.

Current fixture-to-editor mapping (`enum editorSyntaxLanguage` in
[`src/rotide.h`](../../src/rotide.h)):

- `supported/bash/` maps to `EDITOR_SYNTAX_SHELL`
- `supported/c/` maps to `EDITOR_SYNTAX_C`
- `supported/cpp/` maps to `EDITOR_SYNTAX_CPP`
- `supported/csharp/` maps to `EDITOR_SYNTAX_CSHARP`
- `supported/css/` maps to `EDITOR_SYNTAX_CSS`
- `supported/ejs/` maps to `EDITOR_SYNTAX_EJS`
- `supported/erb/` maps to `EDITOR_SYNTAX_ERB`
- `supported/go/` maps to `EDITOR_SYNTAX_GO`
- `supported/haskell/` maps to `EDITOR_SYNTAX_HASKELL`
- `supported/html/` maps to `EDITOR_SYNTAX_HTML`
- `supported/java/` maps to `EDITOR_SYNTAX_JAVA`
- `supported/javascript/` maps to `EDITOR_SYNTAX_JAVASCRIPT`
- `supported/json/` maps to `EDITOR_SYNTAX_JSON`
- `supported/julia/` maps to `EDITOR_SYNTAX_JULIA`
- `supported/ocaml/` maps to `EDITOR_SYNTAX_OCAML`
- `supported/php/` maps to `EDITOR_SYNTAX_PHP`
- `supported/python/` maps to `EDITOR_SYNTAX_PYTHON`
- `supported/regex/` maps to `EDITOR_SYNTAX_REGEX`
- `supported/ruby/` maps to `EDITOR_SYNTAX_RUBY`
- `supported/rust/` maps to `EDITOR_SYNTAX_RUST`
- `supported/scala/` maps to `EDITOR_SYNTAX_SCALA`
- `supported/typescript/` maps to `EDITOR_SYNTAX_TYPESCRIPT`
- `supported/tsx/` maps to `EDITOR_SYNTAX_TSX`

Notes:

- SCSS coverage lives under `supported/css/`; `.sass` is intentionally out of
  scope.
- `EDITOR_SYNTAX_JSDOC` has no fixture directory of its own: it is parser-backed
  doc-comment highlighting via tree-sitter-jsdoc, overlaid on JS/TS host trees,
  not a standalone file detection mode. JSDoc coverage lives under
  `supported/javascript/`, `supported/typescript/`, and `supported/tsx/`.
- Languages with injection coverage in fixtures (`injections.*`):
  - HTML — nested `<script>` JavaScript and `<style>` CSS.
  - JavaScript / TypeScript / TSX — tagged-template `html` / `css` / regex
    literals and JSDoc doc-comment overlay.
  - PHP — interleaved HTML text plus heredoc bodies tagged with a language
    label (`<<<HTML`, `<<<JS`, ...).
  - C++ — raw string literals tagged with a language delimiter
    (`R"html(...)html"`).
  - Haskell — QuasiQuotes (`hamlet`/`lucius`/`julius`/`tsc`/`aesonQQ`/...).
  - Julia — regex (`r"..."`) and command (`` `...` ``) literals.
  - EJS — template `content` as HTML and `code` as JavaScript, with the
    injected HTML further injecting its own `<script>` JS and `<style>` CSS.
  - ERB — same shape as EJS, with `code` injected as Ruby.
- Tests resolve these fixtures from the startup repo root so they keep working
  even if a test temporarily changes the current working directory.
