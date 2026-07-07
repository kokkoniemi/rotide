# RotIDE R highlight grammar

This replaces the pinned `r-lib/tree-sitter-r` grammar before generation. RotIDE consumes R
trees through the vendored highlight and locals queries, so the override retains the nodes,
fields, operators, literals, calls, subsets, parameters/arguments, namespace/extract
operators, and incomplete-input recovery those queries target while omitting machinery that
only matters for a faithful R AST.

The two size reductions versus upstream:

1. **No external scanner.** Upstream ships a 16-token external scanner that makes newlines
   and `;` statement separators, matches raw strings, and emits bracket tokens. None of that
   affects highlighting, so newlines/semicolons are plain whitespace, brackets are literals,
   and raw strings are a single-line regex token. Because the override declares no externals,
   refresh removes the upstream `scanner.c`.
2. **No newline-tracking states.** The operator precedence table is preserved (so real R
   still parses without spurious `ERROR` nodes), but the `repeat($._newline)` states upstream
   scatters through every rule are gone.

The grammar is intentionally more lenient than R (it parses freely across line breaks), which
only ever yields a more connected tree, never worse highlighting. Multi-line raw strings and
`r"---(...)---"` dash-padding degrade to identifier + string; both are rare.

The pinned upstream source still provides queries, metadata, and licensing. Run
`scripts/refresh_tree_sitter_vendor.sh --grammar r` to regenerate with the pinned CLI.
