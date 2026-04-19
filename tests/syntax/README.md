# Syntax Test Fixtures

`tests/syntax/` stores sample files used by RotIDE's syntax tests.

- `supported/` contains real fixtures for syntaxes the editor supports today.
- `planned/` contains placeholder directories for future language onboarding only.

Current fixture-to-editor mapping:

- `supported/bash/` maps to the current shell syntax support (`EDITOR_SYNTAX_SHELL`)
- `supported/c/` maps to `EDITOR_SYNTAX_C`
- `supported/cpp/` maps to `EDITOR_SYNTAX_CPP`
- `supported/go/` maps to `EDITOR_SYNTAX_GO`
- `supported/html/` maps to `EDITOR_SYNTAX_HTML`
- `supported/javascript/` maps to `EDITOR_SYNTAX_JAVASCRIPT`
- `supported/css/` maps to `EDITOR_SYNTAX_CSS`
- `supported/json/` maps to `EDITOR_SYNTAX_JSON`
- `supported/typescript/` maps to `EDITOR_SYNTAX_TYPESCRIPT`
- `supported/python/` maps to `EDITOR_SYNTAX_PYTHON`

Notes:

- `planned/*` is scaffolding only and does not imply runtime syntax support yet.
- SCSS coverage lives under `supported/css/`; `.sass` is intentionally out of scope.
- Tests resolve these fixtures from the startup repo root so they keep working even if a test temporarily changes the current working directory.

Planned placeholder directories:

- `csharp`
- `erb`
- `ejs`
- `haskell`
- `java`
- `jsdoc`
- `julia`
- `ocaml`
- `php`
- `regex`
- `ruby`
- `rust`
- `scala`
