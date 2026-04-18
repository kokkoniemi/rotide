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

## Validation

- Always run `make` and `make test`.
- Run `make test-sanitize` for document/storage/history/save/recovery/syntax/LSP/build-sensitive work.
- If LeakSanitizer flakes locally, rerun with `ASAN_OPTIONS=detect_leaks=0 make test-sanitize` and mention that limitation.
- Treat warnings as blockers; `-Werror` is enabled.

## Skill Routing

- Default: `rotide-maintainer`
- Document, rope, edit history, recovery normalization: `rotide-document-maintainer`
- Search prompt, active match, search highlight flow: `rotide-search-maintainer`
- Tree-sitter activation, queries, incremental parse, highlighting: `rotide-syntax-maintainer`
- LSP lifecycle, sync, definition, install/task-log UX: `rotide-lsp-maintainer`
- Module/file ownership refactors: `rotide-domain-refactor`
- README, AGENTS, skill/reference docs: `rotide-docs-maintainer`

Read the chosen `SKILL.md` first. Open the referenced playbook only if the first inspected files are not enough.
