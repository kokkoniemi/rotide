# RotIDE Kotlin highlight grammar

This replaces the pinned upstream grammar before generation. RotIDE consumes
Kotlin trees only for highlighting, so the override keeps the nodes and fields
`src/language/queries/kotlin/highlights.scm` consumes (identifier, comments,
number/float/character/string literals with content and escape sequences,
`user_type`, `function_declaration.name`, `class_declaration.name`,
`object_declaration.name`, plus every keyword/operator anonymous token) while
folding the rest of the language into permissive expression and declaration
rules.

The pinned upstream source still provides license and metadata. The refresh
removes the upstream external scanner because the highlight grammar has no
external tokens. Run `scripts/refresh_tree_sitter_vendor.sh --grammar kotlin`
to apply this override and regenerate the vendored artifacts with the pinned
CLI.
