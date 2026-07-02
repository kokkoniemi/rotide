# Rust highlight grammar override

This grammar replaces the pinned upstream Rust grammar with a highlight-oriented subset. It
preserves the named nodes and fields consumed by RotIDE's Rust highlight query, incomplete-input
recovery, and the upstream scanner's raw-string and nested-comment behavior.

The upstream base ref is `TREE_SITTER_RUST_GRAMMAR_REF` in `vendor/tree_sitter/VERSIONS.env`.
Regenerate with `scripts/refresh_tree_sitter_vendor.sh --grammar rust`.
