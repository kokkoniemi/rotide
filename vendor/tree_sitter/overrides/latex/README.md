# RotIDE LaTeX highlight grammar

This replaces the pinned upstream grammar before generation. RotIDE uses LaTeX trees only for
highlighting, so the override retains the nodes and fields consumed by
`src/language/queries/latex/highlights.scm` while using permissive text, command, group, and math
rules instead of the full document grammar.

The upstream source, metadata, and license still come from the ref pinned in
`vendor/tree_sitter/VERSIONS.env`. The refresh removes the upstream external scanner because the
highlight grammar has no external tokens. Run
`scripts/refresh_tree_sitter_vendor.sh --grammar latex` to apply this override and regenerate the
vendored artifacts with the pinned CLI.
