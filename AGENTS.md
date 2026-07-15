# AGENTS.md instructions for RotIDE

## Project

- `rotide` is a terminal text editor that began with kilo-style minimalism.
- Priorities: deterministic behavior, readable control flow, and strong regression coverage.
- Preserve user-visible behavior unless the task explicitly changes it.
- Prefer repo-local skills over broad repo walks. Read the chosen `SKILL.md`
  first; use [`task-routing.md`](skills/rotide-maintainer/references/task-routing.md)
  for first-file guidance.

## Working Rule

Be lazy like a senior engineer: efficient, not careless. First read the task and
trace the real flow; then stop at the first rung that holds:

1. Does this need to be built at all?
2. Does this already exist in this codebase?
3. Does the standard library, platform, or an installed dependency cover it?
4. Can this be one line?
5. Only then, write the minimum code that works.

Deletion over addition; boring over clever; fewest files possible. No unneeded
abstractions, dependencies, or boilerplate. Question complex requests when a
simpler shape covers the need. If two existing approaches are the same size,
choose the edge-case-correct one.

Bug fixes address root cause: grep callers of touched functions and prefer one
shared fix. Never shortcut understanding, trust-boundary validation, data-loss
prevention, security, accessibility, real hardware/platform behavior, or
explicitly requested work.

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

Prefer clear code over comments. Add short local comments only for non-obvious
invariants, edge cases, correctness/performance constraints, surprising behavior,
temporary compatibility shims, or intentional shortcuts with a known ceiling and
upgrade path. Explain why, not what; omit comments that restate code, narrate
control flow, reference task history, or document obvious details.

## Code Style

- K&R; hard tabs (8 columns); same-line braces; pointer asterisks attach to the
  name (`char *name`); ≤100 columns preferred, 120 hard limit.
- Public header-declared names use `editorXxx`; file-local `.c` names use the
  module prefix from [`tools/module-prefixes.tsv`](tools/module-prefixes.tsv).
  The `editor` prefix is public-only. Do not rename `main`.
- File-local globals: `g_<module>_xxx`; guards: `ROTIDE_<SUBSYS>_<FILE>_H`;
  macros/constants: `UPPER_SNAKE_CASE`, with `ROTIDE_*`/`EDITOR_*` scopes.
- Out-parameters end in `_out`; borrowed/writable/owned accessors use the
  `View` / `Bytes` / `Dup` family.
- Use cleanup-only `goto` labels: `cleanup`, `done`, `err`, or `out`.
- CI enforces format, clang-tidy, and module-prefix checks; new `.c` files under
  `src/` must update the module-prefix table.

## Validation

- Always run `make` and `make test`.
- Run `ASAN_OPTIONS=detect_leaks=0 make test-sanitize` for document/storage/
  history/save/recovery/syntax/LSP/build-sensitive work.
- Non-trivial logic leaves one runnable check behind: the smallest test or
  self-check that fails if the logic breaks. Trivial one-liners need no test.
- Treat warnings as blockers; `-Werror` is enabled.

## Test API contract

[`tests/editor_test_api.h`](tests/editor_test_api.h) and similar test-only
headers may expose read-only views/counters, but not mutators production code
would not call. Test real code paths or add the missing production path.

## Skill Routing

Default to `rotide-maintainer`; use the narrower document, syntax,
function-refactor, or docs maintainer skill when the task matches. Everything
else (input systems, terminal panes, LSP, DAP, search, drawer/tabs, config,
rendering) stays with `rotide-maintainer`. Open referenced playbooks only when
first inspected files are not enough.
