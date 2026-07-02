# PHP highlight grammar override

This highlight-oriented grammar preserves RotIDE's PHP highlight and mixed HTML/heredoc injection
contracts. It retains the pinned upstream scanner for dynamic heredoc and nowdoc delimiters.

The upstream base ref is `TREE_SITTER_PHP_GRAMMAR_REF` in `vendor/tree_sitter/VERSIONS.env`.
Regenerate with `scripts/refresh_tree_sitter_vendor.sh --grammar php`.
