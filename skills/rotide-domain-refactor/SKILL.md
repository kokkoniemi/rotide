---
name: rotide-domain-refactor
description: Refactor RotIDE by domain boundaries and ownership, extracting clearer modules, headers, and workflows without changing behavior or forcing heavyweight enterprise DDD patterns.
---

# Rotide Domain Refactor

Use this when the main task is code ownership cleanup: splitting oversized modules, narrowing headers, or moving behavior to a clearer home.

## First Inspect

1. Read `AGENTS.md`, the touched modules, and `references/domain-playbook.md`.
2. Name the domain and invariant boundary before moving code.
3. Check the matching domain test file before and after the extraction.

## Guardrails

- Keep `editorDocument` canonical for writable text.
- Keep `struct editorRow` rows derived, never authoritative.
- Prefer domain names over technical accident names.
- In C, “domain boundary” usually means:
  - file ownership
  - header ownership
  - state ownership
  - who is allowed to mutate what
- Avoid umbrella headers that re-export half the system.
- Avoid introducing abstraction layers that do not reduce real coupling.
- Refactors should reduce cognitive load for a human reader.
- New headers use `ROTIDE_<SUBSYS>_<FILE>_H` guards.
- New `.c` files need entries in `docs/module-prefixes.md` and
  `tools/module-prefixes.tsv`; file-local helpers use that prefix once the
  naming migration is accepted.
- Public header-declared APIs keep the `editorXxx` naming surface.

## References

- `references/domain-playbook.md`
