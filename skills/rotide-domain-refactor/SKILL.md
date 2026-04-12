---
name: rotide-domain-refactor
description: Refactor RotIDE by domain boundaries and ownership, extracting clearer modules, headers, and workflows without changing behavior or forcing heavyweight enterprise DDD patterns.
---

# Rotide Domain Refactor

Use this skill when work is primarily about reorganizing code by domain:
- splitting oversized modules by responsibility
- moving files/functions to clearer ownership boundaries
- narrowing headers and reducing cross-domain leakage
- identifying dead weight during refactors
- planning incremental extractions that preserve behavior

Do not use this skill to force generic layered architecture or heavy DDD terminology onto simple C code. The goal is readable ownership and stable behavior.

## Workflow

1. Read `AGENTS.md`, the touched modules, and `references/domain-playbook.md`.
2. Name the domain being changed and the invariants it owns.
3. List what currently leaks across that boundary:
   - functions
   - structs/state
   - headers
   - tests
4. Choose the smallest extraction that gives the domain a clearer home.
5. Prefer one owner per responsibility:
   - one module implements it
   - one header exposes the minimal API
   - helper internals stay private unless another domain truly needs them
6. Remove duplicated or dead code as part of the extraction, not as a vague later cleanup.
7. Preserve behavior first; redesign only when the user explicitly asks for it.
8. Validate with `make` and `make test`. Add `make test-sanitize` for storage/recovery/history/build refactors.

## Guardrails

- Keep `editorDocument` canonical for writable text.
- Keep `struct erow` rows derived, never authoritative.
- Prefer domain names over technical accident names.
- In C, “domain boundary” usually means:
  - file ownership
  - header ownership
  - state ownership
  - who is allowed to mutate what
- Avoid umbrella headers that re-export half the system.
- Avoid introducing abstraction layers that do not reduce real coupling.
- Refactors should reduce cognitive load for a human reader.

## Domain Refactor Outputs

A good domain refactor should leave behind:
- a named domain with obvious ownership
- smaller public headers
- fewer unrelated includes
- fewer cross-module helper leaks
- unchanged behavior covered by existing or adjusted tests

## References

- `references/domain-playbook.md`
