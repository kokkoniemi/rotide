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

Notes:

- C, C++, and Go fixtures include single-file and cross-file definitions plus call/reference sites that are useful for both mock-based tests and manual `Ctrl-O` checks.
- HTML fixtures focus on `id` / `href="#id"` style references because that is the most practical current manual definition target for the HTML server path.
- HTML real-server behavior may vary by language-server version, so HTML fixtures are best-effort manual smoke assets in addition to RotIDE’s mock-based LSP tests.
