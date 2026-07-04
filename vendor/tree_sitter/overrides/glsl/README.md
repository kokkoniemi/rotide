# RotIDE GLSL highlight grammar

This replaces the pinned upstream grammar before generation. The upstream
`tree-sitter-grammars/tree-sitter-glsl` extends the full tree-sitter-c grammar,
so its parser ships ~160 k lines of C. RotIDE only uses GLSL trees for
highlighting, so this override keeps just the nodes and fields
`src/language/queries/glsl/highlights.scm` consumes (identifier, comment,
`preproc_directive`, string/system-lib-string literals, `null`/`number_literal`
/`char_literal`, `call_expression.function`, `field_expression.field`,
`field_identifier`, `function_declarator.declarator`,
`preproc_function_def.name`, `statement_identifier`, `type_identifier`,
`primitive_type`, `sized_type_specifier`, `extension_storage_class`) plus every
C / GLSL keyword and operator anonymous token the query lists. Everything else
folds into a permissive `_item` fallthrough.

The pinned upstream source still provides license and metadata. Run
`scripts/refresh_tree_sitter_vendor.sh --grammar glsl` to apply this override
and regenerate the vendored artifacts with the pinned CLI. Because this
grammar is self-contained, the refresh no longer needs to link
tree-sitter-c as a `node_modules` dependency.
