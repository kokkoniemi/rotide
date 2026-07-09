---
name: rotide-docs-maintainer
description: Maintain RotIDE documentation quality and consistency across README, AGENTS, and skill/reference docs.
---

# Rotide Docs Maintainer

Use for `README.md`, `AGENTS.md`, and `skills/*` docs.

## First Inspect

1. Read `references/docs-playbook.md`.
2. Confirm behavior from source before changing wording.
3. Open `README.md` only when the task touches user-facing docs.

## Guardrails

- Do not describe unsupported syntax/LSP features as shipped.
- Keep architecture language aligned with canonical document-first model.
- Keep config semantics precise (global vs project override behavior).
- Prefer concrete file/module references over vague descriptions.
- Keep AGENTS.md code-style wording contract-shaped, not tutorial-shaped.
- Keep the module-prefix source of truth in `tools/module-prefixes.tsv`
  (`docs/module-prefixes.md` no longer exists).

## References

- `references/docs-playbook.md`
