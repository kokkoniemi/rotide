#include "language/syntax.h"

#include <ctype.h>
#include <limits.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <time.h>

#include "tree_sitter/api.h"

#include "language/languages.h"
#include "language/syntax_internal.h"

static int editorSyntaxTokenFromLine(const char *line, size_t line_len, size_t *idx,
		const char **token_out, size_t *token_len_out) {
	if (line == NULL || idx == NULL || token_out == NULL || token_len_out == NULL) {
		return 0;
	}

	size_t i = *idx;
	while (i < line_len && isspace((unsigned char)line[i])) {
		i++;
	}
	if (i >= line_len) {
		*idx = i;
		return 0;
	}

	size_t start = i;
	while (i < line_len && !isspace((unsigned char)line[i])) {
		i++;
	}

	*idx = i;
	*token_out = &line[start];
	*token_len_out = i - start;
	return 1;
}

static enum editorSyntaxLanguage editorSyntaxLanguageFromShebangBase(const char *base,
		size_t base_len) {
	const struct editorSyntaxLanguageDef *def =
			editorSyntaxLookupLanguageByShebangToken(base, base_len);
	return def != NULL ? def->id : EDITOR_SYNTAX_NONE;
}

static enum editorSyntaxLanguage editorSyntaxDetectLanguageFromShebang(const char *first_line) {
	if (first_line == NULL || first_line[0] != '#' || first_line[1] != '!') {
		return EDITOR_SYNTAX_NONE;
	}

	size_t line_len = strlen(first_line);
	size_t idx = 2;
	const char *token = NULL;
	size_t token_len = 0;
	if (!editorSyntaxTokenFromLine(first_line, line_len, &idx, &token, &token_len)) {
		return EDITOR_SYNTAX_NONE;
	}

	const char *base = token;
	for (size_t i = 0; i < token_len; i++) {
		if (token[i] == '/') {
			base = &token[i + 1];
		}
	}
	size_t base_len = token_len - (size_t)(base - token);

	enum editorSyntaxLanguage lang = editorSyntaxLanguageFromShebangBase(base, base_len);
	if (lang != EDITOR_SYNTAX_NONE) {
		return lang;
	}

	if (base_len == 3 && strncasecmp(base, "env", 3) == 0) {
		for (;;) {
			if (!editorSyntaxTokenFromLine(first_line, line_len, &idx, &token, &token_len)) {
				break;
			}
			if (token_len > 0 && token[0] == '-') {
				continue;
			}

			base = token;
			for (size_t i = 0; i < token_len; i++) {
				if (token[i] == '/') {
					base = &token[i + 1];
				}
			}
			base_len = token_len - (size_t)(base - token);
			lang = editorSyntaxLanguageFromShebangBase(base, base_len);
			if (lang != EDITOR_SYNTAX_NONE) {
				return lang;
			}
			break;
		}
	}

	return EDITOR_SYNTAX_NONE;
}

static int editorSyntaxFilenameIsExtensionless(const char *filename) {
	if (filename == NULL || filename[0] == '\0') {
		return 1;
	}

	const char *base = strrchr(filename, '/');
	if (base != NULL) {
		base++;
	} else {
		base = filename;
	}
	if (base[0] == '\0') {
		return 0;
	}
	if (strchr(base, '.') == NULL) {
		return 1;
	}
	if (base[0] == '.' && strchr(base + 1, '.') == NULL) {
		return 1;
	}
	return 0;
}

enum editorSyntaxLanguage editorSyntaxDetectLanguageFromFilename(const char *filename) {
	if (filename == NULL || filename[0] == '\0') {
		return EDITOR_SYNTAX_NONE;
	}

	const char *dot = strrchr(filename, '.');
	if (dot != NULL) {
		const struct editorSyntaxLanguageDef *def =
				editorSyntaxLookupLanguageByExtension(dot);
		if (def != NULL) {
			return def->id;
		}
	}

	const char *base = strrchr(filename, '/');
	if (base != NULL) {
		base++;
	} else {
		base = filename;
	}
	const struct editorSyntaxLanguageDef *def =
			editorSyntaxLookupLanguageByBasename(base);
	if (def != NULL) {
		return def->id;
	}

	return EDITOR_SYNTAX_NONE;
}

enum editorSyntaxLanguage editorSyntaxDetectLanguageFromFilenameAndFirstLine(
		const char *filename, const char *first_line) {
	enum editorSyntaxLanguage from_filename = editorSyntaxDetectLanguageFromFilename(filename);
	if (from_filename != EDITOR_SYNTAX_NONE) {
		return from_filename;
	}
	if (!editorSyntaxFilenameIsExtensionless(filename)) {
		return EDITOR_SYNTAX_NONE;
	}

	return editorSyntaxDetectLanguageFromShebang(first_line);
}

static void editorSyntaxParsedTreeInit(struct editorSyntaxParsedTree *parsed,
		enum editorSyntaxLanguage language) {
	if (parsed == NULL) {
		return;
	}
	parsed->language = language;
	parsed->parser = NULL;
	parsed->tree = NULL;
	parsed->included_ranges = NULL;
	parsed->included_range_count = 0;
	parsed->revision = 0;
}

static int editorSyntaxParsedTreeCreateParser(struct editorSyntaxParsedTree *parsed,
		enum editorSyntaxLanguage language) {
	if (parsed == NULL) {
		return 0;
	}
	const TSLanguage *ts_language = editorSyntaxLanguageObject(language);
	if (ts_language == NULL) {
		return 0;
	}

	parsed->language = language;
	parsed->parser = ts_parser_new();
	if (parsed->parser == NULL) {
		return 0;
	}
	if (!ts_parser_set_language(parsed->parser, ts_language)) {
		ts_parser_delete(parsed->parser);
		parsed->parser = NULL;
		return 0;
	}
	return 1;
}

static void editorSyntaxLocalsContextInit(struct editorSyntaxLocalsContext *ctx);
static void editorSyntaxLocalsContextFree(struct editorSyntaxLocalsContext *ctx);

static void editorSyntaxParsedTreeDestroy(struct editorSyntaxParsedTree *parsed) {
	if (parsed == NULL) {
		return;
	}
	if (parsed->tree != NULL) {
		ts_tree_delete(parsed->tree);
		parsed->tree = NULL;
	}
	if (parsed->parser != NULL) {
		ts_parser_delete(parsed->parser);
		parsed->parser = NULL;
	}
	free(parsed->included_ranges);
	parsed->included_ranges = NULL;
	parsed->included_range_count = 0;
}

static void editorSyntaxInjectedTreeInit(struct editorSyntaxInjectedTree *injection) {
	if (injection == NULL) {
		return;
	}
	editorSyntaxParsedTreeInit(&injection->parsed, EDITOR_SYNTAX_NONE);
	editorSyntaxLocalsContextInit(&injection->locals);
	injection->locals_revision = 0;
	injection->locals_valid = 0;
	injection->active = 0;
	injection->depth = 0;
}

static void editorSyntaxInjectedTreeDestroy(struct editorSyntaxInjectedTree *injection) {
	if (injection == NULL) {
		return;
	}
	editorSyntaxParsedTreeDestroy(&injection->parsed);
	editorSyntaxLocalsContextFree(&injection->locals);
	injection->locals_revision = 0;
	injection->locals_valid = 0;
	injection->active = 0;
	injection->depth = 0;
}

static void editorSyntaxStateQueueLimitEvent(struct editorSyntaxState *state,
		enum editorSyntaxLimitEventKind kind,
		enum editorSyntaxLanguage language,
		int row,
		int detail) {
	if (state == NULL) {
		return;
	}
	int idx = 0;
	if (state->limit_event_count < ROTIDE_SYNTAX_LIMIT_EVENT_CAP) {
		idx = (state->limit_event_start + state->limit_event_count) %
				ROTIDE_SYNTAX_LIMIT_EVENT_CAP;
		state->limit_event_count++;
	} else {
		idx = state->limit_event_start;
		state->limit_event_start = (state->limit_event_start + 1) %
				ROTIDE_SYNTAX_LIMIT_EVENT_CAP;
	}
	state->limit_events[idx].kind = kind;
	state->limit_events[idx].language = language;
	state->limit_events[idx].row = row;
	state->limit_events[idx].detail = detail;
}

void editorSyntaxStateRecordCaptureTruncated(struct editorSyntaxState *state, int row) {
	if (state == NULL) {
		return;
	}
	if (row < 0) {
		if (state->capture_truncated_unknown_reported) {
			return;
		}
		state->capture_truncated_unknown_reported = 1;
	} else {
		for (int i = 0; i < state->capture_truncated_row_count; i++) {
			if (state->capture_truncated_rows[i] == row) {
				return;
			}
		}
		if (state->capture_truncated_row_count >= state->capture_truncated_row_cap) {
			int new_cap = state->capture_truncated_row_cap == 0 ?
					8 : state->capture_truncated_row_cap * 2;
			int *new_rows = realloc(state->capture_truncated_rows,
					(size_t)new_cap * sizeof(*new_rows));
			if (new_rows == NULL) {
				return;
			}
			state->capture_truncated_rows = new_rows;
			state->capture_truncated_row_cap = new_cap;
		}
		state->capture_truncated_rows[state->capture_truncated_row_count++] = row;
	}
	editorSyntaxStateQueueLimitEvent(state, EDITOR_SYNTAX_LIMIT_EVENT_CAPTURE_TRUNCATED,
			state->language, row, ROTIDE_MAX_SYNTAX_SPANS_PER_ROW);
}

static void editorSyntaxStateRecordInjectionDepthExceeded(struct editorSyntaxState *state,
		enum editorSyntaxLanguage language,
		int depth) {
	if (state == NULL || state->injection_depth_exceeded_reported) {
		return;
	}
	state->injection_depth_exceeded_reported = 1;
	editorSyntaxStateQueueLimitEvent(state,
			EDITOR_SYNTAX_LIMIT_EVENT_INJECTION_DEPTH_EXCEEDED, language, -1, depth);
}

static void editorSyntaxStateRecordInjectionSlotsFull(struct editorSyntaxState *state,
		enum editorSyntaxLanguage language) {
	if (state == NULL || state->injection_slots_full_reported) {
		return;
	}
	state->injection_slots_full_reported = 1;
	editorSyntaxStateQueueLimitEvent(state,
			EDITOR_SYNTAX_LIMIT_EVENT_INJECTION_SLOTS_FULL, language, -1,
			ROTIDE_SYNTAX_MAX_INJECTION_TREES);
}

static void editorSyntaxParsedTreeResetTree(struct editorSyntaxParsedTree *parsed) {
	if (parsed == NULL || parsed->tree == NULL) {
		return;
	}
	ts_tree_delete(parsed->tree);
	parsed->tree = NULL;
	parsed->revision++;
}

static void editorSyntaxStateClearChangedRanges(struct editorSyntaxState *state) {
	if (state == NULL) {
		return;
	}
	state->last_changed_range_count = 0;
}

static int editorSyntaxStateEnsureChangedRangeCapacity(struct editorSyntaxState *state, int needed) {
	if (state == NULL || needed < 0) {
		return 0;
	}
	if (needed <= state->last_changed_range_cap) {
		return 1;
	}

	int new_cap = state->last_changed_range_cap == 0 ? 16 : state->last_changed_range_cap;
	while (new_cap < needed) {
		if (new_cap > INT_MAX / 2) {
			return 0;
		}
		new_cap *= 2;
	}
	size_t bytes = (size_t)new_cap * sizeof(*state->last_changed_ranges);
	struct editorSyntaxByteRange *grown = realloc(state->last_changed_ranges, bytes);
	if (grown == NULL) {
		return 0;
	}
	state->last_changed_ranges = grown;
	state->last_changed_range_cap = new_cap;
	return 1;
}

static int editorSyntaxStateAppendChangedRange(struct editorSyntaxState *state,
		uint32_t start_byte, uint32_t end_byte) {
	if (state == NULL || end_byte <= start_byte) {
		return 1;
	}

	if (state->last_changed_range_count > 0) {
		struct editorSyntaxByteRange *last =
				&state->last_changed_ranges[state->last_changed_range_count - 1];
		if (start_byte <= last->end_byte) {
			if (end_byte > last->end_byte) {
				last->end_byte = end_byte;
			}
			return 1;
		}
	}

	if (!editorSyntaxStateEnsureChangedRangeCapacity(state, state->last_changed_range_count + 1)) {
		return 0;
	}
	state->last_changed_ranges[state->last_changed_range_count].start_byte = start_byte;
	state->last_changed_ranges[state->last_changed_range_count].end_byte = end_byte;
	state->last_changed_range_count++;
	return 1;
}

static int editorSyntaxStateSetChangedRangesFull(struct editorSyntaxState *state, size_t source_len) {
	if (state == NULL) {
		return 0;
	}
	editorSyntaxStateClearChangedRanges(state);
	if (source_len == 0) {
		return 1;
	}
	if (source_len > UINT32_MAX) {
		source_len = UINT32_MAX;
	}
	return editorSyntaxStateAppendChangedRange(state, 0, (uint32_t)source_len);
}

static int editorSyntaxStateSetChangedRangesFromTrees(struct editorSyntaxState *state,
		const TSTree *old_tree,
		const TSTree *new_tree) {
	if (state == NULL) {
		return 0;
	}
	editorSyntaxStateClearChangedRanges(state);
	if (old_tree == NULL || new_tree == NULL) {
		return 1;
	}

	uint32_t range_count = 0;
	TSRange *ranges = ts_tree_get_changed_ranges(old_tree, new_tree, &range_count);
	if (ranges == NULL && range_count > 0) {
		return 0;
	}

	int ok = 1;
	for (uint32_t i = 0; i < range_count; i++) {
		if (!editorSyntaxStateAppendChangedRange(state, ranges[i].start_byte,
					ranges[i].end_byte)) {
			ok = 0;
			break;
		}
	}
	free(ranges);
	return ok;
}

static int editorSyntaxParsedTreeSetIncludedRanges(struct editorSyntaxParsedTree *parsed,
		const TSRange *ranges,
		uint32_t range_count) {
	if (parsed == NULL || parsed->parser == NULL) {
		return 0;
	}

	TSRange *owned = NULL;
	if (range_count > 0) {
		size_t bytes = (size_t)range_count * sizeof(*owned);
		owned = malloc(bytes);
		if (owned == NULL) {
			return 0;
		}
		memcpy(owned, ranges, bytes);
	}

	if (!ts_parser_set_included_ranges(parsed->parser, owned, range_count)) {
		free(owned);
		return 0;
	}

	free(parsed->included_ranges);
	parsed->included_ranges = owned;
	parsed->included_range_count = range_count;
	return 1;
}

static int editorSyntaxParsedTreeParse(struct editorSyntaxParsedTree *parsed,
		struct editorSyntaxState *state,
		const struct editorTextSource *source,
		int incremental) {
	if (parsed == NULL || parsed->parser == NULL || source == NULL || source->read == NULL ||
			!editorSyntaxLengthFitsTreeSitter(source->length)) {
		return 0;
	}

	TSTree *old_tree = incremental ? parsed->tree : NULL;
	struct editorSyntaxBudgetConfig budget =
			editorSyntaxBudgetConfigForMode(state != NULL ? state->perf_mode :
					EDITOR_SYNTAX_PERF_NORMAL);
	TSInput input = {
		.payload = (void *)source,
		.read = editorSyntaxSourceRead,
		.encoding = TSInputEncodingUTF8,
		.decode = NULL
	};

	TSTree *new_tree = NULL;
	if (budget.parse_budget_ns > 0) {
		struct editorSyntaxDeadlineContext parse_deadline = {0};
		parse_deadline.deadline_ns = editorSyntaxComputeDeadlineNs(budget.parse_budget_ns);
		TSParseOptions options = {
			.payload = &parse_deadline,
			.progress_callback = editorSyntaxParseProgressCallback
		};
		new_tree = ts_parser_parse_with_options(parsed->parser, old_tree, input, options);
		if (new_tree == NULL && parse_deadline.exceeded) {
			if (old_tree == NULL &&
					source->length <= ROTIDE_SYNTAX_PERF_DEGRADED_PREDICATES_BYTES) {
				new_tree = ts_parser_parse(parsed->parser, old_tree, input);
			}
			if (new_tree == NULL) {
				if (state != NULL) {
					state->budget_parse_exceeded = 1;
				}
				return 1;
			}
		}
	} else {
		new_tree = ts_parser_parse(parsed->parser, old_tree, input);
	}
	if (new_tree == NULL) {
		return 0;
	}

	if (state != NULL) {
		if (!editorSyntaxStateSetChangedRangesFromTrees(state, old_tree, new_tree)) {
			ts_tree_delete(new_tree);
			return 0;
		}
	}

	if (parsed->tree != NULL) {
		ts_tree_delete(parsed->tree);
	}
	parsed->tree = new_tree;
	parsed->revision++;
	return 1;
}

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

static int editorSyntaxNodeText(struct editorSyntaxState *state,
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

static void editorSyntaxLocalsContextInit(struct editorSyntaxLocalsContext *ctx) {
	if (ctx == NULL) {
		return;
	}
	ctx->marks = NULL;
	ctx->count = 0;
	ctx->cap = 0;
}

static void editorSyntaxLocalsContextFree(struct editorSyntaxLocalsContext *ctx) {
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

static int editorSyntaxLocalsContextNodeIsLocal(const struct editorSyntaxLocalsContext *ctx,
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

static int editorSyntaxBuildLocalsContext(const TSTree *tree,
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

static int editorSyntaxMatchFindCaptureNode(const TSQueryMatch *match,
		uint32_t capture_id,
		TSNode *node_out) {
	if (match == NULL || node_out == NULL) {
		return 0;
	}
	for (uint16_t i = 0; i < match->capture_count; i++) {
		if (match->captures[i].index == capture_id) {
			*node_out = match->captures[i].node;
			return 1;
		}
	}
	return 0;
}

static int editorSyntaxPredicateArgText(const TSQuery *query,
		const TSQueryMatch *match,
		const struct editorSyntaxPredicateContext *ctx,
		const TSQueryPredicateStep *arg,
		int scratch_idx,
		const char **text_out,
		size_t *len_out) {
	if (query == NULL || match == NULL || ctx == NULL || arg == NULL ||
			text_out == NULL || len_out == NULL) {
		return 0;
	}

	if (arg->type == TSQueryPredicateStepTypeString) {
		uint32_t str_len = 0;
		const char *str = ts_query_string_value_for_id(query, arg->value_id, &str_len);
		if (str == NULL) {
			return 0;
		}
		*text_out = str;
		*len_out = (size_t)str_len;
		return 1;
	}

	if (arg->type == TSQueryPredicateStepTypeCapture) {
		TSNode node;
		if (!editorSyntaxMatchFindCaptureNode(match, arg->value_id, &node)) {
			return 0;
		}
		return editorSyntaxNodeText(ctx->state, ctx->source, node, scratch_idx, text_out, len_out);
	}

	return 0;
}

static int editorSyntaxRegexMatchCompiled(const char *text, size_t text_len, regex_t *regex) {
	if (text == NULL || regex == NULL) {
		return 0;
	}

	char *text_buf = malloc(text_len + 1);
	if (text_buf == NULL) {
		free(text_buf);
		return 0;
	}
	memcpy(text_buf, text, text_len);
	text_buf[text_len] = '\0';
	int matched = regexec(regex, text_buf, 0, NULL, 0) == 0;
	free(text_buf);
	return matched;
}

static int editorSyntaxRegexMatch(const char *text, size_t text_len,
		const char *pattern, size_t pattern_len) {
	if (text == NULL || pattern == NULL) {
		return 0;
	}

	char *pattern_buf = malloc(pattern_len + 1);
	if (pattern_buf == NULL) {
		return 0;
	}
	memcpy(pattern_buf, pattern, pattern_len);
	pattern_buf[pattern_len] = '\0';

	regex_t regex;
	int compiled = regcomp(&regex, pattern_buf, REG_EXTENDED | REG_NOSUB);
	free(pattern_buf);
	if (compiled != 0) {
		return 0;
	}

	int matched = editorSyntaxRegexMatchCompiled(text, text_len, &regex);
	regfree(&regex);
	return matched;
}

static int editorSyntaxRegexMatchCached(const TSQuery *query,
		uint32_t string_id,
		const char *text,
		size_t text_len) {
	if (query == NULL || text == NULL) {
		return 0;
	}

	struct editorSyntaxQueryCacheEntry *cache = editorSyntaxQueryCacheEntryForQuery(query);
	if (cache == NULL || cache->compiled_regexes == NULL || cache->compiled_regex_compiled == NULL ||
			cache->compiled_regex_failed == NULL || string_id >= cache->string_count) {
		uint32_t pattern_len = 0;
		const char *pattern = ts_query_string_value_for_id(query, string_id, &pattern_len);
		if (pattern == NULL) {
			return 0;
		}
		return editorSyntaxRegexMatch(text, text_len, pattern, (size_t)pattern_len);
	}

	if (!cache->compiled_regex_compiled[string_id] &&
			!cache->compiled_regex_failed[string_id]) {
		uint32_t pattern_len = 0;
		const char *pattern = ts_query_string_value_for_id(query, string_id, &pattern_len);
		if (pattern == NULL) {
			cache->compiled_regex_failed[string_id] = 1;
			return 0;
		}

		char *pattern_buf = malloc((size_t)pattern_len + 1);
		if (pattern_buf == NULL) {
			return 0;
		}
		memcpy(pattern_buf, pattern, pattern_len);
		pattern_buf[pattern_len] = '\0';

		if (regcomp(&cache->compiled_regexes[string_id], pattern_buf,
					REG_EXTENDED | REG_NOSUB) == 0) {
			cache->compiled_regex_compiled[string_id] = 1;
		} else {
			cache->compiled_regex_failed[string_id] = 1;
		}
		free(pattern_buf);
	}

	if (!cache->compiled_regex_compiled[string_id]) {
		return 0;
	}

	return editorSyntaxRegexMatchCompiled(text, text_len, &cache->compiled_regexes[string_id]);
}

static int editorSyntaxPredicateTargetNode(const TSQuery *query,
		const TSQueryMatch *match,
		const TSQueryPredicateStep *args,
		uint32_t arg_count,
		TSNode *node_out,
		const char **property_out,
		size_t *property_len_out) {
	if (query == NULL || match == NULL || args == NULL || arg_count == 0 ||
			node_out == NULL || property_out == NULL || property_len_out == NULL) {
		return 0;
	}

	if (args[0].type == TSQueryPredicateStepTypeCapture) {
		if (!editorSyntaxMatchFindCaptureNode(match, args[0].value_id, node_out)) {
			return 0;
		}
		if (arg_count < 2 || args[1].type != TSQueryPredicateStepTypeString) {
			return 0;
		}
		uint32_t property_len = 0;
		const char *property = ts_query_string_value_for_id(query, args[1].value_id,
				&property_len);
		if (property == NULL) {
			return 0;
		}
		*property_out = property;
		*property_len_out = property_len;
		return 1;
	}

	if (args[0].type == TSQueryPredicateStepTypeString && match->capture_count > 0) {
		*node_out = match->captures[0].node;
		uint32_t property_len = 0;
		const char *property = ts_query_string_value_for_id(query, args[0].value_id,
				&property_len);
		if (property == NULL) {
			return 0;
		}
		*property_out = property;
		*property_len_out = property_len;
		return 1;
	}

	return 0;
}

typedef int (*editorSyntaxPredicateHandlerFn)(
		const TSQuery *query,
		const TSQueryMatch *match,
		const struct editorSyntaxPredicateContext *ctx,
		const TSQueryPredicateStep *args,
		uint32_t arg_count);

static int editorSyntaxPredicateAlwaysPass(const TSQuery *query,
		const TSQueryMatch *match,
		const struct editorSyntaxPredicateContext *ctx,
		const TSQueryPredicateStep *args,
		uint32_t arg_count) {
	(void)query;
	(void)match;
	(void)ctx;
	(void)args;
	(void)arg_count;
	return 1;
}

static int editorSyntaxPredicateEqShared(int negated,
		const TSQuery *query,
		const TSQueryMatch *match,
		const struct editorSyntaxPredicateContext *ctx,
		const TSQueryPredicateStep *args,
		uint32_t arg_count) {
	if (arg_count < 2) {
		return 1;
	}
	const char *left = NULL;
	size_t left_len = 0;
	const char *right = NULL;
	size_t right_len = 0;
	if (!editorSyntaxPredicateArgText(query, match, ctx, &args[0], 0, &left, &left_len) ||
			!editorSyntaxPredicateArgText(query, match, ctx, &args[1], 1, &right,
					&right_len)) {
		return 0;
	}
	int equal = (left_len == right_len) && memcmp(left, right, left_len) == 0;
	return negated ? !equal : equal;
}

static int editorSyntaxPredicateEq(const TSQuery *query,
		const TSQueryMatch *match,
		const struct editorSyntaxPredicateContext *ctx,
		const TSQueryPredicateStep *args,
		uint32_t arg_count) {
	return editorSyntaxPredicateEqShared(0, query, match, ctx, args, arg_count);
}

static int editorSyntaxPredicateNotEq(const TSQuery *query,
		const TSQueryMatch *match,
		const struct editorSyntaxPredicateContext *ctx,
		const TSQueryPredicateStep *args,
		uint32_t arg_count) {
	return editorSyntaxPredicateEqShared(1, query, match, ctx, args, arg_count);
}

static int editorSyntaxPredicateMatchShared(int negated,
		const TSQuery *query,
		const TSQueryMatch *match,
		const struct editorSyntaxPredicateContext *ctx,
		const TSQueryPredicateStep *args,
		uint32_t arg_count) {
	if (arg_count < 2) {
		return 1;
	}
	const char *text = NULL;
	size_t text_len = 0;
	const char *pattern = NULL;
	size_t pattern_len = 0;
	if (!editorSyntaxPredicateArgText(query, match, ctx, &args[0], 0, &text, &text_len) ||
			!editorSyntaxPredicateArgText(query, match, ctx, &args[1], 1, &pattern,
					&pattern_len)) {
		return 0;
	}
	int matched = 0;
	if (args[1].type == TSQueryPredicateStepTypeString) {
		matched = editorSyntaxRegexMatchCached(query, args[1].value_id, text, text_len);
	} else {
		matched = editorSyntaxRegexMatch(text, text_len, pattern, pattern_len);
	}
	return negated ? !matched : matched;
}

static int editorSyntaxPredicateMatch(const TSQuery *query,
		const TSQueryMatch *match,
		const struct editorSyntaxPredicateContext *ctx,
		const TSQueryPredicateStep *args,
		uint32_t arg_count) {
	return editorSyntaxPredicateMatchShared(0, query, match, ctx, args, arg_count);
}

static int editorSyntaxPredicateNotMatch(const TSQuery *query,
		const TSQueryMatch *match,
		const struct editorSyntaxPredicateContext *ctx,
		const TSQueryPredicateStep *args,
		uint32_t arg_count) {
	return editorSyntaxPredicateMatchShared(1, query, match, ctx, args, arg_count);
}

static int editorSyntaxPredicateAnyOfShared(int negated,
		const TSQuery *query,
		const TSQueryMatch *match,
		const struct editorSyntaxPredicateContext *ctx,
		const TSQueryPredicateStep *args,
		uint32_t arg_count) {
	if (arg_count < 2) {
		return 1;
	}
	const char *target = NULL;
	size_t target_len = 0;
	if (!editorSyntaxPredicateArgText(query, match, ctx, &args[0], 0, &target, &target_len)) {
		return 0;
	}

	int found = 0;
	for (uint32_t i = 1; i < arg_count; i++) {
		if (args[i].type != TSQueryPredicateStepTypeString) {
			continue;
		}
		uint32_t value_len = 0;
		const char *value = ts_query_string_value_for_id(query, args[i].value_id,
				&value_len);
		if (value == NULL) {
			continue;
		}
		if (target_len == value_len && memcmp(target, value, target_len) == 0) {
			found = 1;
			break;
		}
	}

	return negated ? !found : found;
}

static int editorSyntaxPredicateAnyOf(const TSQuery *query,
		const TSQueryMatch *match,
		const struct editorSyntaxPredicateContext *ctx,
		const TSQueryPredicateStep *args,
		uint32_t arg_count) {
	return editorSyntaxPredicateAnyOfShared(0, query, match, ctx, args, arg_count);
}

static int editorSyntaxPredicateNotAnyOf(const TSQuery *query,
		const TSQueryMatch *match,
		const struct editorSyntaxPredicateContext *ctx,
		const TSQueryPredicateStep *args,
		uint32_t arg_count) {
	return editorSyntaxPredicateAnyOfShared(1, query, match, ctx, args, arg_count);
}

/* `#is?` / `#is-not?` recognize only the `local` property today; other properties
 * are treated as not-set. Kept as a single helper per the predicate-table plan. */
static int editorSyntaxPredicateIsShared(int negated,
		const TSQuery *query,
		const TSQueryMatch *match,
		const struct editorSyntaxPredicateContext *ctx,
		const TSQueryPredicateStep *args,
		uint32_t arg_count) {
	TSNode target;
	const char *property = NULL;
	size_t property_len = 0;
	if (!editorSyntaxPredicateTargetNode(query, match, args, arg_count, &target,
				&property, &property_len)) {
		return 1;
	}
	int is_property = 0;
	if (editorSyntaxStringEquals(property, property_len, "local")) {
		is_property = editorSyntaxLocalsContextNodeIsLocal(ctx->locals, target);
	}
	return negated ? !is_property : is_property;
}

static int editorSyntaxPredicateIs(const TSQuery *query,
		const TSQueryMatch *match,
		const struct editorSyntaxPredicateContext *ctx,
		const TSQueryPredicateStep *args,
		uint32_t arg_count) {
	return editorSyntaxPredicateIsShared(0, query, match, ctx, args, arg_count);
}

static int editorSyntaxPredicateIsNot(const TSQuery *query,
		const TSQueryMatch *match,
		const struct editorSyntaxPredicateContext *ctx,
		const TSQueryPredicateStep *args,
		uint32_t arg_count) {
	return editorSyntaxPredicateIsShared(1, query, match, ctx, args, arg_count);
}

struct editorSyntaxPredicateEntry {
	const char *name;
	editorSyntaxPredicateHandlerFn handler;
};

static const struct editorSyntaxPredicateEntry g_predicate_table[] = {
	{"set!",        editorSyntaxPredicateAlwaysPass},
	{"eq?",         editorSyntaxPredicateEq},
	{"not-eq?",     editorSyntaxPredicateNotEq},
	{"match?",      editorSyntaxPredicateMatch},
	{"not-match?",  editorSyntaxPredicateNotMatch},
	{"any-of?",     editorSyntaxPredicateAnyOf},
	{"not-any-of?", editorSyntaxPredicateNotAnyOf},
	{"is?",         editorSyntaxPredicateIs},
	{"is-not?",     editorSyntaxPredicateIsNot}
};

static int editorSyntaxEvaluatePredicate(const TSQuery *query,
		const TSQueryMatch *match,
		const struct editorSyntaxPredicateContext *ctx,
		const TSQueryPredicateStep *steps,
		uint32_t step_count) {
	if (query == NULL || match == NULL || ctx == NULL || steps == NULL || step_count == 0) {
		return 1;
	}
	if (steps[0].type != TSQueryPredicateStepTypeString) {
		return 1;
	}

	uint32_t command_len = 0;
	const char *command = ts_query_string_value_for_id(query, steps[0].value_id, &command_len);
	if (command == NULL) {
		return 1;
	}

	const TSQueryPredicateStep *args = &steps[1];
	uint32_t arg_count = step_count - 1;

	for (size_t i = 0; i < sizeof(g_predicate_table) / sizeof(g_predicate_table[0]); i++) {
		if (editorSyntaxStringEquals(command, command_len, g_predicate_table[i].name)) {
			return g_predicate_table[i].handler(query, match, ctx, args, arg_count);
		}
	}
	return 1;
}

static int editorSyntaxMatchPassesPredicates(const TSQuery *query,
		uint32_t pattern_index,
		const TSQueryMatch *match,
		const struct editorSyntaxPredicateContext *ctx) {
	if (query == NULL || match == NULL || ctx == NULL) {
		return 1;
	}

	uint32_t step_count = 0;
	const TSQueryPredicateStep *steps = ts_query_predicates_for_pattern(query, pattern_index,
			&step_count);
	if (steps == NULL || step_count == 0) {
		return 1;
	}

	uint32_t i = 0;
	while (i < step_count) {
		uint32_t start = i;
		while (i < step_count && steps[i].type != TSQueryPredicateStepTypeDone) {
			i++;
		}
		uint32_t end = i;
		if (end > start) {
			if (!editorSyntaxEvaluatePredicate(query, match, ctx, &steps[start], end - start)) {
				return 0;
			}
		}
		i++;
	}

	return 1;
}

static int editorSyntaxCaptureVecAppend(struct editorSyntaxCaptureVec *vec,
		uint32_t start_byte,
		uint32_t end_byte,
		enum editorSyntaxHighlightClass highlight_class) {
	if (vec == NULL) {
		return 0;
	}
	if (vec->count >= vec->cap) {
		int new_cap = vec->cap == 0 ? 128 : vec->cap * 2;
		if (new_cap <= vec->cap) {
			return 0;
		}
		size_t bytes = (size_t)new_cap * sizeof(*vec->items);
		struct editorSyntaxCapture *grown = realloc(vec->items, bytes);
		if (grown == NULL) {
			return 0;
		}
		vec->items = grown;
		vec->cap = new_cap;
	}

	vec->items[vec->count].start_byte = start_byte;
	vec->items[vec->count].end_byte = end_byte;
	vec->items[vec->count].highlight_class = highlight_class;
	vec->count++;
	return 1;
}

static void editorSyntaxCaptureVecFree(struct editorSyntaxCaptureVec *vec) {
	if (vec == NULL) {
		return;
	}
	free(vec->items);
	vec->items = NULL;
	vec->count = 0;
	vec->cap = 0;
}

static int editorSyntaxCollectCapturesFromTree(struct editorSyntaxState *state,
		const TSTree *tree,
		enum editorSyntaxLanguage language,
		const struct editorTextSource *source,
		uint32_t start_byte,
		uint32_t end_byte,
		const struct editorSyntaxLocalsContext *locals,
		int skip_predicates,
		struct editorSyntaxCaptureVec *captures_out,
		int *query_unavailable_out);

static int editorSyntaxCollectCapturesFromTree(struct editorSyntaxState *state,
		const TSTree *tree,
		enum editorSyntaxLanguage language,
		const struct editorTextSource *source,
		uint32_t start_byte,
		uint32_t end_byte,
		const struct editorSyntaxLocalsContext *locals,
		int skip_predicates,
		struct editorSyntaxCaptureVec *captures_out,
		int *query_unavailable_out) {
	if (query_unavailable_out != NULL) {
		*query_unavailable_out = 0;
	}
	if (captures_out == NULL) {
		return 0;
	}
	if (tree == NULL || start_byte >= end_byte) {
		return 1;
	}

	const struct editorSyntaxQueryCacheEntry *cache =
			editorSyntaxHighlightQueryCachePtr(language);
	if (cache == NULL) {
		if (query_unavailable_out != NULL) {
			*query_unavailable_out = 1;
		}
		return 0;
	}

	TSQueryCursor *cursor = ts_query_cursor_new();
	if (cursor == NULL) {
		return 0;
	}

	TSNode root = ts_tree_root_node(tree);
	ts_query_cursor_set_byte_range(cursor, start_byte, end_byte);
	struct editorSyntaxBudgetConfig budget =
			editorSyntaxBudgetConfigForMode(state != NULL ? state->perf_mode :
					EDITOR_SYNTAX_PERF_NORMAL);
	if (budget.query_match_limit > 0) {
		ts_query_cursor_set_match_limit(cursor, budget.query_match_limit);
	}
	struct editorSyntaxDeadlineContext query_deadline = {0};
	TSQueryCursorOptions query_options = {0};
	if (budget.query_budget_ns > 0) {
		query_deadline.deadline_ns = editorSyntaxComputeDeadlineNs(budget.query_budget_ns);
		query_options.payload = &query_deadline;
		query_options.progress_callback = editorSyntaxQueryProgressCallback;
		ts_query_cursor_exec_with_options(cursor, cache->query, root, &query_options);
	} else {
		ts_query_cursor_exec(cursor, cache->query, root);
	}

	struct editorSyntaxPredicateContext predicate_ctx = {
		.state = state,
		.source = source,
		.locals = locals
	};

	TSQueryMatch match;
	uint32_t capture_idx = 0;
	while (ts_query_cursor_next_capture(cursor, &match, &capture_idx)) {
		if (!skip_predicates &&
				!editorSyntaxMatchPassesPredicates(cache->query, match.pattern_index, &match,
						&predicate_ctx)) {
			continue;
		}
		if (capture_idx >= match.capture_count) {
			continue;
		}
		TSQueryCapture capture = match.captures[capture_idx];
		if (capture.index >= cache->capture_count) {
			continue;
		}
		enum editorSyntaxHighlightClass highlight_class =
				cache->capture_classes[capture.index];
		if (highlight_class == EDITOR_SYNTAX_HL_NONE) {
			continue;
		}

		uint32_t node_start = ts_node_start_byte(capture.node);
		uint32_t node_end = ts_node_end_byte(capture.node);
		uint32_t capture_start = node_start;
		uint32_t capture_end = node_end;
		if (capture_end <= capture_start) {
			continue;
		}
		if (capture_end <= start_byte || capture_start >= end_byte) {
			continue;
		}
		if (capture_start < start_byte) {
			capture_start = start_byte;
		}
		if (capture_end > end_byte) {
			capture_end = end_byte;
		}
		if (capture_end <= capture_start) {
			continue;
		}

		if (!editorSyntaxCaptureVecAppend(captures_out, capture_start, capture_end,
					highlight_class)) {
			ts_query_cursor_delete(cursor);
			return 0;
		}
	}

	if (state != NULL) {
		if (query_deadline.exceeded) {
			state->budget_query_exceeded = 1;
		}
		if (ts_query_cursor_did_exceed_match_limit(cursor)) {
			state->budget_query_exceeded = 1;
		}
	}

	ts_query_cursor_delete(cursor);
	return 1;
}

static int editorSyntaxRangeVecAppend(struct editorSyntaxRangeVec *ranges, const TSRange *range) {
	if (ranges == NULL || range == NULL) {
		return 0;
	}
	if (ranges->count >= ranges->cap) {
		uint32_t new_cap = ranges->cap == 0 ? 16 : ranges->cap * 2;
		if (new_cap <= ranges->cap) {
			return 0;
		}
		size_t bytes = (size_t)new_cap * sizeof(*ranges->items);
		TSRange *grown = realloc(ranges->items, bytes);
		if (grown == NULL) {
			return 0;
		}
		ranges->items = grown;
		ranges->cap = new_cap;
	}
	ranges->items[ranges->count] = *range;
	ranges->count++;
	return 1;
}

static void editorSyntaxRangeVecFree(struct editorSyntaxRangeVec *ranges) {
	if (ranges == NULL) {
		return;
	}
	free(ranges->items);
	ranges->items = NULL;
	ranges->count = 0;
	ranges->cap = 0;
}

static int editorSyntaxCompareRange(const void *a, const void *b) {
	const TSRange *left = a;
	const TSRange *right = b;
	if (left->start_byte < right->start_byte) {
		return -1;
	}
	if (left->start_byte > right->start_byte) {
		return 1;
	}
	if (left->end_byte < right->end_byte) {
		return -1;
	}
	if (left->end_byte > right->end_byte) {
		return 1;
	}
	return 0;
}

struct editorSyntaxInjectionWorkItem {
	enum editorSyntaxLanguage language;
	struct editorSyntaxRangeVec ranges;
	int depth;
};

struct editorSyntaxInjectionWork {
	struct editorSyntaxInjectionWorkItem items[ROTIDE_SYNTAX_MAX_INJECTION_TREES];
	int count;
	int slots_full;
	enum editorSyntaxLanguage slots_full_language;
};

static enum editorSyntaxLanguage editorSyntaxLanguageFromInjectionName(const char *name,
		size_t len) {
	const struct editorSyntaxLanguageDef *def =
			editorSyntaxLookupLanguageByInjectionName(name, len);
	return def != NULL ? def->id : EDITOR_SYNTAX_NONE;
}

static int editorSyntaxLanguageHasInjectionQuery(enum editorSyntaxLanguage language) {
	const struct editorSyntaxLanguageDef *def = editorSyntaxLookupLanguage(language);
	return def != NULL && def->injection_parts != NULL && def->injection_part_count > 0;
}

static struct editorSyntaxInjectionWorkItem *editorSyntaxInjectionWorkFind(
		struct editorSyntaxInjectionWork *work,
		enum editorSyntaxLanguage language) {
	if (work == NULL || language == EDITOR_SYNTAX_NONE) {
		return NULL;
	}
	for (int i = 0; i < work->count; i++) {
		if (work->items[i].language == language) {
			return &work->items[i];
		}
	}
	return NULL;
}

static struct editorSyntaxInjectionWorkItem *editorSyntaxInjectionWorkEnsure(
		struct editorSyntaxInjectionWork *work,
		enum editorSyntaxLanguage language,
		int depth) {
	if (work == NULL || language == EDITOR_SYNTAX_NONE) {
		return NULL;
	}
	struct editorSyntaxInjectionWorkItem *item =
			editorSyntaxInjectionWorkFind(work, language);
	if (item != NULL) {
		if (depth < item->depth) {
			item->depth = depth;
		}
		return item;
	}
	if (work->count >= ROTIDE_SYNTAX_MAX_INJECTION_TREES) {
		work->slots_full = 1;
		work->slots_full_language = language;
		return NULL;
	}
	item = &work->items[work->count++];
	item->language = language;
	item->ranges.items = NULL;
	item->ranges.count = 0;
	item->ranges.cap = 0;
	item->depth = depth;
	return item;
}

static int editorSyntaxInjectionWorkAppendRange(struct editorSyntaxInjectionWork *work,
		enum editorSyntaxLanguage language,
		int depth,
		const TSRange *range) {
	struct editorSyntaxInjectionWorkItem *item =
			editorSyntaxInjectionWorkEnsure(work, language, depth);
	if (item == NULL) {
		return 1;
	}
	return editorSyntaxRangeVecAppend(&item->ranges, range);
}

static int editorSyntaxInjectionWorkAppendRangeExcludingChildren(
		struct editorSyntaxInjectionWork *work,
		enum editorSyntaxLanguage language,
		int depth,
		TSNode node,
		const TSRange *range) {
	if (work == NULL || range == NULL) {
		return 0;
	}
	if (ts_node_is_null(node)) {
		return editorSyntaxInjectionWorkAppendRange(work, language, depth, range);
	}

	uint32_t child_count = ts_node_child_count(node);
	if (child_count == 0) {
		return editorSyntaxInjectionWorkAppendRange(work, language, depth, range);
	}

	uint32_t segment_start_byte = range->start_byte;
	TSPoint segment_start_point = range->start_point;
	for (uint32_t child_idx = 0; child_idx < child_count; child_idx++) {
		TSNode child = ts_node_child(node, child_idx);
		uint32_t child_start_byte = ts_node_start_byte(child);
		uint32_t child_end_byte = ts_node_end_byte(child);
		if (child_end_byte <= range->start_byte || child_start_byte >= range->end_byte) {
			continue;
		}

		if (child_start_byte > segment_start_byte) {
			TSRange segment = {
				.start_point = segment_start_point,
				.end_point = ts_node_start_point(child),
				.start_byte = segment_start_byte,
				.end_byte = child_start_byte
			};
			if (segment.end_byte > segment.start_byte &&
					!editorSyntaxInjectionWorkAppendRange(work, language, depth, &segment)) {
				return 0;
			}
		}

		if (child_end_byte >= range->end_byte) {
			segment_start_byte = range->end_byte;
			segment_start_point = range->end_point;
			break;
		}
		if (child_end_byte > segment_start_byte) {
			segment_start_byte = child_end_byte;
			segment_start_point = ts_node_end_point(child);
		}
	}

	if (segment_start_byte < range->end_byte) {
		TSRange segment = {
			.start_point = segment_start_point,
			.end_point = range->end_point,
			.start_byte = segment_start_byte,
			.end_byte = range->end_byte
		};
		return editorSyntaxInjectionWorkAppendRange(work, language, depth, &segment);
	}
	return 1;
}

static void editorSyntaxInjectionWorkFree(struct editorSyntaxInjectionWork *work) {
	if (work == NULL) {
		return;
	}
	for (int i = 0; i < work->count; i++) {
		editorSyntaxRangeVecFree(&work->items[i].ranges);
	}
	work->count = 0;
}

static void editorSyntaxRangeVecSortUnique(struct editorSyntaxRangeVec *ranges) {
	if (ranges == NULL || ranges->count <= 1) {
		return;
	}
	qsort(ranges->items, ranges->count, sizeof(ranges->items[0]), editorSyntaxCompareRange);
	uint32_t out = 0;
	for (uint32_t i = 0; i < ranges->count; i++) {
		if (ranges->items[i].end_byte <= ranges->items[i].start_byte) {
			continue;
		}
		if (out > 0 &&
				ranges->items[out - 1].start_byte == ranges->items[i].start_byte &&
				ranges->items[out - 1].end_byte == ranges->items[i].end_byte) {
			continue;
		}
		ranges->items[out++] = ranges->items[i];
	}
	ranges->count = out;
}

static struct editorSyntaxInjectedTree *editorSyntaxStateFindInjection(
		struct editorSyntaxState *state,
		enum editorSyntaxLanguage language) {
	if (state == NULL || language == EDITOR_SYNTAX_NONE) {
		return NULL;
	}
	for (int i = 0; i < state->injection_count; i++) {
		if (state->injections[i].parsed.language == language) {
			return &state->injections[i];
		}
	}
	return NULL;
}

static struct editorSyntaxInjectedTree *editorSyntaxStateEnsureInjection(
		struct editorSyntaxState *state,
		enum editorSyntaxLanguage language) {
	if (state == NULL || language == EDITOR_SYNTAX_NONE) {
		return NULL;
	}
	struct editorSyntaxInjectedTree *injection =
			editorSyntaxStateFindInjection(state, language);
	if (injection != NULL) {
		return injection;
	}
	if (state->injection_count >= ROTIDE_SYNTAX_MAX_INJECTION_TREES) {
		return NULL;
	}
	injection = &state->injections[state->injection_count++];
	editorSyntaxInjectedTreeInit(injection);
	if (!editorSyntaxParsedTreeCreateParser(&injection->parsed, language)) {
		editorSyntaxInjectedTreeDestroy(injection);
		state->injection_count--;
		return NULL;
	}
	return injection;
}

static void editorSyntaxStateMarkInjectionsInactive(struct editorSyntaxState *state) {
	if (state == NULL) {
		return;
	}
	for (int i = 0; i < state->injection_count; i++) {
		state->injections[i].active = 0;
		state->injections[i].depth = 0;
	}
}

static int editorSyntaxStateResetInactiveInjections(struct editorSyntaxState *state) {
	if (state == NULL) {
		return 0;
	}
	for (int i = 0; i < state->injection_count; i++) {
		struct editorSyntaxInjectedTree *injection = &state->injections[i];
		if (injection->active) {
			continue;
		}
		if (injection->parsed.parser != NULL &&
				!editorSyntaxParsedTreeSetIncludedRanges(&injection->parsed, NULL, 0)) {
			return 0;
		}
		editorSyntaxParsedTreeResetTree(&injection->parsed);
		injection->locals_valid = 0;
	}
	return 1;
}

static void editorSyntaxApplyInputEdit(TSTree *tree, const struct editorSyntaxEdit *edit);

static void editorSyntaxStateApplyEditToInjections(struct editorSyntaxState *state,
		const struct editorSyntaxEdit *edit) {
	if (state == NULL || edit == NULL) {
		return;
	}
	for (int i = 0; i < state->injection_count; i++) {
		if (state->injections[i].parsed.tree != NULL) {
			editorSyntaxApplyInputEdit(state->injections[i].parsed.tree, edit);
		}
	}
}

static int editorSyntaxPointOffset(TSPoint point, int32_t row_offset,
		int32_t column_offset, TSPoint *out) {
	if (out == NULL || (row_offset < 0 && point.row < (uint32_t)-row_offset)) {
		return 0;
	}
	uint32_t row = row_offset >= 0 ? point.row + (uint32_t)row_offset :
			point.row - (uint32_t)-row_offset;
	if (row_offset >= 0 && row < point.row) {
		return 0;
	}
	uint32_t column_base = row == point.row ? point.column : 0;
	if (column_offset < 0 && column_base < (uint32_t)-column_offset) {
		return 0;
	}
	uint32_t column = column_offset >= 0 ? column_base + (uint32_t)column_offset :
			column_base - (uint32_t)-column_offset;
	if (column_offset >= 0 && column < column_base) {
		return 0;
	}
	*out = (TSPoint){.row = row, .column = column};
	return 1;
}

static int editorSyntaxByteForPoint(const struct editorTextSource *source,
		TSPoint target, uint32_t *byte_out) {
	if (source == NULL || source->read == NULL || byte_out == NULL ||
			source->length > UINT32_MAX) {
		return 0;
	}
	TSPoint pos = {.row = 0, .column = 0};
	size_t offset = 0;
	while (offset < source->length) {
		if (pos.row == target.row && pos.column == target.column) {
			*byte_out = (uint32_t)offset;
			return 1;
		}
		if (pos.row > target.row ||
				(pos.row == target.row && pos.column > target.column)) {
			return 0;
		}
		uint32_t bytes_read = 0;
		const char *chunk = source->read(source, offset, &bytes_read);
		if (chunk == NULL || bytes_read == 0) {
			return 0;
		}
		size_t chunk_len = bytes_read;
		if (chunk_len > source->length - offset) {
			chunk_len = source->length - offset;
		}
		for (size_t i = 0; i < chunk_len; i++) {
			if (chunk[i] == '\n') {
				pos.row++;
				pos.column = 0;
			} else {
				pos.column++;
			}
			offset++;
			if (pos.row == target.row && pos.column == target.column) {
				*byte_out = (uint32_t)offset;
				return 1;
			}
		}
	}
	if (pos.row == target.row && pos.column == target.column) {
		*byte_out = (uint32_t)offset;
		return 1;
	}
	return 0;
}

static int editorSyntaxApplyInjectionOffset(
		const struct editorSyntaxInjectionPatternMetadata *metadata,
		uint32_t capture_id,
		const struct editorTextSource *source,
		TSRange *range) {
	if (metadata == NULL || source == NULL || range == NULL || !metadata->has_offset ||
			metadata->offset_capture_id != capture_id) {
		return 1;
	}
	TSPoint start_point = {0};
	TSPoint end_point = {0};
	if (!editorSyntaxPointOffset(range->start_point, metadata->start_row_offset,
				metadata->start_column_offset, &start_point) ||
			!editorSyntaxPointOffset(range->end_point, metadata->end_row_offset,
				metadata->end_column_offset, &end_point)) {
		return 0;
	}
	uint32_t start_byte = 0;
	uint32_t end_byte = 0;
	if (!editorSyntaxByteForPoint(source, start_point, &start_byte) ||
			!editorSyntaxByteForPoint(source, end_point, &end_byte) ||
			end_byte <= start_byte) {
		return 0;
	}
	range->start_point = start_point;
	range->end_point = end_point;
	range->start_byte = start_byte;
	range->end_byte = end_byte;
	return 1;
}

static enum editorSyntaxLanguage editorSyntaxResolveInjectionLanguage(
		struct editorSyntaxState *state,
		const struct editorTextSource *source,
		const struct editorSyntaxQueryCacheEntry *cache,
		const struct editorSyntaxInjectionPatternMetadata *metadata,
		const TSQueryMatch *match) {
	if (metadata != NULL && metadata->language != NULL) {
		return editorSyntaxLanguageFromInjectionName(metadata->language,
				strlen(metadata->language));
	}
	if (state == NULL || source == NULL || cache == NULL || match == NULL) {
		return EDITOR_SYNTAX_NONE;
	}
	for (uint16_t capture_idx = 0; capture_idx < match->capture_count; capture_idx++) {
		TSQueryCapture capture = match->captures[capture_idx];
		if (capture.index >= cache->capture_count ||
				cache->capture_roles[capture.index] !=
					EDITOR_SYNTAX_CAPTURE_ROLE_INJECTION_LANGUAGE) {
			continue;
		}
		uint32_t start = ts_node_start_byte(capture.node);
		uint32_t end = ts_node_end_byte(capture.node);
		char **scratch = &state->scratch_primary;
		size_t *scratch_cap = &state->scratch_primary_cap;
		char *text = NULL;
		size_t len = 0;
		if (end <= start) {
			continue;
		}
		if ((size_t)(end - start) + 1 > *scratch_cap) {
			size_t new_cap = (size_t)(end - start) + 1;
			char *grown = realloc(*scratch, new_cap);
			if (grown == NULL) {
				return EDITOR_SYNTAX_NONE;
			}
			*scratch = grown;
			*scratch_cap = new_cap;
		}
		text = *scratch;
		if (!editorTextSourceCopyRange(source, start, end, text)) {
			return EDITOR_SYNTAX_NONE;
		}
		len = (size_t)(end - start);
		text[len] = '\0';
		enum editorSyntaxLanguage language =
				editorSyntaxLanguageFromInjectionName(text, len);
		if (language != EDITOR_SYNTAX_NONE) {
			return language;
		}
	}
	return EDITOR_SYNTAX_NONE;
}

static int editorSyntaxCollectInjectionRangesFromTree(struct editorSyntaxState *state,
		const TSTree *tree,
		enum editorSyntaxLanguage language,
		const struct editorTextSource *source,
		int target_depth,
		struct editorSyntaxInjectionWork *work) {
	if (state == NULL || tree == NULL || source == NULL || work == NULL) {
		return 0;
	}
	if (!editorSyntaxLanguageHasInjectionQuery(language)) {
		return 1;
	}
	const struct editorSyntaxQueryCacheEntry *cache =
			editorSyntaxInjectionQueryCachePtr(language);
	if (cache == NULL) {
		editorSyntaxStateRecordQueryUnavailable(state, language,
				EDITOR_SYNTAX_QUERY_KIND_INJECTION);
		return 1;
	}

	TSQueryCursor *cursor = ts_query_cursor_new();
	if (cursor == NULL) {
		return 0;
	}

	TSNode root = ts_tree_root_node(tree);
	struct editorSyntaxBudgetConfig budget =
			editorSyntaxBudgetConfigForMode(state->perf_mode);
	if (budget.query_match_limit > 0) {
		ts_query_cursor_set_match_limit(cursor, budget.query_match_limit);
	}
	struct editorSyntaxDeadlineContext query_deadline = {0};
	TSQueryCursorOptions query_options = {0};
	if (budget.query_budget_ns > 0) {
		query_deadline.deadline_ns = editorSyntaxComputeDeadlineNs(budget.query_budget_ns);
		query_options.payload = &query_deadline;
		query_options.progress_callback = editorSyntaxQueryProgressCallback;
		ts_query_cursor_exec_with_options(cursor, cache->query, root, &query_options);
	} else {
		ts_query_cursor_exec(cursor, cache->query, root);
	}

	struct editorSyntaxPredicateContext predicate_ctx = {
		.state = state,
		.source = source,
		.locals = NULL
	};

	TSQueryMatch match;
	while (ts_query_cursor_next_match(cursor, &match)) {
		if (match.pattern_index >= cache->pattern_count) {
			continue;
		}
		if (!state->perf_disable_predicates &&
				!editorSyntaxMatchPassesPredicates(cache->query, match.pattern_index, &match,
						&predicate_ctx)) {
			continue;
		}

		const struct editorSyntaxInjectionPatternMetadata *metadata =
				&cache->pattern_injection_metadata[match.pattern_index];
		enum editorSyntaxLanguage target_lang =
				editorSyntaxResolveInjectionLanguage(state, source, cache, metadata, &match);
		if (target_lang == EDITOR_SYNTAX_NONE || editorSyntaxLanguageObject(target_lang) == NULL) {
			continue;
		}

		for (uint16_t capture_idx = 0; capture_idx < match.capture_count; capture_idx++) {
			TSQueryCapture capture = match.captures[capture_idx];
			if (capture.index >= cache->capture_count) {
				continue;
			}
			if (cache->capture_roles[capture.index] !=
					EDITOR_SYNTAX_CAPTURE_ROLE_INJECTION_CONTENT) {
				continue;
			}

			TSRange range = {
				.start_point = ts_node_start_point(capture.node),
				.end_point = ts_node_end_point(capture.node),
				.start_byte = ts_node_start_byte(capture.node),
				.end_byte = ts_node_end_byte(capture.node)
			};
			if (!editorSyntaxApplyInjectionOffset(metadata, capture.index, source, &range)) {
				continue;
			}
			if (range.end_byte <= range.start_byte) {
				continue;
			}

			int append_ok = metadata->include_children ?
					editorSyntaxInjectionWorkAppendRange(work, target_lang,
							target_depth, &range) :
					editorSyntaxInjectionWorkAppendRangeExcludingChildren(work,
							target_lang, target_depth, capture.node, &range);
			if (!append_ok) {
				ts_query_cursor_delete(cursor);
				return 0;
			}
		}
	}

	if (query_deadline.exceeded || ts_query_cursor_did_exceed_match_limit(cursor)) {
		state->budget_query_exceeded = 1;
	}

	ts_query_cursor_delete(cursor);
	return 1;
}

static void editorSyntaxApplyInputEdit(TSTree *tree, const struct editorSyntaxEdit *edit) {
	if (tree == NULL || edit == NULL) {
		return;
	}
	TSInputEdit ts_edit = {
		.start_byte = edit->start_byte,
		.old_end_byte = edit->old_end_byte,
		.new_end_byte = edit->new_end_byte,
		.start_point = {.row = edit->start_point.row, .column = edit->start_point.column},
		.old_end_point = {.row = edit->old_end_point.row, .column = edit->old_end_point.column},
		.new_end_point = {.row = edit->new_end_point.row, .column = edit->new_end_point.column}
	};
	ts_tree_edit(tree, &ts_edit);
}

static int editorSyntaxStateParseInjections(struct editorSyntaxState *state,
		const struct editorTextSource *source,
		const struct editorSyntaxEdit *incremental_edit) {
	if (state == NULL || source == NULL) {
		return 0;
	}
	if (state->perf_disable_injections) {
		editorSyntaxStateMarkInjectionsInactive(state);
		return editorSyntaxStateResetInactiveInjections(state);
	}

	editorSyntaxStateApplyEditToInjections(state, incremental_edit);
	editorSyntaxStateMarkInjectionsInactive(state);

	struct editorSyntaxInjectionWork work = {0};
	int ok = 1;
	if (state->host.tree != NULL &&
			!editorSyntaxCollectInjectionRangesFromTree(state, state->host.tree,
				state->language, source, 1, &work)) {
		ok = 0;
	}
	if (work.slots_full) {
		editorSyntaxStateRecordInjectionSlotsFull(state, work.slots_full_language);
	}

	for (int work_idx = 0; ok && work_idx < work.count; work_idx++) {
		struct editorSyntaxInjectionWorkItem *item = &work.items[work_idx];
		if (item->depth > ROTIDE_SYNTAX_MAX_INJECTION_DEPTH) {
			editorSyntaxStateRecordInjectionDepthExceeded(state, item->language, item->depth);
			continue;
		}
		editorSyntaxRangeVecSortUnique(&item->ranges);
		if (item->ranges.count == 0) {
			continue;
		}

		struct editorSyntaxInjectedTree *injection =
				editorSyntaxStateFindInjection(state, item->language);
		if (injection == NULL &&
				state->injection_count >= ROTIDE_SYNTAX_MAX_INJECTION_TREES) {
			editorSyntaxStateRecordInjectionSlotsFull(state, item->language);
			continue;
		}
		if (injection == NULL) {
			injection = editorSyntaxStateEnsureInjection(state, item->language);
		}
		if (injection == NULL) {
			continue;
		}
		int incremental = incremental_edit != NULL && injection->parsed.tree != NULL;
		if (!editorSyntaxParsedTreeSetIncludedRanges(&injection->parsed, item->ranges.items,
					item->ranges.count) ||
				!editorSyntaxParsedTreeParse(&injection->parsed, NULL, source, incremental)) {
			ok = 0;
			break;
		}
		injection->active = 1;
		injection->depth = item->depth;
		injection->locals_valid = 0;

		if (item->depth < ROTIDE_SYNTAX_MAX_INJECTION_DEPTH &&
				editorSyntaxLanguageHasInjectionQuery(item->language) &&
				!editorSyntaxCollectInjectionRangesFromTree(state, injection->parsed.tree,
					item->language, source, item->depth + 1, &work)) {
			ok = 0;
			break;
		}
		if (work.slots_full) {
			editorSyntaxStateRecordInjectionSlotsFull(state, work.slots_full_language);
		}
	}

	if (ok && !editorSyntaxStateResetInactiveInjections(state)) {
		ok = 0;
	}
	editorSyntaxInjectionWorkFree(&work);
	return ok;
}

static void editorSyntaxStateInvalidateLocalsCaches(struct editorSyntaxState *state) {
	if (state == NULL) {
		return;
	}
	state->host_locals_valid = 0;
	for (int i = 0; i < state->injection_count; i++) {
		state->injections[i].locals_valid = 0;
	}
}

static int editorSyntaxStateEnsureLocalsCached(
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

static int editorSyntaxLanguageHasLocalsQuery(enum editorSyntaxLanguage language) {
	const struct editorSyntaxLanguageDef *def = editorSyntaxLookupLanguage(language);
	return def != NULL && def->locals_parts != NULL && def->locals_part_count > 0;
}

static void editorSyntaxStateApplyPerformanceMode(struct editorSyntaxState *state,
		size_t source_len) {
	if (state == NULL) {
		return;
	}

	enum editorSyntaxPerformanceMode mode = EDITOR_SYNTAX_PERF_NORMAL;
	int disable_predicates = 0;
	int disable_injections = 0;

	if (source_len > ROTIDE_SYNTAX_PERF_DEGRADED_INJECTIONS_BYTES) {
		mode = EDITOR_SYNTAX_PERF_DEGRADED_INJECTIONS;
		disable_predicates = 1;
		disable_injections = 1;
	} else if (source_len > ROTIDE_SYNTAX_PERF_DEGRADED_PREDICATES_BYTES) {
		mode = EDITOR_SYNTAX_PERF_DEGRADED_PREDICATES;
		disable_predicates = 1;
	}

	if (state->perf_disable_predicates != disable_predicates) {
		editorSyntaxStateInvalidateLocalsCaches(state);
	}
	state->perf_disable_predicates = disable_predicates;
	state->perf_disable_injections = disable_injections;
	state->perf_mode = mode;
}

int editorSyntaxStateConfigureForSourceLength(struct editorSyntaxState *state, size_t source_len) {
	if (state == NULL) {
		return 0;
	}
	editorSyntaxStateApplyPerformanceMode(state, source_len);
	return editorSyntaxLengthFitsTreeSitter(source_len);
}

enum editorSyntaxPerformanceMode editorSyntaxStatePerformanceMode(
		const struct editorSyntaxState *state) {
	if (state == NULL) {
		return EDITOR_SYNTAX_PERF_NORMAL;
	}
	return state->perf_mode;
}

size_t editorSyntaxStateSourceLength(const struct editorSyntaxState *state) {
	if (state == NULL) {
		return 0;
	}
	return state->source_len;
}

struct editorSyntaxState *editorSyntaxStateCreate(enum editorSyntaxLanguage language) {
	const TSLanguage *host_language = editorSyntaxLanguageObject(language);
	if (host_language == NULL) {
		return NULL;
	}
	(void)host_language;

	struct editorSyntaxState *state = malloc(sizeof(*state));
	if (state == NULL) {
		return NULL;
	}
	state->language = language;
	editorSyntaxParsedTreeInit(&state->host, language);
	editorSyntaxLocalsContextInit(&state->host_locals);
	for (int i = 0; i < ROTIDE_SYNTAX_MAX_INJECTION_TREES; i++) {
		editorSyntaxInjectedTreeInit(&state->injections[i]);
	}
	state->injection_count = 0;
	state->host_locals_revision = 0;
	state->host_locals_valid = 0;
	state->perf_disable_predicates = 0;
	state->perf_disable_injections = 0;
	state->perf_mode = EDITOR_SYNTAX_PERF_NORMAL;
	state->last_changed_ranges = NULL;
	state->last_changed_range_count = 0;
	state->last_changed_range_cap = 0;
	state->budget_parse_exceeded = 0;
	state->budget_query_exceeded = 0;
	state->query_unavailable_pending = 0;
	state->query_unavailable_language = EDITOR_SYNTAX_NONE;
	state->query_unavailable_kind = EDITOR_SYNTAX_QUERY_KIND_HIGHLIGHT;
	state->limit_event_start = 0;
	state->limit_event_count = 0;
	state->injection_depth_exceeded_reported = 0;
	state->injection_slots_full_reported = 0;
	state->capture_truncated_unknown_reported = 0;
	state->capture_truncated_rows = NULL;
	state->capture_truncated_row_count = 0;
	state->capture_truncated_row_cap = 0;
	state->source_len = 0;
	state->scratch_primary = NULL;
	state->scratch_primary_cap = 0;
	state->scratch_secondary = NULL;
	state->scratch_secondary_cap = 0;

	if (!editorSyntaxParsedTreeCreateParser(&state->host, language)) {
		free(state);
		return NULL;
	}

	return state;
}

void editorSyntaxStateDestroy(struct editorSyntaxState *state) {
	if (state == NULL) {
		return;
	}
	editorSyntaxParsedTreeDestroy(&state->host);
	editorSyntaxLocalsContextFree(&state->host_locals);
	for (int i = 0; i < ROTIDE_SYNTAX_MAX_INJECTION_TREES; i++) {
		editorSyntaxInjectedTreeDestroy(&state->injections[i]);
	}
	state->injection_count = 0;
	free(state->last_changed_ranges);
	state->last_changed_ranges = NULL;
	state->last_changed_range_count = 0;
	state->last_changed_range_cap = 0;
	free(state->capture_truncated_rows);
	state->capture_truncated_rows = NULL;
	state->capture_truncated_row_count = 0;
	state->capture_truncated_row_cap = 0;
	free(state->scratch_primary);
	state->scratch_primary = NULL;
	state->scratch_primary_cap = 0;
	free(state->scratch_secondary);
	state->scratch_secondary = NULL;
	state->scratch_secondary_cap = 0;
	state->source_len = 0;
	free(state);
}

int editorSyntaxStateParseFull(struct editorSyntaxState *state,
		const struct editorTextSource *source) {
	if (state == NULL || source == NULL || source->read == NULL ||
			!editorSyntaxLengthFitsTreeSitter(source->length)) {
		return 0;
	}
	state->budget_parse_exceeded = 0;
	state->budget_query_exceeded = 0;
	editorSyntaxStateClearChangedRanges(state);
	editorSyntaxStateApplyPerformanceMode(state, source->length);

	if (!editorSyntaxParsedTreeParse(&state->host, state, source, 0)) {
		return 0;
	}
	if (!editorSyntaxStateParseInjections(state, source, NULL)) {
		return 0;
	}
	state->source_len = source->length;
	if (!editorSyntaxStateSetChangedRangesFull(state, source->length)) {
		return 0;
	}
	return 1;
}

int editorSyntaxStateApplyEditAndParse(struct editorSyntaxState *state,
		const struct editorSyntaxEdit *edit,
		const struct editorTextSource *source) {
	if (state == NULL || edit == NULL || source == NULL || source->read == NULL ||
			!editorSyntaxLengthFitsTreeSitter(source->length) ||
			state->host.parser == NULL || state->host.tree == NULL) {
		return 0;
	}
	state->budget_parse_exceeded = 0;
	state->budget_query_exceeded = 0;
	editorSyntaxStateClearChangedRanges(state);
	if ((size_t)edit->old_end_byte > state->source_len || edit->old_end_byte < edit->start_byte) {
		return 0;
	}
	editorSyntaxStateApplyPerformanceMode(state, source->length);

	editorSyntaxApplyInputEdit(state->host.tree, edit);
	if (!editorSyntaxParsedTreeParse(&state->host, state, source, 1)) {
		return 0;
	}
	if (!editorSyntaxStateParseInjections(state, source, edit)) {
		return 0;
	}
	state->source_len = source->length;
	return 1;
}

int editorSyntaxStateHasTree(const struct editorSyntaxState *state) {
	return state != NULL && state->host.tree != NULL;
}

const char *editorSyntaxStateRootType(const struct editorSyntaxState *state) {
	if (state == NULL || state->host.tree == NULL) {
		return NULL;
	}
	TSNode root = ts_tree_root_node(state->host.tree);
	return ts_node_type(root);
}

enum editorSyntaxLanguage editorSyntaxStateLanguage(const struct editorSyntaxState *state) {
	if (state == NULL) {
		return EDITOR_SYNTAX_NONE;
	}
	return state->language;
}

static int editorSyntaxCaptureSortKeyCmp(const struct editorSyntaxCapture *left,
		const struct editorSyntaxCapture *right) {
	if (left->start_byte < right->start_byte) {
		return -1;
	}
	if (left->start_byte > right->start_byte) {
		return 1;
	}
	if (left->end_byte < right->end_byte) {
		return -1;
	}
	if (left->end_byte > right->end_byte) {
		return 1;
	}
	return 0;
}

int editorSyntaxStateCollectCapturesForRange(struct editorSyntaxState *state,
		const struct editorTextSource *source,
		uint32_t start_byte,
		uint32_t end_byte,
		struct editorSyntaxCapture *captures,
		int max_captures,
		int *count_out) {
	if (count_out != NULL) {
		*count_out = 0;
	}
	if (state == NULL || source == NULL || source->read == NULL || start_byte >= end_byte ||
			max_captures < 0 ||
			(max_captures > 0 && captures == NULL)) {
		return 0;
	}
	if (max_captures == 0 || state->host.tree == NULL) {
		return 1;
	}
	int skip_predicates = state->perf_disable_predicates;

	struct editorSyntaxCaptureVec capture_vecs[1 + ROTIDE_SYNTAX_MAX_INJECTION_TREES] = {0};
	int capture_vec_count = 1;
	const struct editorSyntaxLocalsContext *host_locals = NULL;
	if (!skip_predicates && editorSyntaxLanguageHasLocalsQuery(state->language) &&
			!editorSyntaxStateEnsureLocalsCached(state, &state->host, source,
					state->language, NULL, &host_locals)) {
		return 0;
	}

	int query_unavailable = 0;
	int ok = editorSyntaxCollectCapturesFromTree(state, state->host.tree, state->language,
			source, start_byte, end_byte, host_locals, skip_predicates, &capture_vecs[0],
			&query_unavailable);
	if (!ok) {
		if (query_unavailable) {
			editorSyntaxStateRecordQueryUnavailable(state, state->language,
					EDITOR_SYNTAX_QUERY_KIND_HIGHLIGHT);
			ok = 1;
		} else {
			editorSyntaxCaptureVecFree(&capture_vecs[0]);
			return 0;
		}
	}

	if (!state->perf_disable_injections) {
		for (int i = 0; i < state->injection_count &&
				capture_vec_count < (int)(sizeof(capture_vecs) / sizeof(capture_vecs[0]));
				i++) {
			struct editorSyntaxInjectedTree *injection = &state->injections[i];
			if (!injection->active || injection->parsed.tree == NULL) {
				continue;
			}
			const struct editorSyntaxLocalsContext *injection_locals = NULL;
			if (!skip_predicates && editorSyntaxLanguageHasLocalsQuery(injection->parsed.language) &&
					!editorSyntaxStateEnsureLocalsCached(state, &injection->parsed,
						source, injection->parsed.language, injection, &injection_locals)) {
				ok = 0;
				break;
			}
			int vec_idx = capture_vec_count;
			query_unavailable = 0;
			if (!editorSyntaxCollectCapturesFromTree(state, injection->parsed.tree,
						injection->parsed.language, source, start_byte, end_byte,
						injection_locals, skip_predicates,
						&capture_vecs[vec_idx], &query_unavailable)) {
				if (query_unavailable) {
					editorSyntaxStateRecordQueryUnavailable(state, injection->parsed.language,
							EDITOR_SYNTAX_QUERY_KIND_HIGHLIGHT);
					continue;
				} else {
					editorSyntaxCaptureVecFree(&capture_vecs[vec_idx]);
					ok = 0;
					break;
				}
			}
			capture_vec_count++;
		}
	}
	if (!ok) {
		for (int i = 0; i < capture_vec_count; i++) {
			editorSyntaxCaptureVecFree(&capture_vecs[i]);
		}
		return 0;
	}

	int indices[1 + ROTIDE_SYNTAX_MAX_INJECTION_TREES] = {0};
	int out_count = 0;
	while (out_count < max_captures) {
		int source_choice = -1;
		const struct editorSyntaxCapture *choice = NULL;

		for (int vec_idx = 0; vec_idx < capture_vec_count; vec_idx++) {
			if (indices[vec_idx] >= capture_vecs[vec_idx].count) {
				continue;
			}
			const struct editorSyntaxCapture *candidate =
					&capture_vecs[vec_idx].items[indices[vec_idx]];
			int cmp = choice == NULL ? -1 :
					editorSyntaxCaptureSortKeyCmp(candidate, choice);
			if (choice == NULL || cmp < 0 || (cmp == 0 && vec_idx > source_choice)) {
				choice = candidate;
				source_choice = vec_idx;
			}
		}

		if (choice == NULL || source_choice < 0) {
			break;
		}
		captures[out_count++] = *choice;
		indices[source_choice]++;
	}
	if (out_count >= max_captures) {
		for (int vec_idx = 0; vec_idx < capture_vec_count; vec_idx++) {
			if (indices[vec_idx] < capture_vecs[vec_idx].count) {
				editorSyntaxStateRecordCaptureTruncated(state, -1);
				break;
			}
		}
	}

	if (count_out != NULL) {
		*count_out = out_count;
	}
	for (int i = 0; i < capture_vec_count; i++) {
		editorSyntaxCaptureVecFree(&capture_vecs[i]);
	}
	return 1;
}

int editorSyntaxStateCopyLastChangedRanges(const struct editorSyntaxState *state,
		struct editorSyntaxByteRange *ranges, int max_ranges, int *count_out) {
	if (count_out != NULL) {
		*count_out = 0;
	}
	if (state == NULL || max_ranges < 0 || (max_ranges > 0 && ranges == NULL)) {
		return 0;
	}

	int total_count = state->last_changed_range_count;
	int copy_count = total_count;
	if (copy_count > max_ranges) {
		copy_count = max_ranges;
	}
	for (int i = 0; i < copy_count; i++) {
		ranges[i] = state->last_changed_ranges[i];
	}
	if (count_out != NULL) {
		*count_out = total_count;
	}
	return 1;
}

int editorSyntaxStateConsumeBudgetEvents(struct editorSyntaxState *state,
		int *parse_budget_exceeded_out,
		int *query_budget_exceeded_out) {
	if (parse_budget_exceeded_out != NULL) {
		*parse_budget_exceeded_out = 0;
	}
	if (query_budget_exceeded_out != NULL) {
		*query_budget_exceeded_out = 0;
	}
	if (state == NULL) {
		return 0;
	}

	if (parse_budget_exceeded_out != NULL) {
		*parse_budget_exceeded_out = state->budget_parse_exceeded;
	}
	if (query_budget_exceeded_out != NULL) {
		*query_budget_exceeded_out = state->budget_query_exceeded;
	}

	int had = state->budget_parse_exceeded || state->budget_query_exceeded;
	state->budget_parse_exceeded = 0;
	state->budget_query_exceeded = 0;
	return had;
}

int editorSyntaxStateConsumeQueryUnavailableEvent(struct editorSyntaxState *state,
		enum editorSyntaxLanguage *language_out,
		enum editorSyntaxQueryKind *kind_out) {
	if (language_out != NULL) {
		*language_out = EDITOR_SYNTAX_NONE;
	}
	if (kind_out != NULL) {
		*kind_out = EDITOR_SYNTAX_QUERY_KIND_HIGHLIGHT;
	}
	if (state == NULL || !state->query_unavailable_pending) {
		return 0;
	}
	if (language_out != NULL) {
		*language_out = state->query_unavailable_language;
	}
	if (kind_out != NULL) {
		*kind_out = state->query_unavailable_kind;
	}
	state->query_unavailable_pending = 0;
	state->query_unavailable_language = EDITOR_SYNTAX_NONE;
	state->query_unavailable_kind = EDITOR_SYNTAX_QUERY_KIND_HIGHLIGHT;
	return 1;
}

int editorSyntaxStateConsumeLimitEvent(struct editorSyntaxState *state,
		struct editorSyntaxLimitEvent *event_out) {
	if (event_out != NULL) {
		event_out->kind = EDITOR_SYNTAX_LIMIT_EVENT_CAPTURE_TRUNCATED;
		event_out->language = EDITOR_SYNTAX_NONE;
		event_out->row = -1;
		event_out->detail = 0;
	}
	if (state == NULL || state->limit_event_count <= 0) {
		return 0;
	}

	int idx = state->limit_event_start;
	if (event_out != NULL) {
		*event_out = state->limit_events[idx];
	}
	state->limit_event_start = (state->limit_event_start + 1) %
			ROTIDE_SYNTAX_LIMIT_EVENT_CAP;
	state->limit_event_count--;
	return 1;
}

