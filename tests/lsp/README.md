# LSP Fixtures

Fixtures for definition lookup, document sync, and manual `Ctrl-O` smoke checks.
Keep them separate from `tests/syntax/`, which is only for parser/highlighting
coverage.

- `single_file_definition.*` covers same-file targets.
- `cross_file/` covers references that must resolve across files.
- JavaScript fixtures also cover ESLint diagnostics/fix actions.
- HTML/CSS/JSON samples use server-friendly targets such as `id` references,
  custom properties, and repeated keys; real server behavior can vary.

Add small deterministic files only when they back an automated test or a named
manual smoke path.
