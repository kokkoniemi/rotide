---
name: rotide-lsp-maintainer
description: Maintain RotIDE Go/C/C++/HTML LSP behavior including lifecycle, sync, definition flow, config gating, and missing-server task-log UX.
---

# Rotide LSP Maintainer

Use for LSP lifecycle, sync, definition flow, config gating, and install/task-log behavior.

## First Inspect

1. Read `references/lsp-playbook.md`.
2. Inspect `src/language/lsp.c` and the immediate caller/config module involved.
3. Check `tests/test_lsp.c`.

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
