# LSP Playbook

## Primary touchpoints

- `lsp.c` / `lsp.h`
  - client lifecycle
  - initialize/shutdown
  - didOpen/didChange/didSave/didClose
  - definition request/response handling
  - startup failure reason reporting
- `input.c`
  - `editorGoToDefinition()`
  - definition picker/jump flow
  - missing-`gopls` install prompt trigger
- `buffer.c`
  - active edit -> `editorLspNotifyDidChangeActive(...)`
  - save/close notifications and tab lifecycle hooks
- `keymap.c`
  - `[lsp]` config loading and precedence

## Behavior baseline

- LSP features are currently Go-only.
- Definition request requires:
  - Go syntax buffer
  - saved filename
  - `[lsp].enabled = true`
  - non-empty `gopls_command`
- didChange should use incremental edits when possible and full text fallback where needed.
- Startup command-not-found failure can trigger install prompt flow.

## `gopls` install flow baseline

- Prompt text: `gopls not found. Install now?`
- On accept:
  - open/focus task-log tab
  - run configured install command
  - stream stdout/stderr into tab
  - leave tab open
  - show success/failure status
- Do not auto-retry definition request.

## Config baseline

- Defaults:
  - `enabled = true`
  - `gopls_command = "gopls"`
  - `gopls_install_command = "go install golang.org/x/tools/gopls@latest"`
- Load precedence:
  - defaults -> global -> project
- `gopls_install_command` is global-only override:
  - project key is ignored, not treated as section-invalid

## Test checklist

- config precedence and invalid cases
- Go-only gating behavior
- definition request success/failure/multi-location picker
- missing-`gopls` decline/accept flows
- task-log output and final status behavior
- didChange/didSave/didClose sequencing

Validation:
- `make`
- `make test`
- `make test-sanitize` for transport/parsing-sensitive changes
