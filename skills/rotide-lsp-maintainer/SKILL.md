---
name: rotide-lsp-maintainer
description: Maintain RotIDE Go/C/C++/HTML LSP behavior including lifecycle, sync, definition flow, config gating, and missing-server task-log UX.
---

# Rotide LSP Maintainer

## Scope

Use for changes in:
- `src/language/lsp.c` / `src/language/lsp.h`
- `src/input/dispatch.c` definition UX, picker flow, and missing-server prompts/instructions
- `src/editing/buffer_core.c` LSP didChange/didSave/didClose integration
- `src/config/lsp_config.c` / `src/config/lsp_config.h` `[lsp]` config loading
- `src/workspace/tabs.c` / `src/workspace/task.h` task-log behavior tied to LSP install/actions

## Workflow

1. Read `references/lsp-playbook.md`.
2. Determine whether the change is config, transport, sync, definition UX, or install flow.
3. Preserve per-language gating and server-specific UX unless the change explicitly broadens scope.
4. Update tests in `tests/rotide_tests.c`.
5. Run `make` and `make test`.
6. Run sanitizer suite for transport/parsing/memory-sensitive changes.

## Guardrails

- Do not break Go, C/C++, or HTML gating behavior; unsupported languages should remain cleanly unavailable.
- Keep document sync source document-backed (`editorTextSource`/document path).
- Preserve startup failure reason handling and status messages.
- Missing-`gopls` behavior:
  - prompt only when command-not-found is the startup failure reason
  - run configured install command in task-log tab
  - do not auto-retry original definition request
- Missing-`clangd` behavior:
  - do not auto-install
  - show installation instructions in a read-only task-log tab
  - point users at `https://clangd.llvm.org/installation`
  - mention `[lsp].clangd_command` for custom install paths
- Missing-`vscode-langservers-extracted` behavior:
  - prompt only when command-not-found is the startup failure reason
  - run configured install command in task-log tab
  - do not auto-retry original definition request
- Keep generated task-log tabs read-only/non-savable.

## References

- `references/lsp-playbook.md`
