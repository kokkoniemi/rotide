# RotIDE Scala highlight grammar

This replaces the pinned upstream grammar before generation. RotIDE consumes Scala trees through
the highlight and locals queries, so the override retains their nodes, fields, indentation scopes,
and incomplete-string recovery while collapsing the full language grammar into permissive
declaration, expression, type, and literal rules.

The pinned upstream source still provides queries, metadata, license, and the indentation scanner.
Run `scripts/refresh_tree_sitter_vendor.sh --grammar scala` to regenerate the vendored artifacts
with the pinned CLI.
