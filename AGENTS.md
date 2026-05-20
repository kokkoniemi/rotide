# AGENTS.md instructions for /home/mk/Development/rotide

## Project

- `rotide` is a terminal text editor inspired by kilo.
- Priorities: deterministic behavior, readable control flow, and strong regression coverage.
- Preserve user-visible behavior unless the task explicitly changes it.
- Prefer repo-local skills over broad repo walks; use [`task-routing.md`](skills/rotide-maintainer/references/task-routing.md) for first-file guidance.

## Non-Negotiables

- `editorDocument` is the canonical writable text state.
- `struct editorRow` stays derived render/cache state only — no raw byte storage.
  Read raw line bytes via `editorDocumentLineView` / `editorDocumentLineBytes`
  / `editorDocumentLineDup`.
- Text mutations update `E.dirty`; navigation/search/view changes do not.
- Key behavior routes through `enum editorAction` and keymap paths.
- Syntax and LSP state stay tab-local.
- Task-log tabs stay generated, read-only, and non-savable.
- Do not revert unrelated local changes.

## Comment policy

Prefer clear code over comments. Add comments only when they explain something non-obvious and important: invariants, edge cases, correctness/performance constraints, surprising behavior, or temporary compatibility shims.

Keep comments short and local. Explain why, not what.

Do not add comments that:
- restate the code
- narrate control flow
- reference plan phases, checklists, migration steps, tickets, or task history
- document obvious implementation details

When in doubt, omit the comment.

## Code Style

- Base style: K&R. Indent with hard tabs (8 columns). Put braces on the same line.
- Pointer asterisks attach to the name, for example `char *name`.
- Keep lines ≤100 columns where practical; 120 columns is the hard limit.
- Header guards use `ROTIDE_<SUBSYS>_<FILE>_H`.
- The proposed module-prefix table for file-local naming lives in
  [`docs/module-prefixes.md`](docs/module-prefixes.md). Existing static helpers
  may keep the current `editorXxx` convention until that migration is accepted;
  `make lint-prefixes` enforces table completeness and reports naming drift as
  advisory output.
- Macros and constants use `UPPER_SNAKE_CASE`.
- Out-parameters use the `_out` suffix.
- Borrowed views / writable byte spans / owned copies use the `View` / `Bytes`
  / `Dup` accessor family.
- `make format` and `make format-check` use the repository `.clang-format`;
  `format-check` becomes blocking only after the formatter baseline lands.
- `make lint` is advisory until the complexity refactors land. It measures
  function size, cognitive complexity, and nesting depth.
- Use `goto` only for cleanup-style exits, with labels named `cleanup`, `done`,
  `err`, or `out`.

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
- Document, edit history, recovery normalization: `rotide-document-maintainer`
- Search prompt, active match, search highlight flow: `rotide-search-maintainer`
- Tree-sitter activation, queries, incremental parse, highlighting: `rotide-syntax-maintainer`
- LSP lifecycle, sync, definition, install/task-log UX: `rotide-lsp-maintainer`
- Terminal panes, DAP lifecycle/control UX, and pane layout: `rotide-maintainer`
- Module/file ownership refactors: `rotide-domain-refactor`
- README, AGENTS, skill/reference docs: `rotide-docs-maintainer`

Read the chosen `SKILL.md` first. Open the referenced playbook only if the first inspected files are not enough.
