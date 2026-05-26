/* Highlight-capture collection.
 *
 * Walks the parsed host tree (and active injection trees) with the
 * language's highlight query, gathers per-capture byte ranges and
 * highlight classes through the predicate evaluator, and merges the
 * results into the caller-provided buffer. Owns the in-memory capture
 * vector lifetime; produces output that the visible-cache module
 * consumes.
 */
#include "language/syntax.h"
#include "language/syntax_internal.h"
#include "rotide.h"
#include "tree_sitter/api.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int syntaxCapturesVecAppend(struct editorSyntaxCaptureVec *vec, uint32_t start_byte,
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

static void syntaxCapturesVecFree(struct editorSyntaxCaptureVec *vec) {
	if (vec == NULL) {
		return;
	}
	free(vec->items);
	vec->items = NULL;
	vec->count = 0;
	vec->cap = 0;
}

static int syntaxCapturesCollectFromTree(
        struct editorSyntaxState *state, const TSTree *tree, enum editorSyntaxLanguage language,
        const struct editorTextSource *source, uint32_t start_byte, uint32_t end_byte,
        const struct editorSyntaxLocalsContext *locals, int skip_predicates,
        struct editorSyntaxCaptureVec *captures_out, int *query_unavailable_out);

static int syntaxCapturesCollectFromTree(
        struct editorSyntaxState *state, const TSTree *tree, enum editorSyntaxLanguage language,
        const struct editorTextSource *source, uint32_t start_byte, uint32_t end_byte,
        const struct editorSyntaxLocalsContext *locals, int skip_predicates,
        struct editorSyntaxCaptureVec *captures_out, int *query_unavailable_out) {
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
	struct editorSyntaxBudgetConfig budget = editorSyntaxBudgetConfigForMode(
	        state != NULL ? state->perf_mode : EDITOR_SYNTAX_PERF_NORMAL);
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
	        .state = state, .source = source, .locals = locals};

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

		if (!syntaxCapturesVecAppend(captures_out, capture_start, capture_end,
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

static int syntaxCapturesSortKeyCmp(const struct editorSyntaxCapture *left,
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
                                             uint32_t start_byte, uint32_t end_byte,
                                             struct editorSyntaxCapture *captures, int max_captures,
                                             int *count_out) {
	if (count_out != NULL) {
		*count_out = 0;
	}
	if (state == NULL || source == NULL || source->read == NULL || start_byte >= end_byte ||
	    max_captures < 0 || (max_captures > 0 && captures == NULL)) {
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
	    !editorSyntaxStateEnsureLocalsCached(state, &state->host, source, state->language, NULL,
	                                         &host_locals)) {
		return 0;
	}

	int query_unavailable = 0;
	int ok = syntaxCapturesCollectFromTree(state, state->host.tree, state->language, source,
	                                       start_byte, end_byte, host_locals, skip_predicates,
	                                       &capture_vecs[0], &query_unavailable);
	if (!ok) {
		if (query_unavailable) {
			editorSyntaxStateRecordQueryUnavailable(state, state->language,
			                                        EDITOR_SYNTAX_QUERY_KIND_HIGHLIGHT);
			ok = 1;
		} else {
			syntaxCapturesVecFree(&capture_vecs[0]);
			return 0;
		}
	}

	if (!state->perf_disable_injections) {
		for (int i = 0;
		     i < state->injection_count &&
		     capture_vec_count < (int)(sizeof(capture_vecs) / sizeof(capture_vecs[0]));
		     i++) {
			struct editorSyntaxInjectedTree *injection = &state->injections[i];
			if (!injection->active || injection->parsed.tree == NULL) {
				continue;
			}
			const struct editorSyntaxLocalsContext *injection_locals = NULL;
			if (!skip_predicates &&
			    editorSyntaxLanguageHasLocalsQuery(injection->parsed.language) &&
			    !editorSyntaxStateEnsureLocalsCached(state, &injection->parsed, source,
			                                         injection->parsed.language,
			                                         injection, &injection_locals)) {
				ok = 0;
				break;
			}
			int vec_idx = capture_vec_count;
			query_unavailable = 0;
			if (!syntaxCapturesCollectFromTree(
			            state, injection->parsed.tree, injection->parsed.language,
			            source, start_byte, end_byte, injection_locals, skip_predicates,
			            &capture_vecs[vec_idx], &query_unavailable)) {
				if (query_unavailable) {
					editorSyntaxStateRecordQueryUnavailable(
					        state, injection->parsed.language,
					        EDITOR_SYNTAX_QUERY_KIND_HIGHLIGHT);
					continue;
				} else {
					syntaxCapturesVecFree(&capture_vecs[vec_idx]);
					ok = 0;
					break;
				}
			}
			capture_vec_count++;
		}
	}
	if (!ok) {
		for (int i = 0; i < capture_vec_count; i++) {
			syntaxCapturesVecFree(&capture_vecs[i]);
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
			int cmp = choice == NULL ? -1 : syntaxCapturesSortKeyCmp(candidate, choice);
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
		syntaxCapturesVecFree(&capture_vecs[i]);
	}
	return 1;
}
