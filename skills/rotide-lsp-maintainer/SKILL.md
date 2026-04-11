---
name: rotide-lsp-maintainer
description: Maintain RotIDE Go LSP behavior including lifecycle, sync, definition flow, and missing-gopls install/task-log integration.
---

# Rotide LSP Maintainer

## Scope

Use for changes in:
- `lsp.c` / `lsp.h`
- `input.c` Go definition UX and install prompt flow
- `buffer.c` LSP didChange/didSave/didClose integration
- `keymap.c` `[lsp]` config loading
- task-log command output behavior tied to LSP install actions

## Workflow

1. Read `references/lsp-playbook.md`.
2. Determine whether the change is config, transport, sync, definition UX, or install flow.
3. Keep Go-only gating semantics unless explicitly expanding language scope.
4. Update tests in `tests/rotide_tests.c`.
5. Run `make` and `make test`.
6. Run sanitizer suite for transport/parsing/memory-sensitive changes.

## Guardrails

- Do not break non-Go behavior (should remain cleanly disabled/unavailable).
- Keep document sync source document-backed (`editorTextSource`/document path).
- Preserve startup failure reason handling and status messages.
- Missing-`gopls` behavior:
  - prompt only when command-not-found is the startup failure reason
  - run configured install command in task-log tab
  - do not auto-retry original definition request
- Keep generated task-log tabs read-only/non-savable.

## References

- `references/lsp-playbook.md`
