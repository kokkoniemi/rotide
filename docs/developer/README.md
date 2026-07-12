# RotIDE Developer Documentation

For maintainers changing behavior.

## Table of Contents

- [Architecture](architecture.md): containers, ownership, design rules.
- [Workflows](workflows.md): sequenced runtime paths.
- [Concurrency](concurrency.md): the syntax worker protocol.
- [Debugging (DAP)](debugging.md): adapter boundary, launch lifecycle,
  breakpoints, console, and configuration.
- [Vim input](input-systems.md): dispatch boundaries, modal state, terminal
  input, and keymap configuration.
- [Error handling](error_handling.md): OOM and validation policy.
- [Build and tests](build-and-tests.md): make targets, runner flags,
  fuzz/bench/golden commands, metrics, sanitizers, diagrams.
- [Testing](testing.md): test model, validation layers, fuzzing, golden
  snapshots, metrics, and how to add coverage.
- [Metrics dashboard](metrics-dashboard.md): live SVG trend charts for the
  test suite, microbenches, fuzz targets, and lines of code (test/bench/fuzz
  series update nightly; the lines-of-code series updates per push to `main`).

## Diagrams

PlantUML sources live in [`../diagrams/src/`](../diagrams/src/); committed
SVGs in [`../diagrams/svg/`](../diagrams/svg/). Regenerate with:

```bash
make docs-diagrams
```

Requires a local `plantuml`. Diagrams use the PlantUML C4 stdlib.
