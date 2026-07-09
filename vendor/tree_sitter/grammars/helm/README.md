# tree-sitter-helm (self-authored)

This grammar is **not vendored from an upstream repository**. It is a minimal
Go template (Helm) grammar authored in-repo for RotIDE so the editor does not
depend on an unmaintained third-party grammar.

- Source of truth: `grammar.js` in this directory.
- `src/parser.c` is generated; regenerate with the pinned Tree-sitter CLI via
  `scripts/refresh_tree_sitter_vendor.sh --grammar helm` (regenerates in place;
  there is no upstream tarball to download and no `VERSIONS.env` ref).
- Highlight and YAML-injection queries live at `src/language/queries/helm/`.

The grammar parses a chart as a flat sequence of literal `text` and
`{{ ... }}` `action` nodes and tokenizes the action body loosely (enough for
highlighting, not a full Go-template expression tree). This keeps it
LR(1)-clean with no external scanner, so only `parser.c` ships.
