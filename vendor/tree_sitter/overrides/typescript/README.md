# TypeScript/TSX highlight grammar override

This shared override preserves RotIDE's TypeScript and TSX highlight, locals, tagged-template,
regex, JSX, and JSDoc contracts while inheriting the pinned JavaScript grammar.

The upstream base ref is `TREE_SITTER_TYPESCRIPT_GRAMMAR_REF` in
`vendor/tree_sitter/VERSIONS.env`. Regenerate both parsers with
`scripts/refresh_tree_sitter_vendor.sh --grammar typescript`.
