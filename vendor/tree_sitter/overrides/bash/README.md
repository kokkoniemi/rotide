# Bash highlight grammar override

This grammar replaces the pinned upstream Bash grammar with a highlight-oriented subset. It
preserves the named nodes consumed by RotIDE's Bash highlight query, incomplete-input recovery,
and the upstream external scanner's heredoc contract.

The upstream base ref is `TREE_SITTER_BASH_GRAMMAR_REF` in `vendor/tree_sitter/VERSIONS.env`.
Regenerate with `scripts/refresh_tree_sitter_vendor.sh --grammar bash`.
