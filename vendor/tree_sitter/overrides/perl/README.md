# RotIDE Perl highlight grammar

This replaces the pinned `tree-sitter-perl/tree-sitter-perl` grammar before generation.
RotIDE consumes Perl trees only through the vendored highlight and injection queries, so
the override retains every node, field, keyword, sigil, operator, and quote-like internal
those queries target while omitting the machinery that only matters for a faithful Perl
AST. On the fixture suite the capture stream matches upstream except for a handful of
token-shape nuances inside quote-like operators (see below).

Known degradations, all rare or token-level: exotic quote delimiters (`q#…#`, `s,,,`)
degrade to plain tokens or errors; regex interiors are flat (upstream paints brackets
and escapes inside patterns); `qq{}`/`qx{}` bodies and heredoc bodies do not highlight
interpolated variables (heredoc and `s///e` bodies still highlight via injection);
`s{}{}`-style substitutions support braces only; legacy prototypes parse through the
lenient signature rule; formats and given/when are not modeled; the `s`/`tr` operator
capture spans the opening delimiter too (`s/` instead of `s`).

The pinned upstream source still provides queries, metadata, and licensing. Run
`scripts/refresh_tree_sitter_vendor.sh --grammar perl` to regenerate with the pinned CLI
(the refresh replaces `grammar.js`, installs this `scanner.c`, and drops the upstream
`tsp_*.h`/`bsearch.h` scanner headers).
