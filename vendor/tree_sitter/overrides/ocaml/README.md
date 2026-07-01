# RotIDE OCaml highlight grammar

This replaces the pinned OCaml subgrammar before generation. RotIDE consumes OCaml trees through
the highlight and locals queries, so the override retains their nodes, fields, scopes, operators,
string recovery, and module/class structure while omitting the full language AST.

The pinned upstream source still provides queries, metadata, and licensing. The override has no
external tokens, so refresh removes the upstream scanner and shared scanner support. Run
`scripts/refresh_tree_sitter_vendor.sh --grammar ocaml` to regenerate with the pinned CLI.
