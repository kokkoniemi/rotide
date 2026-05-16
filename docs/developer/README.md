# RotIDE Developer Documentation

For maintainers changing behavior. The root README is user-facing.

## Start here

- [Architecture](architecture.md): containers, ownership, design rules.
- [Workflows](workflows.md): sequenced runtime paths.
- [Concurrency](concurrency.md): the syntax worker protocol.
- [Error handling](error_handling.md): OOM and validation policy.
- [Build and tests](build-and-tests.md): targets, sanitizers, diagrams.
- [Testing](testing.md): the test pipeline: what we run, how it's
  validated, how to reproduce a failure.

## Diagrams

PlantUML sources live in [`../diagrams/src/`](../diagrams/src/); committed
SVGs in [`../diagrams/svg/`](../diagrams/svg/). Regenerate with:

```bash
make docs-diagrams
```

Requires a local `plantuml`. Diagrams use the PlantUML C4 stdlib.

## House rules for these docs

- Describe responsibilities, boundaries, and data flow, not file lists
  or function names.
- Diagrams stay at architectural level; no per-file participants.
- Document shipped behavior only.
- If a detail won't help a maintainer change the code safely, leave it
  out.
