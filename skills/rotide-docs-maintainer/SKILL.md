---
name: rotide-docs-maintainer
description: Maintain RotIDE documentation quality and consistency across README, AGENTS, and skill/reference docs.
---

# Rotide Docs Maintainer

## Scope

Use for:
- `README.md`
- `AGENTS.md`
- `skills/*/SKILL.md`
- `skills/*/references/*.md`

## Workflow

1. Read `references/docs-playbook.md`.
2. Confirm current behavior from source files before documenting.
3. Update docs with terminology-first, architecture-accurate wording.
4. Keep user docs and contributor docs consistent.
5. Ensure new/renamed skills are reflected in `AGENTS.md`.
6. Run `make` and `make test` before finalizing doc-only PRs in this repo.

## Guardrails

- Do not describe unsupported syntax/LSP features as shipped.
- Keep architecture language aligned with canonical document-first model.
- Keep config semantics precise (global vs project override behavior).
- Prefer concrete file/module references over vague descriptions.

## References

- `references/docs-playbook.md`
