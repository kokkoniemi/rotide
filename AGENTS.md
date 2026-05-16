# AGENTS.md instructions for /home/mk/Development/rotide

## Project

- `rotide` is a terminal text editor inspired by kilo.
- Priorities: deterministic behavior, readable control flow, and strong regression coverage.
- Preserve user-visible behavior unless the task explicitly changes it.
- Prefer repo-local skills over broad repo walks; use [`task-routing.md`](skills/rotide-maintainer/references/task-routing.md) for first-file guidance.

## Non-Negotiables

- `editorDocument` is the canonical writable text state.
- `struct erow` stays derived render/cache state only.
- `row->chars` stays NUL-terminated for derived rows.
- Text mutations update `E.dirty`; navigation/search/view changes do not.
- Key behavior routes through `enum editorAction` and keymap paths.
- Syntax and LSP state stay tab-local.
- Task-log tabs stay generated, read-only, and non-savable.
- Do not revert unrelated local changes.
- The buffer refactor below is sequenced. Work the first unchecked phase; do not bundle phases or pre-implement later phases.

## Comment policy

Prefer clear code over comments. Add comments only when they explain something non-obvious and important: invariants, edge cases, correctness/performance constraints, surprising behavior, or temporary compatibility shims.

Keep comments short and local. Explain why, not what.

Do not add comments that:
- restate the code
- narrate control flow
- reference plan phases, checklists, migration steps, tickets, or task history
- document obvious implementation details

When in doubt, omit the comment.

## Buffer Refactor (In Progress)

Text storage is being migrated from the flat `editorRope` chunk array to a SumTree-of-pieces. The phase checklist in [BUFFER_REFACTOR_PLAN.md](BUFFER_REFACTOR_PLAN.md) is the single source of truth for status — the first unchecked box there is the active phase. Audit: [BUFFER_AUDIT.md](BUFFER_AUDIT.md). Skill: `rotide-buffer-refactor`.

Gate at every phase boundary: `make`, `make test`, `ASAN_OPTIONS=detect_leaks=0 make test-sanitize`. Property tests in [tests/test_text_invariants.c](tests/test_text_invariants.c) must stay green; only modify that harness during Phase 0.

## Validation

- Always run `make` and `make test`.
- Run `ASAN_OPTIONS=detect_leaks=0 make test-sanitize` for document/storage/history/save/recovery/syntax/LSP/build-sensitive work.
- Treat warnings as blockers; `-Werror` is enabled.

## Test API contract

The test API in [`tests/editor_test_api.h`](tests/editor_test_api.h) and
similar test-only headers may expose read-only views of internal state
and counters. It must not provide mutators that production code would
not itself call. Adding a mutator means the test is asserting an
arrangement that production cannot reach. Write the test against a real
code path instead, or add the missing production path.

## Skill Routing

- Default: `rotide-maintainer`
- Buffer storage refactor phases (rope → SumTree-of-pieces, per BUFFER_REFACTOR_PLAN.md): `rotide-buffer-refactor`
- Document, rope, edit history, recovery normalization (work *not* part of the buffer refactor): `rotide-document-maintainer`
- Search prompt, active match, search highlight flow: `rotide-search-maintainer`
- Tree-sitter activation, queries, incremental parse, highlighting: `rotide-syntax-maintainer`
- LSP lifecycle, sync, definition, install/task-log UX: `rotide-lsp-maintainer`
- Terminal panes, DAP lifecycle/control UX, and pane layout: `rotide-maintainer`
- Module/file ownership refactors: `rotide-domain-refactor`
- README, AGENTS, skill/reference docs: `rotide-docs-maintainer`

Read the chosen `SKILL.md` first. Open the referenced playbook only if the first inspected files are not enough.
