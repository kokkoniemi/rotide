# AGENTS.md instructions for /home/mk/Development/rotide

## Project

- `rotide` is a terminal text editor that began with kilo-style minimalism.
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
- Naming uses a public/static boundary:
  - Public (header-declared) functions, structs, enums, typedefs: `editorXxx`.
  - File-local (`static` / .c-only) functions, structs, enums, typedefs:
    `<module>Xxx`, where `<module>` is the lowercase camelCase slug listed
    in [`docs/module-prefixes.md`](docs/module-prefixes.md).
  - The prefix `editor` is reserved for the public surface; no `.c` file
    may declare it as its module prefix.
  - Permitted naming exceptions (do not rename): `main`.
- File-local globals use `g_<module>_xxx` snake_case.
- Header guards use `ROTIDE_<SUBSYS>_<FILE>_H`.
- Macros and constants use `UPPER_SNAKE_CASE`. Project-wide constants use
  `ROTIDE_*`; subsystem-public feature macros use `EDITOR_*`.
- Out-parameters use the `_out` suffix.
- Borrowed views / writable byte spans / owned copies use the `View` / `Bytes`
  / `Dup` accessor family.
- `make format` and `make format-check` use the repository `.clang-format`;
  CI enforces `format-check`.
- `make lint` runs clang-tidy in CI. Clang-tidy diagnostics remain warnings
  unless `.clang-tidy` promotes them with `WarningsAsErrors`.
- `make lint-prefixes` enforces module-prefix table completeness; new `.c`
  files under `src/` must add an entry. Static-name drift is reported as
  advisory output and fails the target only when `LINT_PREFIXES_STRICT=1`.
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
- Input systems (CUA/Vim interface, registry, modal Vim, `[input]`/`[keymap.vim]`): `rotide-maintainer` (see [`docs/developer/input-systems.md`](docs/developer/input-systems.md); a dedicated input-systems skill is not yet warranted)
- Terminal panes, DAP lifecycle/control UX, and pane layout: `rotide-maintainer`
- Module/file ownership refactors: `rotide-domain-refactor`
- README, AGENTS, skill/reference docs: `rotide-docs-maintainer`

Read the chosen `SKILL.md` first. Open the referenced playbook only if the first inspected files are not enough.
