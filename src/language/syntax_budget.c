/* Performance modes, limit-event recording, and budget reporting.
 *
 * Owns the queue of `editorSyntaxLimitEvent`s, the per-state perf
 * mode/source-length tracking, and the consume APIs that the editor
 * polls to surface "budget exceeded" / "capture truncated" / "injection
 * slots full" diagnostics. Pure state mutation; no Tree-sitter parsing
 * or query work happens here.
 */
#include "language/syntax_internal.h"
#include "language/syntax.h"
#include "rotide.h"

#include <stdlib.h>

static void syntaxBudgetQueueLimitEvent(struct editorSyntaxState *state,
                                        enum editorSyntaxLimitEventKind kind,
                                        enum editorSyntaxLanguage language, int row, int detail) {
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
		state->limit_event_start =
		        (state->limit_event_start + 1) % ROTIDE_SYNTAX_LIMIT_EVENT_CAP;
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
			int new_cap = state->capture_truncated_row_cap == 0
			                      ? 8
			                      : state->capture_truncated_row_cap * 2;
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
	syntaxBudgetQueueLimitEvent(state, EDITOR_SYNTAX_LIMIT_EVENT_CAPTURE_TRUNCATED,
	                            state->language, row, ROTIDE_MAX_SYNTAX_SPANS_PER_ROW);
}

void editorSyntaxStateRecordInjectionDepthExceeded(struct editorSyntaxState *state,
                                                   enum editorSyntaxLanguage language, int depth) {
	if (state == NULL || state->injection_depth_exceeded_reported) {
		return;
	}
	state->injection_depth_exceeded_reported = 1;
	syntaxBudgetQueueLimitEvent(state, EDITOR_SYNTAX_LIMIT_EVENT_INJECTION_DEPTH_EXCEEDED,
	                            language, -1, depth);
}

void editorSyntaxStateRecordInjectionSlotsFull(struct editorSyntaxState *state,
                                               enum editorSyntaxLanguage language) {
	if (state == NULL || state->injection_slots_full_reported) {
		return;
	}
	state->injection_slots_full_reported = 1;
	syntaxBudgetQueueLimitEvent(state, EDITOR_SYNTAX_LIMIT_EVENT_INJECTION_SLOTS_FULL, language,
	                            -1, ROTIDE_SYNTAX_MAX_INJECTION_TREES);
}

void editorSyntaxStateRecordParseFailed(struct editorSyntaxState *state, int consecutive_failures) {
	if (state == NULL) {
		return;
	}
	syntaxBudgetQueueLimitEvent(state, EDITOR_SYNTAX_LIMIT_EVENT_PARSE_FAILED, state->language,
	                            -1, consecutive_failures);
}

void editorSyntaxStateRecordParseTreeHasError(struct editorSyntaxState *state,
                                              enum editorSyntaxLanguage language) {
	if (state == NULL) {
		return;
	}
	syntaxBudgetQueueLimitEvent(state, EDITOR_SYNTAX_LIMIT_EVENT_PARSE_TREE_HAS_ERROR, language,
	                            -1, 1);
}

void editorSyntaxStateApplyPerformanceMode(struct editorSyntaxState *state, size_t source_len) {
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

enum editorSyntaxPerformanceMode
editorSyntaxStatePerformanceMode(const struct editorSyntaxState *state) {
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
	state->limit_event_start = (state->limit_event_start + 1) % ROTIDE_SYNTAX_LIMIT_EVENT_CAP;
	state->limit_event_count--;
	return 1;
}
