/* Language injections — secondary parse trees for nested content.
 *
 * Owns the injection-query traversal that turns `@injection.content`
 * captures into per-injected-language byte ranges, the work queue used
 * to deduplicate and sort those ranges, the per-state pool of
 * `editorSyntaxInjectedTree` slots, and the parse/edit pipeline that
 * keeps injected trees in sync with the host edit. Limit events
 * (depth-exceeded, slots-full) are emitted through syntax_budget.
 */
#include "language/syntax_internal.h"

#include "language/languages.h"

#include "tree_sitter/api.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void editorSyntaxParsedTreeResetTree(struct editorSyntaxParsedTree *parsed) {
	if (parsed == NULL || parsed->tree == NULL) {
		return;
	}
	ts_tree_delete(parsed->tree);
	parsed->tree = NULL;
	parsed->revision++;
	parsed->tree_error_reported = 0;
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

	/* Only named children are semantic injection-content boundaries.
	 * Anonymous tokens (e.g. literal backticks inside markdown's (inline)
	 * node, or quote characters around a string body) are part of the
	 * content the injected grammar needs to see — excluding them
	 * fragments the included range and breaks injection parsers that
	 * depend on seeing those delimiters. */
	uint32_t child_count = ts_node_named_child_count(node);
	if (child_count == 0) {
		return editorSyntaxInjectionWorkAppendRange(work, language, depth, range);
	}

	uint32_t segment_start_byte = range->start_byte;
	TSPoint segment_start_point = range->start_point;
	for (uint32_t child_idx = 0; child_idx < child_count; child_idx++) {
		TSNode child = ts_node_named_child(node, child_idx);
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

static int editorSyntaxRangeExtendTrailingNewline(const struct editorTextSource *source,
		TSRange *range) {
	if (source == NULL || source->read == NULL || range == NULL ||
			range->end_byte >= source->length) {
		return 1;
	}

	uint32_t bytes_read = 0;
	const char *chunk = source->read(source, range->end_byte, &bytes_read);
	if (chunk == NULL || bytes_read == 0) {
		return 0;
	}
	if (chunk[0] != '\n') {
		return 1;
	}

	range->end_byte++;
	range->end_point.row++;
	range->end_point.column = 0;
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
			if (target_lang == EDITOR_SYNTAX_MARKDOWN_INLINE &&
					!editorSyntaxRangeExtendTrailingNewline(source, &range)) {
				ts_query_cursor_delete(cursor);
				return 0;
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

void editorSyntaxApplyInputEdit(TSTree *tree, const struct editorSyntaxEdit *edit) {
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

int editorSyntaxStateParseInjections(struct editorSyntaxState *state,
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
		if (item->depth > g_editor_syntax_max_injection_depth) {
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

		if (item->depth <= g_editor_syntax_max_injection_depth &&
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
