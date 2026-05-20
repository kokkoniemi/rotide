#include "test_case.h"
#include "test_support.h"

static int test_editor_syntax_visible_cache_recomputes_only_changed_rows(void) {
	char path[] = "/tmp/rotide-test-syntax-visible-cache-XXXXXX.c";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 2, "tests/syntax/supported/c/visible_cache.c"));

	editorOpen(path);
	E.window_rows = 6;
	E.window_cols = 100;
	E.rowoff = 0;
	E.coloff = 0;
	E.cy = 1;
	E.cx = 2;

	editorSyntaxTestResetVisibleRowRecomputeCount();
	ASSERT_TRUE(editorSyntaxPrepareVisibleRowSpans(E.rowoff, E.window_rows));
	int full_recompute = editorSyntaxTestVisibleRowRecomputeCount();
	ASSERT_TRUE(full_recompute > 0);

	editorSyntaxTestResetVisibleRowRecomputeCount();
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxPrepareVisibleRowSpans(E.rowoff, E.window_rows));
	int incremental_recompute = editorSyntaxTestVisibleRowRecomputeCount();
	ASSERT_TRUE(incremental_recompute > 0);
	ASSERT_TRUE(incremental_recompute < full_recompute);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_visible_cache_slides_on_scroll(void) {
	char path[] = "/tmp/rotide-test-syntax-visible-cache-scroll-XXXXXX.c";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 2, "tests/syntax/supported/c/visible_cache.c"));

	editorOpen(path);
	E.window_rows = 4;
	E.window_cols = 100;
	E.rowoff = 0;
	E.coloff = 0;

	editorSyntaxTestResetVisibleRowRecomputeCount();
	ASSERT_TRUE(editorSyntaxPrepareVisibleRowSpans(E.rowoff, E.window_rows));
	ASSERT_EQ_INT(E.window_rows, editorSyntaxTestVisibleRowRecomputeCount());

	struct editorRowSyntaxSpan before[ROTIDE_MAX_SYNTAX_SPANS_PER_ROW];
	int before_count = 0;
	ASSERT_TRUE(editorSyntaxRowRenderSpans(1, before, (int)(sizeof(before) / sizeof(before[0])),
	                                       &before_count));
	ASSERT_TRUE(before_count > 0);

	E.rowoff = 1;
	editorSyntaxTestResetVisibleRowRecomputeCount();
	ASSERT_TRUE(editorSyntaxPrepareVisibleRowSpans(E.rowoff, E.window_rows));
	ASSERT_EQ_INT(1, editorSyntaxTestVisibleRowRecomputeCount());

	struct editorRowSyntaxSpan after[ROTIDE_MAX_SYNTAX_SPANS_PER_ROW];
	int after_count = 0;
	ASSERT_TRUE(editorSyntaxRowRenderSpans(1, after, (int)(sizeof(after) / sizeof(after[0])),
	                                       &after_count));
	ASSERT_EQ_INT(before_count, after_count);
	ASSERT_MEM_EQ(before, after, sizeof(before[0]) * (size_t)before_count);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_background_parse_commits_visible_spans(void) {
	char path[] = "/tmp/rotide-test-syntax-background-XXXXXX.c";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 2, "tests/syntax/supported/c/visible_cache.c"));

	editorSyntaxBackgroundSetEnabledForTests(1);
	editorOpen(path);
	E.window_rows = 4;
	E.window_cols = 100;
	E.rowoff = 0;
	E.coloff = 0;

	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());
	ASSERT_TRUE(!editorSyntaxTreeExists());
	ASSERT_TRUE(editorSyntaxPrepareVisibleRowSpans(E.rowoff, E.window_rows));
	ASSERT_TRUE(editorSyntaxBackgroundFlushForTests());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_STR("translation_unit", editorSyntaxRootType());

	struct editorRowSyntaxSpan spans[ROTIDE_MAX_SYNTAX_SPANS_PER_ROW];
	int span_count = 0;
	ASSERT_TRUE(editorSyntaxRowRenderSpans(1, spans, (int)(sizeof(spans) / sizeof(spans[0])),
	                                       &span_count));
	ASSERT_TRUE(span_count > 0);

	editorSyntaxBackgroundSetEnabledForTests(0);
	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_background_poll_reports_committed_result(void) {
	char path[] = "/tmp/rotide-test-syntax-background-event-XXXXXX.c";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 2, "tests/syntax/supported/c/visible_cache.c"));

	editorSyntaxBackgroundSetEnabledForTests(1);
	editorOpen(path);
	E.window_rows = 4;
	E.window_cols = 100;
	E.rowoff = 0;
	E.coloff = 0;

	ASSERT_TRUE(editorSyntaxPrepareVisibleRowSpans(E.rowoff, E.window_rows));
	editorSyntaxWorkerWaitForIdle();
	ASSERT_EQ_INT(1, editorSyntaxBackgroundPoll());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(0, editorSyntaxBackgroundPoll());

	editorSyntaxBackgroundSetEnabledForTests(0);
	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_background_viewport_schedule_preserves_cached_overlap(void) {
	char path[] = "/tmp/rotide-test-syntax-background-scroll-XXXXXX.c";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 2, "tests/syntax/supported/c/visible_cache.c"));

	editorSyntaxBackgroundSetEnabledForTests(1);
	editorOpen(path);
	E.window_rows = 4;
	E.window_cols = 100;
	E.rowoff = 0;
	E.coloff = 0;

	ASSERT_TRUE(editorSyntaxPrepareVisibleRowSpans(E.rowoff, E.window_rows));
	ASSERT_TRUE(editorSyntaxBackgroundFlushForTests());

	struct editorRowSyntaxSpan before[ROTIDE_MAX_SYNTAX_SPANS_PER_ROW];
	int before_count = 0;
	ASSERT_TRUE(editorSyntaxRowRenderSpans(1, before, (int)(sizeof(before) / sizeof(before[0])),
	                                       &before_count));
	ASSERT_TRUE(before_count > 0);

	E.rowoff = 1;
	ASSERT_TRUE(editorSyntaxPrepareVisibleRowSpans(E.rowoff, E.window_rows));
	ASSERT_EQ_INT(0, E.syntax_background_pending);

	struct editorRowSyntaxSpan during[ROTIDE_MAX_SYNTAX_SPANS_PER_ROW];
	int during_count = 0;
	ASSERT_TRUE(editorSyntaxRowRenderSpans(1, during, (int)(sizeof(during) / sizeof(during[0])),
	                                       &during_count));
	ASSERT_EQ_INT(before_count, during_count);
	ASSERT_MEM_EQ(before, during, sizeof(before[0]) * (size_t)before_count);

	struct editorRowSyntaxSpan outside[ROTIDE_MAX_SYNTAX_SPANS_PER_ROW];
	int outside_count = 0;
	ASSERT_TRUE(editorSyntaxRowRenderSpans(
	        7, outside, (int)(sizeof(outside) / sizeof(outside[0])), &outside_count));
	ASSERT_TRUE(outside_count > 0);

	ASSERT_TRUE(editorSyntaxBackgroundFlushForTests());

	editorSyntaxBackgroundSetEnabledForTests(0);
	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_background_drops_stale_parse_results(void) {
	char path[] = "/tmp/rotide-test-syntax-background-stale-XXXXXX.c";
	ASSERT_TRUE(
	        write_fixture_to_temp_path(path, 2, "tests/syntax/supported/c/visible_cache.c"));

	editorSyntaxBackgroundSetEnabledForTests(1);
	editorOpen(path);
	uint64_t first_revision = E.syntax_revision;
	ASSERT_TRUE(first_revision > 0);

	E.cy = 1;
	E.cx = 2;
	editorInsertChar('x');
	ASSERT_TRUE(E.syntax_revision > first_revision);
	ASSERT_TRUE(editorSyntaxBackgroundFlushForTests());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(0, E.syntax_background_pending);
	ASSERT_EQ_STR("translation_unit", editorSyntaxRootType());

	editorSyntaxBackgroundSetEnabledForTests(0);
	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_background_unsupported_file_stays_plain(void) {
	char path[] = "/tmp/rotide-test-syntax-background-plain-XXXXXX.txt";
	ASSERT_TRUE(write_temp_text_file(path, sizeof(path), "plain text\n"));

	editorSyntaxBackgroundSetEnabledForTests(1);
	editorOpen(path);
	ASSERT_TRUE(editorSyntaxBackgroundFlushForTests());
	ASSERT_TRUE(!editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_NONE, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxPrepareVisibleRowSpans(E.rowoff, E.window_rows));

	editorSyntaxBackgroundSetEnabledForTests(0);
	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

const struct editorTestCase g_syntax_background_tests[] = {
        {"editor_syntax_visible_cache_recomputes_only_changed_rows",
         test_editor_syntax_visible_cache_recomputes_only_changed_rows},
        {"editor_syntax_visible_cache_slides_on_scroll",
         test_editor_syntax_visible_cache_slides_on_scroll},
        {"editor_syntax_background_parse_commits_visible_spans",
         test_editor_syntax_background_parse_commits_visible_spans},
        {"editor_syntax_background_poll_reports_committed_result",
         test_editor_syntax_background_poll_reports_committed_result},
        {"editor_syntax_background_viewport_schedule_preserves_cached_overlap",
         test_editor_syntax_background_viewport_schedule_preserves_cached_overlap},
        {"editor_syntax_background_drops_stale_parse_results",
         test_editor_syntax_background_drops_stale_parse_results},
        {"editor_syntax_background_unsupported_file_stays_plain",
         test_editor_syntax_background_unsupported_file_stays_plain},
};

const int g_syntax_background_test_count =
        (int)(sizeof(g_syntax_background_tests) / sizeof(g_syntax_background_tests[0]));
