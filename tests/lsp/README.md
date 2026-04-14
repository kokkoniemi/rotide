# LSP Test Fixtures

`tests/lsp/` stores fixtures shaped for go-to-definition and document-sync testing.

- These fixtures are separate from `tests/syntax/` on purpose.
- `tests/syntax/` is for syntax/highlighting behavior.
- `tests/lsp/` is for symbol layout, cross-file references, and manual LSP smoke tests.

Current fixture groups:

- `supported/c/`
- `supported/cpp/`
- `supported/go/`
- `supported/html/`
- `supported/css/`
- `supported/json/`
- `supported/javascript/`

Notes:

- C, C++, and Go fixtures include single-file and cross-file definitions plus call/reference sites that are useful for both mock-based tests and manual `Ctrl-O` checks.
- HTML fixtures focus on `id` / `href="#id"` style references because that is the most practical current manual definition target for the HTML server path.
- CSS fixtures use custom property / variable references for `Ctrl-O` and document-sync coverage, including a separate SCSS sample for language-id routing.
- JSON fixtures use repeated property names so mock definition tests can exercise filename-based routing without needing Tree-sitter JSON.
- JavaScript fixtures are currently aimed at ESLint diagnostics and fix-action coverage rather than go-to-definition.
- HTML real-server behavior may vary by language-server version, so HTML fixtures are best-effort manual smoke assets in addition to RotIDE’s mock-based LSP tests.
