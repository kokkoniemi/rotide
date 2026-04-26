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
  - missing-`vscode-langservers-extracted` install prompt trigger
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
  - HTML via `~/.local/bin/vscode-html-language-server --stdio` by default
  - CSS/SCSS via `~/.local/bin/vscode-css-language-server --stdio` by default
  - JSON via `~/.local/bin/vscode-json-language-server --stdio` by default
  - JavaScript/JSX via `~/.local/bin/typescript-language-server --stdio` by default
- ESLint diagnostics for active JavaScript buffers (`.js`, `.mjs`, `.cjs`, `.jsx`) via `~/.local/bin/vscode-eslint-language-server --stdio`, plus the `eslint_fix` code-action path.
- Server kinds enumerated in `src/language/lsp_internal.h` (`EDITOR_LSP_SERVER_GOPLS`, `_CLANGD`, `_HTML`, `_CSS`, `_JSON`, `_JAVASCRIPT`, `_ESLINT`).
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
  - a note that `clangd` usually needs `compile_commands.json` for C/C++ projects
  - setup options for both:
    - CMake: `cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`
    - Bear: `bear -- make` or `bear -- <build command>`
  - mention that Bear is often a good fit for pure C projects
  - a reminder that `[lsp].clangd_command` can point to a custom path
- Do not auto-retry definition request.

## `vscode-langservers-extracted` install flow baseline

- Prompt text: `vscode-langservers-extracted not found. Install now?`
- On accept:
  - open/focus task-log tab
  - run configured install command
  - stream stdout/stderr into tab
- leave tab open
- show success/failure status
- Do not auto-retry definition request.

## Config baseline

- Defaults:
  - `gopls_command = "gopls"`
  - `clangd_command = "clangd"`
  - `html_command = "~/.local/bin/vscode-html-language-server --stdio"`
  - `css_command = "~/.local/bin/vscode-css-language-server --stdio"`
  - `json_command = "~/.local/bin/vscode-json-language-server --stdio"`
  - `javascript_command = "~/.local/bin/typescript-language-server --stdio"`
  - `eslint_command = "~/.local/bin/vscode-eslint-language-server --stdio"`
  - `gopls_enabled = true`, `clangd_enabled = true`, `html_enabled = true`, `css_enabled = true`, `json_enabled = true`, `javascript_enabled = true`, `eslint_enabled = true`
  - `gopls_install_command = "go install golang.org/x/tools/gopls@latest"`
  - `vscode_langservers_install_command = "npm install --global --prefix ~/.local vscode-langservers-extracted"`
  - `javascript_install_command = "npm install --global --prefix ~/.local typescript typescript-language-server"`
- Load precedence:
  - defaults -> global -> project
- Legacy compatibility:
  - `enabled = true|false` remains accepted as a shorthand for toggling both servers
- `gopls_install_command` is global-only override:
  - project key is ignored, not treated as section-invalid
- `vscode_langservers_install_command` is global-only override:
  - project key is ignored, not treated as section-invalid

## Test checklist

- config precedence and invalid cases
- Go/C/C++/HTML gating behavior
- definition request success/failure/multi-location picker
- missing-`gopls` decline/accept flows
- missing-`clangd` instruction-tab flow
- missing-`vscode-langservers-extracted` decline/accept flows
- task-log output and final status behavior
- didChange/didSave/didClose sequencing

Validation:
- `make`
- `make test`
- `ASAN_OPTIONS=detect_leaks=0 make test-sanitize` when LSAN is flaky locally
