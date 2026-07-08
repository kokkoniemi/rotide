# RotIDE Swift highlight grammar

This replaces the pinned `alex-pinkus/tree-sitter-swift` grammar before generation. RotIDE
consumes Swift trees only through the vendored highlight and injection queries, so the
override retains every node, field, keyword, operator, literal, and string/regex internal
those queries target while omitting machinery that only matters for a faithful Swift AST.
On the fixture suite the capture stream is byte-for-byte identical to upstream.

The three size reductions versus upstream (~20 MB / 10321-state `parser.c` down to
~3 MB / 1949 states; the ~3.7 MB parser object drops to ~650 KB):

1. **No external scanner.** Upstream ships a stateful C scanner for nested block
   comments, `#`-balanced raw strings, and newline-as-implicit-semicolon lookahead plus a
   dozen semi-suppressing operator tokens. Here block comments are one non-nesting token,
   raw strings are single-line regex tokens (the closing `#` run is not balance-checked),
   and statement separation uses the tree-sitter-go trick: `extras` holds the
   single-character `/\s/` while `'\n'` is an explicit statement separator token, so a
   newline is shifted where a separator is expected and skipped as whitespace everywhere
   else. Because the override declares no externals, refresh removes `scanner.c`.

2. **A collapsed expression grammar.** All infix operator tiers fold into one flat
   `binary_expression` rule (with the ternary and `as`/`is` forms as alternatives of the
   same rule), and `try`/`await` ride as prefix operators. Only the postfix shapes the
   query matches — navigation, call, prefix — keep their structure.

3. **Funneled containers.** Call arguments, subscripts, tuples, array/dictionary
   literals, string interpolations, and the `#selector`-style bodies all route through one
   `value_argument` list; every `: type` annotation shares one rule; declaration
   signatures and type-declaration headers are loose element loops. Each distinct
   expression/type context otherwise costs hundreds of LR states.

Protocol bodies reuse the unified `class_body`/`function_declaration` shapes, so protocol
members paint through the same query patterns; `protocol_function_declaration` and
`protocol_property_declaration` remain defined behind impossible guard tokens purely so
the upstream highlight query still compiles.

Known degradations, all rare or token-level: nested block comments end at the first
`*/`, raw strings are single-line without `#`-count balancing, multiline regex literals
are not modeled, attribute arguments are a shallow token soup, accessor-level attributes
are unsupported, and statements split across a newline at a point where the first line is
already a complete statement parse as two statements (the capture classes still match).

The pinned upstream source still provides queries, metadata, and licensing. Run
`scripts/refresh_tree_sitter_vendor.sh --grammar swift` to regenerate with the pinned CLI.
