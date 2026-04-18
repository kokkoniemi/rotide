# RotIDE Task Routing

Use this as the default first-stop map before opening broad repo docs.

| Task type | Skill | First files | Matching tests | Validation |
| --- | --- | --- | --- | --- |
| General editor behavior or mixed bugfix | `rotide-maintainer` | touched module, nearest caller, nearest test file | domain test file below | `make`, `make test` |
| Document, rope, offset mapping, undo/redo capture | `rotide-document-maintainer` | `src/text/document.c`, `src/text/rope.c`, `src/editing/buffer_core.c` | `tests/test_document_text_editing.c` | `make`, `make test`, sanitizer if storage-sensitive |
| Save, recovery, startup restore | `rotide-document-maintainer` | `src/support/file_io.c`, `src/workspace/recovery.c`, save path in editing layer | `tests/test_save_recovery.c` | `make`, `make test`, `make test-sanitize` |
| Search prompt, active match, search highlight | `rotide-search-maintainer` | search caller, buffer search helpers, renderer highlight path | `tests/test_input_search.c`, `tests/test_render_terminal.c` | `make`, `make test` |
| Syntax activation, queries, incremental parse, highlight colors | `rotide-syntax-maintainer` | `src/language/syntax.c`, `src/editing/buffer_core.c`, `src/render/screen.c` | `tests/test_syntax.c`, `tests/test_render_terminal.c` | `make`, `make test`, `make test-sanitize` |
| LSP lifecycle, definition flow, install/task-log UX | `rotide-lsp-maintainer` | `src/language/lsp.c`, `src/input/dispatch.c`, `src/config/lsp_config.c` | `tests/test_lsp.c` | `make`, `make test`, `make test-sanitize` |
| Drawer, tabs, config loading, terminal/window sizing | `rotide-maintainer` | `src/workspace/*.c`, `src/config/*.c`, terminal/render caller | `tests/test_workspace_config.c`, `tests/test_input_search.c` | `make`, `make test` |
| Rendering-only output regressions | `rotide-maintainer` | `src/render/screen.c` and closest state producer | `tests/test_render_terminal.c` | `make`, `make test` |
| File/module ownership cleanup | `rotide-domain-refactor` | touched module plus nearest owner/caller | matching domain test file | `make`, `make test`, sanitizer when refactor touches sensitive paths |
| README/AGENTS/skill docs | `rotide-docs-maintainer` | touched docs, then source-of-truth modules | doc-adjacent tests only if behavior wording changes | `make`, `make test` |
