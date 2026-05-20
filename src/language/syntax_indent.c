/* Indent-anchor suggestion for syntax-aware indentation.
 *
 * Given a row/column and the parsed Tree-sitter tree of the host
 * language, return the row the editor should align fresh indentation to
 * and how many additional levels to add. Used by the auto-indent action
 * when the language registers indent rules.
 */
#include "language/languages.h"
#include "language/syntax_internal.h"
#include "tree_sitter/api.h"

#include <stddef.h>
#include <string.h>

static int editorSyntaxTypeEqualsAny(const char *type, const char *const *types, size_t count) {
	if (type == NULL || types == NULL) {
		return 0;
	}
	for (size_t i = 0; i < count; i++) {
		if (strcmp(type, types[i]) == 0) {
			return 1;
		}
	}
	return 0;
}

static int editorSyntaxNodeStartsIndentScope(enum editorSyntaxLanguage language, const char *type) {
	static const char *const brace_scope_types[] = {"block",
	                                                "compound_statement",
	                                                "statement_block",
	                                                "declaration_list",
	                                                "field_declaration_list",
	                                                "enum_body",
	                                                "class_body",
	                                                "interface_body",
	                                                "namespace_body",
	                                                "object",
	                                                "array"};
	static const char *const python_scope_types[] = {
	        "if_statement",    "for_statement", "while_statement",     "with_statement",
	        "try_statement",   "except_clause", "function_definition", "class_definition",
	        "match_statement", "case_clause"};

	switch (language) {
		case EDITOR_SYNTAX_C:
		case EDITOR_SYNTAX_CPP:
		case EDITOR_SYNTAX_GO:
		case EDITOR_SYNTAX_JAVASCRIPT:
		case EDITOR_SYNTAX_TYPESCRIPT:
		case EDITOR_SYNTAX_TSX:
		case EDITOR_SYNTAX_CSS:
		case EDITOR_SYNTAX_JSON:
		case EDITOR_SYNTAX_PHP:
		case EDITOR_SYNTAX_RUST:
		case EDITOR_SYNTAX_JAVA:
		case EDITOR_SYNTAX_CSHARP:
		case EDITOR_SYNTAX_JULIA:
		case EDITOR_SYNTAX_SCALA:
			return editorSyntaxTypeEqualsAny(type, brace_scope_types,
			                                 sizeof(brace_scope_types) /
			                                         sizeof(brace_scope_types[0]));
		case EDITOR_SYNTAX_PYTHON:
			return editorSyntaxTypeEqualsAny(type, brace_scope_types,
			                                 sizeof(brace_scope_types) /
			                                         sizeof(brace_scope_types[0])) ||
			       editorSyntaxTypeEqualsAny(type, python_scope_types,
			                                 sizeof(python_scope_types) /
			                                         sizeof(python_scope_types[0]));
		default:
			return 0;
	}
}

static int editorSyntaxLanguageUsesBraceIndentScopes(enum editorSyntaxLanguage language) {
	switch (language) {
		case EDITOR_SYNTAX_C:
		case EDITOR_SYNTAX_CPP:
		case EDITOR_SYNTAX_GO:
		case EDITOR_SYNTAX_JAVASCRIPT:
		case EDITOR_SYNTAX_TYPESCRIPT:
		case EDITOR_SYNTAX_TSX:
		case EDITOR_SYNTAX_CSS:
		case EDITOR_SYNTAX_JSON:
		case EDITOR_SYNTAX_PHP:
		case EDITOR_SYNTAX_RUST:
		case EDITOR_SYNTAX_JAVA:
		case EDITOR_SYNTAX_CSHARP:
		case EDITOR_SYNTAX_JULIA:
		case EDITOR_SYNTAX_SCALA:
			return 1;
		default:
			return 0;
	}
}

static int editorSyntaxIndentAnchorRowForScope(enum editorSyntaxLanguage language, TSNode scope) {
	TSPoint start = ts_node_start_point(scope);
	int anchor_row = (int)start.row;
	if (!editorSyntaxLanguageUsesBraceIndentScopes(language)) {
		return anchor_row;
	}

	TSNode parent = ts_node_parent(scope);
	if (ts_node_is_null(parent) || ts_node_is_null(ts_node_parent(parent))) {
		return anchor_row;
	}

	TSPoint parent_start = ts_node_start_point(parent);
	const char *parent_type = ts_node_type(parent);
	if (parent_start.row < start.row &&
	    !editorSyntaxNodeStartsIndentScope(language, parent_type)) {
		return (int)parent_start.row;
	}
	return anchor_row;
}

int editorSyntaxStateSuggestIndentAnchor(const struct editorSyntaxState *state, int row, int column,
                                         int *anchor_row_out, int *extra_levels_out) {
	if (anchor_row_out != NULL) {
		*anchor_row_out = 0;
	}
	if (extra_levels_out != NULL) {
		*extra_levels_out = 0;
	}
	if (state == NULL || state->host.tree == NULL || row < 0 || column < 0) {
		return 0;
	}

	TSPoint point = {.row = (uint32_t)row, .column = (uint32_t)column};
	TSNode root = ts_tree_root_node(state->host.tree);
	TSNode node = ts_node_descendant_for_point_range(root, point, point);
	if (ts_node_is_null(node)) {
		node = root;
	}

	while (!ts_node_is_null(node)) {
		const char *type = ts_node_type(node);
		TSPoint start = ts_node_start_point(node);
		TSPoint end = ts_node_end_point(node);
		if (editorSyntaxNodeStartsIndentScope(state->language, type) &&
		    start.row <= point.row && point.row < end.row) {
			if (anchor_row_out != NULL) {
				*anchor_row_out =
				        editorSyntaxIndentAnchorRowForScope(state->language, node);
			}
			if (extra_levels_out != NULL) {
				*extra_levels_out = 1;
			}
			return 1;
		}
		node = ts_node_parent(node);
	}

	return 0;
}
