#include "language/syntax.h"

#include "language/languages.h"
#include "language/syntax_internal.h"
#include "tree_sitter/api.h"

#include <ctype.h>
#include <limits.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

int g_editor_syntax_max_injection_depth = ROTIDE_SYNTAX_DEFAULT_MAX_INJECTION_DEPTH;

static int g_syntax_test_full_parse_failures = 0;
static int g_syntax_test_incremental_parse_failures = 0;

static int syntaxTestConsumeParseFailure(int incremental) {
	int *failures = incremental ? &g_syntax_test_incremental_parse_failures
	                            : &g_syntax_test_full_parse_failures;
	if (*failures <= 0) {
		return 0;
	}
	(*failures)--;
	return 1;
}

void editorSyntaxParsedTreeInit(struct editorSyntaxParsedTree *parsed,
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
	parsed->tree_error_reported = 0;
}

int editorSyntaxParsedTreeCreateParser(struct editorSyntaxParsedTree *parsed,
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

void editorSyntaxParsedTreeDestroy(struct editorSyntaxParsedTree *parsed) {
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
	parsed->tree_error_reported = 0;
}

void editorSyntaxInjectedTreeInit(struct editorSyntaxInjectedTree *injection) {
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

void editorSyntaxInjectedTreeDestroy(struct editorSyntaxInjectedTree *injection) {
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

static void syntaxStateClearChangedRanges(struct editorSyntaxState *state) {
	if (state == NULL) {
		return;
	}
	state->last_changed_range_count = 0;
}

static int syntaxStateEnsureChangedRangeCapacity(struct editorSyntaxState *state, int needed) {
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

static int syntaxStateAppendChangedRange(struct editorSyntaxState *state, uint32_t start_byte,
                                         uint32_t end_byte) {
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

	if (!syntaxStateEnsureChangedRangeCapacity(state, state->last_changed_range_count + 1)) {
		return 0;
	}
	state->last_changed_ranges[state->last_changed_range_count].start_byte = start_byte;
	state->last_changed_ranges[state->last_changed_range_count].end_byte = end_byte;
	state->last_changed_range_count++;
	return 1;
}

static int syntaxStateSetChangedRangesFull(struct editorSyntaxState *state, size_t source_len) {
	if (state == NULL) {
		return 0;
	}
	syntaxStateClearChangedRanges(state);
	if (source_len == 0) {
		return 1;
	}
	if (source_len > UINT32_MAX) {
		source_len = UINT32_MAX;
	}
	return syntaxStateAppendChangedRange(state, 0, (uint32_t)source_len);
}

static int syntaxStateSetChangedRangesFromTrees(struct editorSyntaxState *state,
                                                const TSTree *old_tree, const TSTree *new_tree) {
	if (state == NULL) {
		return 0;
	}
	syntaxStateClearChangedRanges(state);
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
		if (!syntaxStateAppendChangedRange(state, ranges[i].start_byte,
		                                   ranges[i].end_byte)) {
			ok = 0;
			break;
		}
	}
	free(ranges);
	return ok;
}

int editorSyntaxParsedTreeParse(struct editorSyntaxParsedTree *parsed,
                                struct editorSyntaxState *state,
                                const struct editorTextSource *source, int incremental) {
	if (parsed == NULL || parsed->parser == NULL || source == NULL || source->read == NULL ||
	    !editorSyntaxLengthFitsTreeSitter(source->length)) {
		return 0;
	}

	TSTree *old_tree = incremental ? parsed->tree : NULL;
	struct editorSyntaxBudgetConfig budget = editorSyntaxBudgetConfigForMode(
	        state != NULL ? state->perf_mode : EDITOR_SYNTAX_PERF_NORMAL);
	TSInput input = {.payload = (void *)source,
	                 .read = editorSyntaxSourceRead,
	                 .encoding = TSInputEncodingUTF8,
	                 .decode = NULL};

	TSTree *new_tree = NULL;
	if (budget.parse_budget_ns > 0) {
		struct editorSyntaxDeadlineContext parse_deadline = {0};
		parse_deadline.deadline_ns = editorSyntaxComputeDeadlineNs(budget.parse_budget_ns);
		TSParseOptions options = {.payload = &parse_deadline,
		                          .progress_callback = editorSyntaxParseProgressCallback};
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
		if (!syntaxStateSetChangedRangesFromTrees(state, old_tree, new_tree)) {
			ts_tree_delete(new_tree);
			return 0;
		}
	}

	if (parsed->tree != NULL) {
		ts_tree_delete(parsed->tree);
	}
	parsed->tree = new_tree;
	parsed->revision++;
	TSNode root = ts_tree_root_node(parsed->tree);
	if (ts_node_has_error(root)) {
		if (!parsed->tree_error_reported) {
			editorSyntaxStateRecordParseTreeHasError(state, parsed->language);
			parsed->tree_error_reported = 1;
		}
	} else {
		parsed->tree_error_reported = 0;
	}
	return 1;
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
	if (syntaxTestConsumeParseFailure(0)) {
		return 0;
	}
	state->budget_parse_exceeded = 0;
	state->budget_query_exceeded = 0;
	syntaxStateClearChangedRanges(state);
	editorSyntaxStateApplyPerformanceMode(state, source->length);

	if (!editorSyntaxParsedTreeParse(&state->host, state, source, 0)) {
		return 0;
	}
	if (!editorSyntaxStateParseInjections(state, source, NULL)) {
		return 0;
	}
	state->source_len = source->length;
	if (!syntaxStateSetChangedRangesFull(state, source->length)) {
		return 0;
	}
	return 1;
}

int editorSyntaxStateApplyEditAndParse(struct editorSyntaxState *state,
                                       const struct editorSyntaxEdit *edit,
                                       const struct editorTextSource *source) {
	if (state == NULL || edit == NULL || source == NULL || source->read == NULL ||
	    !editorSyntaxLengthFitsTreeSitter(source->length) || state->host.parser == NULL ||
	    state->host.tree == NULL) {
		return 0;
	}
	if (syntaxTestConsumeParseFailure(1)) {
		return 0;
	}
	state->budget_parse_exceeded = 0;
	state->budget_query_exceeded = 0;
	syntaxStateClearChangedRanges(state);
	if ((size_t)edit->old_end_byte > state->source_len ||
	    edit->old_end_byte < edit->start_byte) {
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

int editorSyntaxStateHasError(const struct editorSyntaxState *state) {
	if (state == NULL || state->host.tree == NULL) {
		return 0;
	}
	return ts_node_has_error(ts_tree_root_node(state->host.tree));
}

static int syntaxFirstErrorNode(TSNode node, TSNode *error_out) {
	if (!ts_node_has_error(node)) {
		return 0;
	}
	if (ts_node_is_error(node) || ts_node_is_missing(node)) {
		*error_out = node;
		return 1;
	}

	uint32_t child_count = ts_node_child_count(node);
	for (uint32_t i = 0; i < child_count; i++) {
		if (syntaxFirstErrorNode(ts_node_child(node, i), error_out)) {
			return 1;
		}
	}

	return 0;
}

int editorSyntaxStateFirstErrorPosition(const struct editorSyntaxState *state, int *row_out,
                                        int *column_out) {
	if (row_out != NULL) {
		*row_out = 0;
	}
	if (column_out != NULL) {
		*column_out = 0;
	}
	if (state == NULL || state->host.tree == NULL) {
		return 0;
	}

	TSNode root = ts_tree_root_node(state->host.tree);
	TSNode error = {0};
	if (!syntaxFirstErrorNode(root, &error)) {
		return 0;
	}

	TSPoint point = ts_node_start_point(error);
	if (row_out != NULL) {
		*row_out = (int)point.row;
	}
	if (column_out != NULL) {
		*column_out = (int)point.column;
	}
	return 1;
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

int editorSyntaxStateCopyLastChangedRanges(const struct editorSyntaxState *state,
                                           struct editorSyntaxByteRange *ranges, int max_ranges,
                                           int *count_out) {
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

void editorSyntaxTestSetParseFailureCountdowns(int full_parse_failures,
                                               int incremental_parse_failures) {
	g_syntax_test_full_parse_failures = full_parse_failures > 0 ? full_parse_failures : 0;
	g_syntax_test_incremental_parse_failures =
	        incremental_parse_failures > 0 ? incremental_parse_failures : 0;
}

void editorSyntaxTestResetParseFailureCountdowns(void) {
	editorSyntaxTestSetParseFailureCountdowns(0, 0);
}

void editorSyntaxTestSetMaxInjectionDepth(int depth) {
	g_editor_syntax_max_injection_depth =
	        depth > 0 ? depth : ROTIDE_SYNTAX_DEFAULT_MAX_INJECTION_DEPTH;
}

void editorSyntaxTestResetMaxInjectionDepth(void) {
	g_editor_syntax_max_injection_depth = ROTIDE_SYNTAX_DEFAULT_MAX_INJECTION_DEPTH;
}
