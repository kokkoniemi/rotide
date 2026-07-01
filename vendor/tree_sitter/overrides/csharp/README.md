# RotIDE C# highlight grammar

This replaces the pinned upstream grammar before generation. RotIDE uses C# trees only for
highlighting, so the override retains the nodes and fields consumed by the vendored highlight
query while collapsing the full language grammar into permissive declaration, expression, type,
literal, and recovery rules.

The upstream source, queries, metadata, and license still come from the ref pinned in
`vendor/tree_sitter/VERSIONS.env`. The refresh removes the upstream external scanner because this
grammar has no external tokens. Run
`scripts/refresh_tree_sitter_vendor.sh --grammar csharp` to regenerate the vendored artifacts with
the pinned CLI.
