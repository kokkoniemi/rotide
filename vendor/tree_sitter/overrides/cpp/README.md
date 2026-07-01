# RotIDE C++ highlight grammar

This standalone grammar replaces the pinned upstream C++ grammar before generation. It keeps
the nodes, fields, and anonymous tokens consumed by RotIDE's combined C/C++ highlight query and
the raw-string injection query while collapsing the full C/C++ AST.

The upstream raw-string scanner remains because matching its arbitrary opening and closing
delimiter preserves exact injection ranges. The upstream source, queries, metadata, scanner, and
license still come from the ref pinned in `vendor/tree_sitter/VERSIONS.env`. Run
`scripts/refresh_tree_sitter_vendor.sh --grammar cpp` to regenerate the vendored artifacts.
