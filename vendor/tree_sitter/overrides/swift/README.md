# RotIDE Swift highlight grammar

This replaces the pinned `alex-pinkus/tree-sitter-swift` grammar before generation. RotIDE
consumes Swift trees only through the vendored highlight and injection queries, so the
override retains every node, field, keyword, operator, literal, and string/regex internal
those queries target while omitting machinery that only matters for a faithful Swift AST.
On the fixture suite the capture stream is byte-for-byte identical to upstream.

Known degradations, all rare or token-level: nested block comments end at the first
`*/`, raw strings are single-line without `#`-count balancing, multiline regex literals
are not modeled, attribute arguments are a shallow token soup, accessor-level attributes
are unsupported, and statements split across a newline at a point where the first line is
already a complete statement parse as two statements (the capture classes still match).

The pinned upstream source still provides queries, metadata, and licensing. Run
`scripts/refresh_tree_sitter_vendor.sh --grammar swift` to regenerate with the pinned CLI.
