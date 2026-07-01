# RotIDE Ruby highlight grammar

This replaces the pinned Ruby grammar before generation. RotIDE consumes Ruby trees through the
highlight and locals queries, so the override retains their nodes, fields, scopes, operators,
strings, interpolation, parameters, calls, assignments, and incomplete-input recovery while
omitting the full language AST.

The pinned upstream source still provides queries, metadata, and licensing. The override has no
external tokens, so refresh removes the upstream scanner. Run
`scripts/refresh_tree_sitter_vendor.sh --grammar ruby` to regenerate with the pinned CLI.
