# LSP Playbook

## Primary touchpoints

- `src/language/lsp.c` / `src/language/lsp.h`
  - client lifecycle
  - initialize/shutdown
  - didOpen/didChange/didSave/didClose
  - definition request/response handling
  - startup failure reason reporting
- `src/input/dispatch.c`
  - `editorGoToDefinition()`
  - definition picker/jump flow
  - missing-`gopls` install prompt trigger
  - missing-`clangd` instruction-tab trigger
- `src/editing/buffer_core.c`
  - active edit -> `editorLspNotifyDidChangeActive(...)`
  - save/close notifications and tab lifecycle hooks
- `src/config/lsp_config.c`
  - `[lsp]` config loading and precedence
- `src/workspace/tabs.c` / `src/workspace/task.h`
  - task-log tabs for install actions and instruction flows

## Behavior baseline

- LSP-backed definition lookup currently supports:
  - Go via `gopls`
  - C/C++ via `clangd`
- Definition request requires:
  - supported source buffer
  - saved filename
  - matching server enabled in `[lsp]`
  - non-empty command for the active language server
- didChange should use incremental edits when possible and full text fallback where needed.
- Startup command-not-found failure can trigger server-specific follow-up UX.

## `gopls` install flow baseline

- Prompt text: `gopls not found. Install now?`
- On accept:
  - open/focus task-log tab
  - run configured install command
  - stream stdout/stderr into tab
- leave tab open
- show success/failure status
- Do not auto-retry definition request.

## `clangd` missing-command baseline

- Do not attempt automatic installation.
- Open/focus a read-only task-log tab with:
  - a short explanation that `clangd` was not found on `PATH`
  - the installation URL `https://clangd.llvm.org/installation`
  - a reminder that `[lsp].clangd_command` can point to a custom path
- Do not auto-retry definition request.

## Config baseline

- Defaults:
  - `gopls_command = "gopls"`
  - `clangd_command = "clangd"`
  - `gopls_enabled = true`
  - `clangd_enabled = true`
  - `gopls_install_command = "go install golang.org/x/tools/gopls@latest"`
- Load precedence:
  - defaults -> global -> project
- Legacy compatibility:
  - `enabled = true|false` remains accepted as a shorthand for toggling both servers
- `gopls_install_command` is global-only override:
  - project key is ignored, not treated as section-invalid

## Test checklist

- config precedence and invalid cases
- Go/C/C++ gating behavior
- definition request success/failure/multi-location picker
- missing-`gopls` decline/accept flows
- missing-`clangd` instruction-tab flow
- task-log output and final status behavior
- didChange/didSave/didClose sequencing

Validation:
- `make`
- `make test`
- `ASAN_OPTIONS=detect_leaks=0 make test-sanitize` when LSAN is flaky locally
