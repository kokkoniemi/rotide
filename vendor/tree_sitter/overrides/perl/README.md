# RotIDE Perl highlight grammar

This replaces the pinned `tree-sitter-perl/tree-sitter-perl` grammar before generation.
RotIDE consumes Perl trees only through the vendored highlight and injection queries, so
the override retains every node, field, keyword, sigil, operator, and quote-like internal
those queries target while omitting the machinery that only matters for a faithful Perl
AST. On the fixture suite the capture stream matches upstream except for a handful of
token-shape nuances inside quote-like operators (see below).

The reductions versus upstream (~29 MB / 5749-state `parser.c` down to ~5.9 MB / 1586
states; the ~4.7 MB parser object drops to ~930 KB):

1. **A minimal external scanner** (`scanner.c` here, ~230 lines) instead of upstream's
   54-token scanner with its generated intuit headers. Only heredocs and POD genuinely
   need cross-line state: the scanner tracks a queue of pending heredoc terminators
   (`heredoc_token`/`command_heredoc_token` record them; `_heredoc_start`, hidden
   per-line `_heredoc_text`, and `heredoc_end` deliver the body as the `heredoc_content`
   extra, with `heredoc_end` spanning exactly the terminator word so RotIDE's
   inject-by-terminator query keeps working), plus line-anchored POD through `=cut`.
   Everything else — quote-like operators, `$`/`@`/`%`/`&`/`*` sigils (upstream's
   `token(prec(2))` trick), `__DATA__` sections, autoquoting — is ordinary grammar.
   Perl statements are `;`-separated, so no newline machinery is needed at all.

2. **A collapsed expression grammar**: one flat `binary_expression` rule carries every
   infix operator in an `operator:` field, which is all the query reads
   (`(_ operator: _ @operator)`); the ternary tail is aliased to
   `conditional_expression`, and `isa` keeps its `relational_expression` node for the
   query's `right:` capture.

3. **Regex-token quote-likes**: `q qq qw qx m qr s tr y` support the common delimiters
   (`()`, `{}` with one nesting level, `[]`, `<>`, `//`, `!!`). Slash forms split into
   operator/content/closer tokens so only the interior paints as regex and the `s///e`
   replacement injects cleanly; paired-delimiter forms are single tokens.

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
