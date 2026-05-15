# RotIDE Developer Documentation

This directory explains how RotIDE is built internally. The root README stays
user-facing; this tree is for maintainers changing behavior.

## Start Here

- [Architecture](architecture.md): state ownership, module boundaries, and the
  design choices that keep editing deterministic.
- [Workflows](workflows.md): the main runtime paths from input to rendering,
  edits, syntax, LSP, save/recovery, search, and task logs.
- [Concurrency](concurrency.md): the syntax background worker's
  snapshot/revision protocol — the one place RotIDE goes multi-threaded.
- [Error handling](error_handling.md): the OOM-status-bar contract and where
  validation belongs.
- [Build and tests](build-and-tests.md): local targets, sanitizer expectations,
  Tree-sitter vendor refresh, and diagram rendering.

## Diagrams

PlantUML sources live in [`../diagrams/src/`](../diagrams/src/). Generated SVGs
live in [`../diagrams/svg/`](../diagrams/svg/) and are committed so these docs
render in plain Markdown viewers.

Regenerate diagrams with:

```bash
make docs-diagrams
```

The renderer expects a local `plantuml` command. It uses PlantUML stdlib C4
includes such as `!include <C4/C4_Container>`.

## Documentation Rules

- Treat `editorDocument` as the canonical writable text state.
- Treat `struct erow` as derived render/cache state.
- Keep syntax and LSP state tab-local in descriptions.
- Keep task-log tabs described as generated, read-only, and non-savable.
- Document shipped behavior only.
