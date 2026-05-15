/* Locals-query analysis for Tree-sitter syntax highlighting.
 *
 * Owns scope discovery (where a definition lives), local-vs-property
 * classification (driven by `@local.scope` / `@local.definition` /
 * `@local.reference` captures), the per-tree locals cache lifetime,
 * and the per-node text extraction helper used by both the locals
 * analysis pass and predicate evaluation.
 */
#include "language/syntax_internal.h"

#include "language/languages.h"

#include "tree_sitter/api.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int editorSyntaxNodeContains(TSNode outer, TSNode inner) {
	uint32_t outer_start = ts_node_start_byte(outer);
	uint32_t outer_end = ts_node_end_byte(outer);
	uint32_t inner_start = ts_node_start_byte(inner);
	uint32_t inner_end = ts_node_end_byte(inner);
	return outer_start <= inner_start && outer_end >= inner_end;
}

static uint32_t editorSyntaxNodeSpan(TSNode node) {
	uint32_t start = ts_node_start_byte(node);
	uint32_t end = ts_node_end_byte(node);
	if (end < start) {
		return 0;
	}
	return end - start;
}

static int editorSyntaxStateCopySourceRangeToScratch(struct editorSyntaxState *state,
		const struct editorTextSource *source,
		size_t start_byte,
		size_t end_byte,
		int scratch_idx,
		const char **text_out, size_t *len_out) {
	if (state == NULL || source == NULL || text_out == NULL || len_out == NULL ||
			end_byte < start_byte || end_byte > source->length) {
		return 0;
	}
	if (start_byte == end_byte) {
		*text_out = "";
		*len_out = 0;
		return 1;
	}

	char **scratch = scratch_idx == 0 ? &state->scratch_primary : &state->scratch_secondary;
	size_t *scratch_cap = scratch_idx == 0 ?
			&state->scratch_primary_cap : &state->scratch_secondary_cap;
	size_t len = end_byte - start_byte;
	if (*scratch_cap < len + 1) {
		char *grown = realloc(*scratch, len + 1);
		if (grown == NULL) {
			return 0;
		}
		*scratch = grown;
		*scratch_cap = len + 1;
	}
	if (!editorTextSourceCopyRange(source, start_byte, end_byte, *scratch)) {
		return 0;
	}
	(*scratch)[len] = '\0';
	*text_out = *scratch;
	*len_out = len;
	return 1;
}

int editorSyntaxNodeText(struct editorSyntaxState *state,
		const struct editorTextSource *source, TSNode node, int scratch_idx,
		const char **text_out, size_t *len_out) {
	if (source == NULL || text_out == NULL || len_out == NULL) {
		return 0;
	}

	uint32_t start = ts_node_start_byte(node);
	uint32_t end = ts_node_end_byte(node);
	if (end < start) {
		return 0;
	}
	return editorSyntaxStateCopySourceRangeToScratch(state, source, start, end, scratch_idx,
			text_out, len_out);
}

static int editorSyntaxNodeArrayAppend(TSNode **items, int *count, int *cap, TSNode node) {
	if (items == NULL || count == NULL || cap == NULL) {
		return 0;
	}
	if (*count >= *cap) {
		int new_cap = *cap == 0 ? 16 : *cap * 2;
		if (new_cap <= *cap) {
			return 0;
		}
		size_t bytes = (size_t)new_cap * sizeof(**items);
		TSNode *grown = realloc(*items, bytes);
		if (grown == NULL) {
			return 0;
		}
		*items = grown;
		*cap = new_cap;
	}
	(*items)[*count] = node;
	(*count)++;
	return 1;
}

void editorSyntaxLocalsContextInit(struct editorSyntaxLocalsContext *ctx) {
	if (ctx == NULL) {
		return;
	}
	ctx->marks = NULL;
	ctx->count = 0;
	ctx->cap = 0;
}

void editorSyntaxLocalsContextFree(struct editorSyntaxLocalsContext *ctx) {
	if (ctx == NULL) {
		return;
	}
	free(ctx->marks);
	ctx->marks = NULL;
	ctx->count = 0;
	ctx->cap = 0;
}

static int editorSyntaxLocalsContextMarkNode(struct editorSyntaxLocalsContext *ctx,
		TSNode node,
		int is_local) {
	if (ctx == NULL) {
		return 0;
	}
	for (int i = 0; i < ctx->count; i++) {
		if (ts_node_eq(ctx->marks[i].node, node)) {
			if (is_local) {
				ctx->marks[i].is_local = 1;
			}
			return 1;
		}
	}

	if (ctx->count >= ctx->cap) {
		int new_cap = ctx->cap == 0 ? 32 : ctx->cap * 2;
		if (new_cap <= ctx->cap) {
			return 0;
		}
		size_t bytes = (size_t)new_cap * sizeof(*ctx->marks);
		struct editorSyntaxLocalMark *grown = realloc(ctx->marks, bytes);
		if (grown == NULL) {
			return 0;
		}
		ctx->marks = grown;
		ctx->cap = new_cap;
	}

	ctx->marks[ctx->count].node = node;
	ctx->marks[ctx->count].is_local = is_local ? 1 : 0;
	ctx->count++;
	return 1;
}

int editorSyntaxLocalsContextNodeIsLocal(const struct editorSyntaxLocalsContext *ctx,
		TSNode node) {
	if (ctx == NULL) {
		return 0;
	}
	for (int i = 0; i < ctx->count; i++) {
		if (ts_node_eq(ctx->marks[i].node, node)) {
			return ctx->marks[i].is_local != 0;
		}
	}
	return 0;
}

static int editorSyntaxScopeAddDefinition(struct editorSyntaxScopeInfo *scope,
		const char *name,
		size_t name_len) {
	if (scope == NULL || name == NULL) {
		return 0;
	}

	for (int i = 0; i < scope->def_count; i++) {
		if (strlen(scope->definitions[i]) == name_len &&
				memcmp(scope->definitions[i], name, name_len) == 0) {
			return 1;
		}
	}

	if (scope->def_count >= scope->def_cap) {
		int new_cap = scope->def_cap == 0 ? 8 : scope->def_cap * 2;
		if (new_cap <= scope->def_cap) {
			return 0;
		}
		size_t bytes = (size_t)new_cap * sizeof(*scope->definitions);
		char **grown = realloc(scope->definitions, bytes);
		if (grown == NULL) {
			return 0;
		}
		scope->definitions = grown;
		scope->def_cap = new_cap;
	}

	char *dup = malloc(name_len + 1);
	if (dup == NULL) {
		return 0;
	}
	memcpy(dup, name, name_len);
	dup[name_len] = '\0';
	
	scope->definitions[scope->def_count] = dup;
	scope->def_count++;
	return 1;
}

static int editorSyntaxScopeHasDefinition(const struct editorSyntaxScopeInfo *scope,
		const char *name,
		size_t name_len) {
	if (scope == NULL || name == NULL) {
		return 0;
	}
	for (int i = 0; i < scope->def_count; i++) {
		if (strlen(scope->definitions[i]) == name_len &&
				memcmp(scope->definitions[i], name, name_len) == 0) {
			return 1;
		}
	}
	return 0;
}

static void editorSyntaxScopeInfoFree(struct editorSyntaxScopeInfo *scopes, int scope_count) {
	if (scopes == NULL) {
		return;
	}
	for (int i = 0; i < scope_count; i++) {
		for (int j = 0; j < scopes[i].def_count; j++) {
			free(scopes[i].definitions[j]);
		}
		free(scopes[i].definitions);
	}
	free(scopes);
}

static int editorSyntaxFindInnermostScope(const struct editorSyntaxScopeInfo *scopes,
		int scope_count,
		TSNode node) {
	int best_idx = -1;
	uint32_t best_span = UINT32_MAX;
	for (int i = 0; i < scope_count; i++) {
		if (!editorSyntaxNodeContains(scopes[i].node, node)) {
			continue;
		}
		uint32_t span = editorSyntaxNodeSpan(scopes[i].node);
		if (span < best_span) {
			best_span = span;
			best_idx = i;
		}
	}
	return best_idx;
}

int editorSyntaxBuildLocalsContext(const TSTree *tree,
		struct editorSyntaxState *state,
		enum editorSyntaxLanguage language,
		const struct editorTextSource *source,
		struct editorSyntaxLocalsContext *ctx_out) {
	if (ctx_out == NULL) {
		return 0;
	}
	editorSyntaxLocalsContextInit(ctx_out);
	if (tree == NULL || source == NULL) {
		return 1;
	}
	if (!editorSyntaxEnsureLocalsQuery(language)) {
		return 1;
	}

	const struct editorSyntaxQueryCacheEntry *cache =
			editorSyntaxLocalsQueryCacheForLanguage(language);
	if (cache == NULL || cache->query == NULL || cache->capture_roles == NULL) {
		return 1;
	}

	TSQueryCursor *cursor = ts_query_cursor_new();
	if (cursor == NULL) {
		return 0;
	}

	TSNode root = ts_tree_root_node(tree);
	ts_query_cursor_exec(cursor, cache->query, root);

	TSNode *scope_nodes = NULL;
	TSNode *definition_nodes = NULL;
	TSNode *reference_nodes = NULL;
	int scope_count = 0;
	int scope_cap = 0;
	int definition_count = 0;
	int definition_cap = 0;
	int reference_count = 0;
	int reference_cap = 0;

	TSQueryMatch match;
	while (ts_query_cursor_next_match(cursor, &match)) {
		for (uint16_t capture_idx = 0; capture_idx < match.capture_count; capture_idx++) {
			TSQueryCapture capture = match.captures[capture_idx];
			if (capture.index >= cache->capture_count) {
				continue;
			}
			uint8_t role = cache->capture_roles[capture.index];
			if (role == EDITOR_SYNTAX_CAPTURE_ROLE_LOCAL_SCOPE) {
				if (!editorSyntaxNodeArrayAppend(&scope_nodes, &scope_count, &scope_cap,
							capture.node)) {
					goto oom;
				}
			} else if (role == EDITOR_SYNTAX_CAPTURE_ROLE_LOCAL_DEFINITION) {
				if (!editorSyntaxNodeArrayAppend(&definition_nodes, &definition_count,
							&definition_cap, capture.node)) {
					goto oom;
				}
			} else if (role == EDITOR_SYNTAX_CAPTURE_ROLE_LOCAL_REFERENCE) {
				if (!editorSyntaxNodeArrayAppend(&reference_nodes, &reference_count,
							&reference_cap, capture.node)) {
					goto oom;
				}
			}
		}
	}

	if (!editorSyntaxNodeArrayAppend(&scope_nodes, &scope_count, &scope_cap, root)) {
		goto oom;
	}

	struct editorSyntaxScopeInfo *scopes = calloc((size_t)scope_count, sizeof(*scopes));
	if (scopes == NULL) {
		goto oom;
	}

	for (int i = 0; i < scope_count; i++) {
		scopes[i].node = scope_nodes[i];
		scopes[i].parent_idx = -1;
		scopes[i].definitions = NULL;
		scopes[i].def_count = 0;
		scopes[i].def_cap = 0;
	}

	for (int i = 0; i < scope_count; i++) {
		uint32_t span_i = editorSyntaxNodeSpan(scopes[i].node);
		uint32_t best_span = UINT32_MAX;
		int best_parent = -1;
		for (int j = 0; j < scope_count; j++) {
			if (i == j) {
				continue;
			}
			if (!editorSyntaxNodeContains(scopes[j].node, scopes[i].node)) {
				continue;
			}
			uint32_t span_j = editorSyntaxNodeSpan(scopes[j].node);
			if (span_j <= span_i) {
				continue;
			}
			if (span_j < best_span) {
				best_span = span_j;
				best_parent = j;
			}
		}
		scopes[i].parent_idx = best_parent;
	}

	for (int i = 0; i < definition_count; i++) {
		const char *text = NULL;
		size_t text_len = 0;
		if (!editorSyntaxNodeText(state, source, definition_nodes[i], 0, &text, &text_len) ||
				text_len == 0) {
			continue;
		}
		int scope_idx = editorSyntaxFindInnermostScope(scopes, scope_count, definition_nodes[i]);
		if (scope_idx < 0) {
			continue;
		}
		if (!editorSyntaxScopeAddDefinition(&scopes[scope_idx], text, text_len)) {
			editorSyntaxScopeInfoFree(scopes, scope_count);
			goto oom;
		}
		if (!editorSyntaxLocalsContextMarkNode(ctx_out, definition_nodes[i], 1)) {
			editorSyntaxScopeInfoFree(scopes, scope_count);
			goto oom;
		}
	}

	for (int i = 0; i < reference_count; i++) {
		const char *text = NULL;
		size_t text_len = 0;
		if (!editorSyntaxNodeText(state, source, reference_nodes[i], 0, &text, &text_len) ||
				text_len == 0) {
			continue;
		}
		int scope_idx = editorSyntaxFindInnermostScope(scopes, scope_count, reference_nodes[i]);
		if (scope_idx < 0) {
			continue;
		}

		int is_local = 0;
		int probe = scope_idx;
		while (probe >= 0) {
			if (editorSyntaxScopeHasDefinition(&scopes[probe], text, text_len)) {
				is_local = 1;
				break;
			}
			probe = scopes[probe].parent_idx;
		}
		if (!editorSyntaxLocalsContextMarkNode(ctx_out, reference_nodes[i], is_local)) {
			editorSyntaxScopeInfoFree(scopes, scope_count);
			goto oom;
		}
	}

	editorSyntaxScopeInfoFree(scopes, scope_count);
	ts_query_cursor_delete(cursor);
	free(scope_nodes);
	free(definition_nodes);
	free(reference_nodes);
	return 1;

oom:
	ts_query_cursor_delete(cursor);
	free(scope_nodes);
	free(definition_nodes);
	free(reference_nodes);
	editorSyntaxLocalsContextFree(ctx_out);
	return 0;
}

void editorSyntaxStateInvalidateLocalsCaches(struct editorSyntaxState *state) {
	if (state == NULL) {
		return;
	}
	state->host_locals_valid = 0;
	for (int i = 0; i < state->injection_count; i++) {
		state->injections[i].locals_valid = 0;
	}
}

int editorSyntaxStateEnsureLocalsCached(
		struct editorSyntaxState *state,
		const struct editorSyntaxParsedTree *parsed,
		const struct editorTextSource *source,
		enum editorSyntaxLanguage language,
		struct editorSyntaxInjectedTree *injection,
		const struct editorSyntaxLocalsContext **locals_out) {
	if (locals_out == NULL) {
		return 0;
	}
	*locals_out = NULL;

	if (state == NULL || parsed == NULL || parsed->tree == NULL || source == NULL) {
		return 1;
	}

	struct editorSyntaxLocalsContext *cache = injection != NULL ?
			&injection->locals : &state->host_locals;
	uint64_t *cache_revision = injection != NULL ?
			&injection->locals_revision : &state->host_locals_revision;
	int *cache_valid = injection != NULL ?
			&injection->locals_valid : &state->host_locals_valid;

	if (*cache_valid && *cache_revision == parsed->revision) {
		*locals_out = cache;
		return 1;
	}

	editorSyntaxLocalsContextFree(cache);
	editorSyntaxLocalsContextInit(cache);
	if (!editorSyntaxBuildLocalsContext(parsed->tree, state, language, source, cache)) {
		editorSyntaxLocalsContextFree(cache);
		*cache_valid = 0;
		return 0;
	}

	*cache_revision = parsed->revision;
	*cache_valid = 1;
	*locals_out = cache;
	return 1;
}

int editorSyntaxLanguageHasLocalsQuery(enum editorSyntaxLanguage language) {
	const struct editorSyntaxLanguageDef *def = editorSyntaxLookupLanguage(language);
	return def != NULL && def->locals_parts != NULL && def->locals_part_count > 0;
}
