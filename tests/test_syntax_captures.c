#include "test_case.h"
#include "test_helpers.h"
#include "test_support.h"

static char *build_dense_c_expression_source(int terms, size_t *len_out) {
	if (terms <= 0 || len_out == NULL) {
		return NULL;
	}

	const char *prefix = "int value = ";
	const char *suffix = ";\n";
	size_t cap = strlen(prefix) + strlen(suffix) + (size_t)terms * 16 + 1;
	char *source = malloc(cap);
	if (source == NULL) {
		return NULL;
	}

	size_t used = 0;
	int written = snprintf(source + used, cap - used, "%s", prefix);
	if (written < 0 || (size_t)written >= cap - used) {
		free(source);
		return NULL;
	}
	used += (size_t)written;
	for (int i = 0; i < terms; i++) {
		written = snprintf(source + used, cap - used, "%s%d", i == 0 ? "" : " + ", i);
		if (written < 0 || (size_t)written >= cap - used) {
			free(source);
			return NULL;
		}
		used += (size_t)written;
	}
	written = snprintf(source + used, cap - used, "%s", suffix);
	if (written < 0 || (size_t)written >= cap - used) {
		free(source);
		return NULL;
	}
	used += (size_t)written;
	*len_out = used;
	return source;
}

static int test_editor_syntax_query_budget_match_limit_is_graceful(void) {
	size_t source_len = 0;
	char *source = build_repeated_text("const value = document + window;\n", 512, &source_len);
	ASSERT_TRUE(source != NULL);
	ASSERT_TRUE(source_len <= UINT32_MAX);
	struct editorTextSource source_view = {0};
	editorTextSourceInitString(&source_view, source, source_len);

	editorSyntaxTestSetBudgetOverrides(1, 1, 0, 2000000000ULL);
	struct editorSyntaxState *state = editorSyntaxStateCreate(EDITOR_SYNTAX_JAVASCRIPT);
	ASSERT_TRUE(state != NULL);
	ASSERT_TRUE(editorSyntaxStateParseFull(state, &source_view));

	int parse_budget = 0;
	int query_budget = 0;
	(void)editorSyntaxStateConsumeBudgetEvents(state, &parse_budget, &query_budget);

	struct editorSyntaxCapture captures[1024];
	int capture_count = 0;
	ASSERT_TRUE(editorSyntaxStateCollectCapturesForRange(
	        state, &source_view, 0, (uint32_t)source_len, captures,
	        (int)(sizeof(captures) / sizeof(captures[0])), &capture_count));
	ASSERT_TRUE(editorSyntaxStateConsumeBudgetEvents(state, &parse_budget, &query_budget));
	ASSERT_EQ_INT(0, parse_budget);
	ASSERT_EQ_INT(1, query_budget);

	editorSyntaxStateDestroy(state);
	editorSyntaxTestResetBudgetOverrides();
	free(source);
	return 0;
}

static int test_editor_syntax_query_compile_failure_records_diagnostics(void) {
	const char *broken_query = "((identifier) @name";
	editorSyntaxTestResetLastQueryCompileError();

	ASSERT_EQ_INT(0, editorSyntaxTestCompileQueryForDiagnostics(EDITOR_SYNTAX_C, broken_query));

	struct editorSyntaxQueryCompileError error = {0};
	ASSERT_TRUE(editorSyntaxCopyLastQueryCompileError(&error));
	ASSERT_EQ_INT(1, error.has_error);
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, error.language);
	ASSERT_TRUE(error.error_type != 0);
	ASSERT_TRUE(error.error_offset <= strlen(broken_query));
	ASSERT_TRUE(strstr(error.context, "identifier") != NULL);

	struct editorSyntaxQueryCompileError drained = {0};
	ASSERT_TRUE(editorSyntaxDrainLastQueryCompileError(&drained));
	ASSERT_EQ_INT(error.language, drained.language);
	ASSERT_EQ_INT(error.error_type, drained.error_type);
	ASSERT_EQ_INT(error.error_offset, drained.error_offset);
	ASSERT_EQ_STR(error.context, drained.context);
	ASSERT_TRUE(!editorSyntaxDrainLastQueryCompileError(NULL));

	editorSyntaxTestResetLastQueryCompileError();
	return 0;
}

static int test_editor_syntax_capture_rules_are_longest_match_first(void) {
	int rule_count = editorSyntaxTestCaptureRuleCount();
	ASSERT_TRUE(rule_count > 0);

	size_t previous_len = (size_t)-1;
	for (int i = 0; i < rule_count; i++) {
		const char *prefix = NULL;
		enum editorSyntaxHighlightClass highlight_class = EDITOR_SYNTAX_HL_NONE;
		ASSERT_TRUE(editorSyntaxTestCaptureRuleAt(i, &prefix, &highlight_class));
		ASSERT_TRUE(prefix != NULL);
		ASSERT_TRUE(prefix[0] != '\0');
		size_t prefix_len = strlen(prefix);
		ASSERT_TRUE(prefix_len <= previous_len);
		ASSERT_TRUE(highlight_class > EDITOR_SYNTAX_HL_NONE);
		ASSERT_TRUE(highlight_class < EDITOR_SYNTAX_HL_CLASS_COUNT);
		ASSERT_EQ_INT(highlight_class, editorSyntaxTestClassFromCaptureName(prefix));

		char nested_capture[128];
		int written = snprintf(nested_capture, sizeof(nested_capture), "%s.extra", prefix);
		ASSERT_TRUE(written > 0);
		ASSERT_TRUE((size_t)written < sizeof(nested_capture));
		ASSERT_EQ_INT(highlight_class,
		              editorSyntaxTestClassFromCaptureName(nested_capture));

		previous_len = prefix_len;
	}

	ASSERT_EQ_INT(EDITOR_SYNTAX_HL_PARAMETER,
	              editorSyntaxTestClassFromCaptureName("variable.parameter"));
	ASSERT_EQ_INT(EDITOR_SYNTAX_HL_PROPERTY,
	              editorSyntaxTestClassFromCaptureName("variable.member"));
	ASSERT_EQ_INT(EDITOR_SYNTAX_HL_VARIABLE, editorSyntaxTestClassFromCaptureName("variable"));
	ASSERT_EQ_INT(EDITOR_SYNTAX_HL_MODULE, editorSyntaxTestClassFromCaptureName("namespace"));
	ASSERT_EQ_INT(EDITOR_SYNTAX_HL_PROPERTY, editorSyntaxTestClassFromCaptureName("property"));
	ASSERT_EQ_INT(EDITOR_SYNTAX_HL_CONSTANT, editorSyntaxTestClassFromCaptureName("constant"));
	ASSERT_EQ_INT(EDITOR_SYNTAX_HL_NONE, editorSyntaxTestClassFromCaptureName("none"));
	return 0;
}

static int test_editor_syntax_collect_c_captures_without_injection_query_is_graceful(void) {
	const char *source = "int value = 1;\n";
	struct editorTextSource source_view = {0};
	editorTextSourceInitString(&source_view, source, strlen(source));

	struct editorSyntaxState *state = editorSyntaxStateCreate(EDITOR_SYNTAX_C);
	ASSERT_TRUE(state != NULL);
	ASSERT_TRUE(editorSyntaxStateParseFull(state, &source_view));

	struct editorSyntaxCapture captures[32];
	int capture_count = 0;
	ASSERT_TRUE(editorSyntaxStateCollectCapturesForRange(
	        state, &source_view, 0, (uint32_t)strlen(source), captures,
	        (int)(sizeof(captures) / sizeof(captures[0])), &capture_count));
	ASSERT_TRUE(capture_count > 0);
	ASSERT_TRUE(!editorSyntaxStateConsumeQueryUnavailableEvent(state, NULL, NULL));

	editorSyntaxStateDestroy(state);
	return 0;
}

static int test_editor_syntax_capture_truncation_reports_event_and_keeps_span_limit(void) {
	size_t source_len = 0;
	char *source = build_dense_c_expression_source(300, &source_len);
	ASSERT_TRUE(source != NULL);
	ASSERT_TRUE(source_len <= UINT32_MAX);

	char path[512];
	ASSERT_TRUE(write_temp_c_file(path, sizeof(path), source));
	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());

	struct editorRowSyntaxSpan spans[ROTIDE_MAX_SYNTAX_SPANS_PER_ROW];
	int span_count = 0;
	ASSERT_TRUE(editorSyntaxRowRenderSpans(0, spans, (int)(sizeof(spans) / sizeof(spans[0])),
	                                       &span_count));
	ASSERT_EQ_INT(ROTIDE_MAX_SYNTAX_SPANS_PER_ROW, span_count);

	for (int i = 0; i < span_count; i++) {
		ASSERT_TRUE(spans[i].end_render_idx > spans[i].start_render_idx);
		ASSERT_TRUE(spans[i].highlight_class != EDITOR_SYNTAX_HL_NONE);
	}

	struct editorSyntaxLimitEvent event = {0};
	ASSERT_TRUE(editorSyntaxStateConsumeLimitEvent(E.syntax_state, &event));
	ASSERT_EQ_INT(EDITOR_SYNTAX_LIMIT_EVENT_CAPTURE_TRUNCATED, event.kind);
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, event.language);
	ASSERT_EQ_INT(-1, event.row);
	ASSERT_EQ_INT(ROTIDE_MAX_SYNTAX_SPANS_PER_ROW, event.detail);

	E.window_rows = 4;
	E.window_cols = 120;
	E.rowoff = 0;
	E.coloff = 0;
	ASSERT_TRUE(editorSyntaxPrepareVisibleRowSpans(E.rowoff, E.window_rows));
	ASSERT_TRUE(strstr(E.statusmsg, "Tree-sitter syntax spans truncated") != NULL);

	span_count = 0;
	ASSERT_TRUE(editorSyntaxRowRenderSpans(0, spans, (int)(sizeof(spans) / sizeof(spans[0])),
	                                       &span_count));
	ASSERT_EQ_INT(ROTIDE_MAX_SYNTAX_SPANS_PER_ROW, span_count);

	ASSERT_TRUE(unlink(path) == 0);
	free(source);
	return 0;
}

static int test_editor_syntax_parse_tree_errors_report_event_and_status(void) {
	const char *broken_source = "int main( {\n";
	struct editorTextSource broken_view = {0};
	editorTextSourceInitString(&broken_view, broken_source, strlen(broken_source));

	struct editorSyntaxState *state = editorSyntaxStateCreate(EDITOR_SYNTAX_C);
	ASSERT_TRUE(state != NULL);
	ASSERT_TRUE(editorSyntaxStateParseFull(state, &broken_view));

	struct editorSyntaxLimitEvent event = {0};
	ASSERT_TRUE(editorSyntaxStateConsumeLimitEvent(state, &event));
	ASSERT_EQ_INT(EDITOR_SYNTAX_LIMIT_EVENT_PARSE_TREE_HAS_ERROR, event.kind);
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, event.language);
	ASSERT_EQ_INT(-1, event.row);
	ASSERT_EQ_INT(1, event.detail);
	ASSERT_TRUE(!editorSyntaxStateConsumeLimitEvent(state, NULL));

	const char *valid_source = "int main(void) { return 0; }\n";
	struct editorTextSource valid_view = {0};
	editorTextSourceInitString(&valid_view, valid_source, strlen(valid_source));
	ASSERT_TRUE(editorSyntaxStateParseFull(state, &valid_view));
	ASSERT_TRUE(!editorSyntaxStateConsumeLimitEvent(state, NULL));

	ASSERT_TRUE(editorSyntaxStateParseFull(state, &broken_view));
	ASSERT_TRUE(editorSyntaxStateConsumeLimitEvent(state, &event));
	ASSERT_EQ_INT(EDITOR_SYNTAX_LIMIT_EVENT_PARSE_TREE_HAS_ERROR, event.kind);
	editorSyntaxStateDestroy(state);

	char path[512];
	ASSERT_TRUE(write_temp_c_file(path, sizeof(path), broken_source));
	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_TRUE(strstr(E.statusmsg, "Tree-sitter parse tree has errors") != NULL);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_injection_depth_limit_reports_event(void) {
	editorSyntaxTestSetMaxInjectionDepth(3);

	const char *source = "const nested = cpp`const char *page = "
	                     "R\"html(<script>const tooDeep = /abc/;</script>)html\";`;\n";
	struct editorTextSource source_view = {0};
	editorTextSourceInitString(&source_view, source, strlen(source));

	struct editorSyntaxState *state = editorSyntaxStateCreate(EDITOR_SYNTAX_JAVASCRIPT);
	ASSERT_TRUE(state != NULL);
	ASSERT_TRUE(editorSyntaxStateParseFull(state, &source_view));

	struct editorSyntaxLimitEvent event = {0};
	ASSERT_TRUE(editorSyntaxStateConsumeLimitEvent(state, &event));
	ASSERT_EQ_INT(EDITOR_SYNTAX_LIMIT_EVENT_INJECTION_DEPTH_EXCEEDED, event.kind);
	ASSERT_EQ_INT(EDITOR_SYNTAX_REGEX, event.language);
	ASSERT_EQ_INT(-1, event.row);
	ASSERT_EQ_INT(4, event.detail);
	ASSERT_TRUE(!editorSyntaxStateConsumeLimitEvent(state, NULL));
	editorSyntaxStateDestroy(state);

	char path[] = "/tmp/rotide-test-syntax-injection-depth-XXXXXX.js";
	ASSERT_TRUE(write_fixture_to_temp_path(
	        path, 3, "tests/syntax/supported/javascript/injection_depth.js"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_TRUE(strstr(E.statusmsg, "Tree-sitter injection depth limit reached") != NULL);

	ASSERT_TRUE(unlink(path) == 0);
	editorSyntaxTestResetMaxInjectionDepth();
	return 0;
}

static int test_editor_syntax_injection_slot_limit_reports_event(void) {
	const char *source = "const a0 = c`x`;\n"
	                     "const a1 = cpp`x`;\n"
	                     "const a2 = go`x`;\n"
	                     "const a3 = bash`x`;\n"
	                     "const a4 = html`x`;\n"
	                     "const a5 = javascript`x`;\n"
	                     "const a6 = jsdoc`x`;\n"
	                     "const a7 = typescript`x`;\n"
	                     "const a8 = css`x`;\n"
	                     "const a9 = json`x`;\n"
	                     "const a10 = python`x`;\n"
	                     "const a11 = php`x`;\n"
	                     "const a12 = rust`x`;\n"
	                     "const a13 = java`x`;\n"
	                     "const a14 = regex`x`;\n"
	                     "const a15 = csharp`x`;\n"
	                     "const a16 = haskell`x`;\n";
	struct editorTextSource source_view = {0};
	editorTextSourceInitString(&source_view, source, strlen(source));

	struct editorSyntaxState *state = editorSyntaxStateCreate(EDITOR_SYNTAX_JAVASCRIPT);
	ASSERT_TRUE(state != NULL);
	ASSERT_TRUE(editorSyntaxStateParseFull(state, &source_view));

	struct editorSyntaxLimitEvent event = {0};
	ASSERT_TRUE(editorSyntaxStateConsumeLimitEvent(state, &event));
	ASSERT_EQ_INT(EDITOR_SYNTAX_LIMIT_EVENT_INJECTION_SLOTS_FULL, event.kind);
	ASSERT_EQ_INT(EDITOR_SYNTAX_HASKELL, event.language);
	ASSERT_EQ_INT(-1, event.row);
	ASSERT_EQ_INT(16, event.detail);
	ASSERT_TRUE(!editorSyntaxStateConsumeLimitEvent(state, NULL));

	editorSyntaxStateDestroy(state);
	return 0;
}

static int test_editor_syntax_unknown_injection_target_is_graceful(void) {
	const char *source = "const value = notalanguage`body`;\n";
	struct editorTextSource source_view = {0};
	editorTextSourceInitString(&source_view, source, strlen(source));

	struct editorSyntaxState *state = editorSyntaxStateCreate(EDITOR_SYNTAX_JAVASCRIPT);
	ASSERT_TRUE(state != NULL);
	ASSERT_TRUE(editorSyntaxStateParseFull(state, &source_view));
	ASSERT_TRUE(editorSyntaxStateHasTree(state));
	ASSERT_TRUE(!editorSyntaxStateConsumeLimitEvent(state, NULL));
	ASSERT_TRUE(!editorSyntaxStateConsumeQueryUnavailableEvent(state, NULL, NULL));

	editorSyntaxStateDestroy(state);
	return 0;
}

static int test_editor_syntax_parse_budget_is_graceful(void) {
	size_t source_len = 0;
	char *source = build_repeated_text("function item(){ return 1; }\n", 120000, &source_len);
	ASSERT_TRUE(source != NULL);
	ASSERT_TRUE(source_len <= UINT32_MAX);
	struct editorTextSource source_view = {0};
	editorTextSourceInitString(&source_view, source, source_len);

	editorSyntaxTestSetBudgetOverrides(1, 8192, 2000000000ULL, 1);
	struct editorSyntaxState *state = editorSyntaxStateCreate(EDITOR_SYNTAX_JAVASCRIPT);
	ASSERT_TRUE(state != NULL);
	ASSERT_TRUE(editorSyntaxStateParseFull(state, &source_view));

	int parse_budget = 0;
	int query_budget = 0;
	ASSERT_TRUE(editorSyntaxStateConsumeBudgetEvents(state, &parse_budget, &query_budget));
	ASSERT_EQ_INT(1, parse_budget);
	ASSERT_EQ_INT(0, query_budget);

	editorSyntaxStateDestroy(state);
	editorSyntaxTestResetBudgetOverrides();
	free(source);
	return 0;
}

const struct editorTestCase g_syntax_captures_tests[] = {
        {"editor_syntax_query_budget_match_limit_is_graceful",
         test_editor_syntax_query_budget_match_limit_is_graceful},
        {"editor_syntax_query_compile_failure_records_diagnostics",
         test_editor_syntax_query_compile_failure_records_diagnostics},
        {"editor_syntax_capture_rules_are_longest_match_first",
         test_editor_syntax_capture_rules_are_longest_match_first},
        {"editor_syntax_collect_c_captures_without_injection_query_is_graceful",
         test_editor_syntax_collect_c_captures_without_injection_query_is_graceful},
        {"editor_syntax_capture_truncation_reports_event_and_keeps_span_limit",
         test_editor_syntax_capture_truncation_reports_event_and_keeps_span_limit},
        {"editor_syntax_parse_tree_errors_report_event_and_status",
         test_editor_syntax_parse_tree_errors_report_event_and_status},
        {"editor_syntax_injection_depth_limit_reports_event",
         test_editor_syntax_injection_depth_limit_reports_event},
        {"editor_syntax_injection_slot_limit_reports_event",
         test_editor_syntax_injection_slot_limit_reports_event},
        {"editor_syntax_unknown_injection_target_is_graceful",
         test_editor_syntax_unknown_injection_target_is_graceful},
        {"editor_syntax_parse_budget_is_graceful", test_editor_syntax_parse_budget_is_graceful},
};

const int g_syntax_captures_test_count =
        (int)(sizeof(g_syntax_captures_tests) / sizeof(g_syntax_captures_tests[0]));
#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "language/syntax.h"
#include "language/syntax_visible_cache.h"
#include "rotide.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
