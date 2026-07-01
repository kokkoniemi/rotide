# RotIDE Julia highlight grammar

This replaces the pinned Julia grammar before generation. RotIDE consumes Julia trees through
the highlight, locals, and injection queries, so the override retains their nodes, fields,
scopes, string/command content, interpolation, definitions, calls, assignments, and
incomplete-input recovery while omitting the full language AST.

The pinned upstream source still provides queries, metadata, and licensing. The override has no
external tokens, so refresh removes the upstream scanner. Run
`scripts/refresh_tree_sitter_vendor.sh --grammar julia` to regenerate with the pinned CLI.
