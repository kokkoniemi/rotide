#include "test_case.h"
#include "test_support.h"
#include "test_helpers.h"

static int test_editor_lsp_autocomplete_disabled_by_config_does_not_trigger(void) {
	editorLspTestResetMock();
	editorLspTestSetMockEnabled(1);
	E.lsp_clangd_enabled = 1;
	E.lsp_autocomplete_enabled = 0;
	free(E.filename);
	E.filename = strdup("/tmp/auto.c");
	ASSERT_TRUE(E.filename != NULL);
	E.syntax_language = EDITOR_SYNTAX_C;
	add_row("foo");
	E.cy = 0;
	E.cx = 3;

	editorAutocompleteOnCharInserted('o');
	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(0, stats.completion_count);
	ASSERT_EQ_INT(0, editorAutocompleteIsVisible());

	editorAutocompleteShutdown();
	editorLspTestResetMock();
	editorLspTestSetMockEnabled(0);
	return 0;
}

static int test_editor_lsp_autocomplete_identifier_trigger_opens_popup(void) {
	editorLspTestResetMock();
	editorLspTestSetMockEnabled(1);
	E.lsp_clangd_enabled = 1;
	E.lsp_autocomplete_enabled = 1;
	E.lsp_autocomplete_max_items = 50;
	free(E.filename);
	E.filename = strdup("/tmp/auto.c");
	ASSERT_TRUE(E.filename != NULL);
	E.syntax_language = EDITOR_SYNTAX_C;
	add_row("foo");
	E.cy = 0;
	E.cx = 3;

	struct editorLspCompletionItem mock_items[2] = {
	        {.label = (char *)"foobar"},
	        {.label = (char *)"foobaz"},
	};
	editorLspTestSetMockCompletionResponse(mock_items, 2);

	editorAutocompleteOnCharInserted('o');
	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.completion_count);
	ASSERT_EQ_INT(1, editorLspCompletionPendingActive());

	editorLspTestDeliverPendingCompletion();
	ASSERT_EQ_INT(1, editorAutocompleteIsVisible());
	ASSERT_EQ_INT(2, editorPopupItemCount());
	ASSERT_EQ_STR("foobar", editorPopupSelectedLabel());

	editorAutocompleteShutdown();
	editorLspTestResetMock();
	editorLspTestSetMockEnabled(0);
	return 0;
}

static int test_editor_lsp_autocomplete_stale_response_after_cursor_move_is_ignored(void) {
	editorLspTestResetMock();
	editorLspTestSetMockEnabled(1);
	E.lsp_clangd_enabled = 1;
	E.lsp_autocomplete_enabled = 1;
	E.lsp_autocomplete_max_items = 50;
	free(E.filename);
	E.filename = strdup("/tmp/auto.c");
	ASSERT_TRUE(E.filename != NULL);
	E.syntax_language = EDITOR_SYNTAX_C;
	add_row("foo");
	add_row("bar");
	E.cy = 0;
	E.cx = 3;

	struct editorLspCompletionItem mock_items[1] = {
	        {.label = (char *)"foobar"},
	};
	editorLspTestSetMockCompletionResponse(mock_items, 1);
	editorAutocompleteOnCharInserted('o');
	ASSERT_EQ_INT(1, editorLspCompletionPendingActive());

	/* Move cursor to a different line before the response arrives. */
	E.cy = 1;
	E.cx = 0;

	editorLspTestDeliverPendingCompletion();
	ASSERT_EQ_INT(0, editorAutocompleteIsVisible());

	editorAutocompleteShutdown();
	editorLspTestResetMock();
	editorLspTestSetMockEnabled(0);
	return 0;
}

static int test_editor_lsp_autocomplete_accept_inserts_label(void) {
	editorLspTestResetMock();
	editorLspTestSetMockEnabled(1);
	E.lsp_clangd_enabled = 1;
	E.lsp_autocomplete_enabled = 1;
	E.lsp_autocomplete_max_items = 50;
	free(E.filename);
	E.filename = strdup("/tmp/auto.c");
	ASSERT_TRUE(E.filename != NULL);
	E.syntax_language = EDITOR_SYNTAX_C;
	add_row("foo");
	E.cy = 0;
	E.cx = 3;

	struct editorLspCompletionItem mock_items[1] = {
	        {.label = (char *)"foobar"},
	};
	editorLspTestSetMockCompletionResponse(mock_items, 1);
	editorAutocompleteOnCharInserted('o');
	editorLspTestDeliverPendingCompletion();
	ASSERT_EQ_INT(1, editorAutocompleteIsVisible());

	ASSERT_TRUE(editorAutocompleteAcceptSelection());
	ASSERT_EQ_INT(0, editorAutocompleteIsVisible());
	ASSERT_ROW_TEXT_EQ(0, "foobar");

	editorAutocompleteShutdown();
	editorLspTestResetMock();
	editorLspTestSetMockEnabled(0);
	return 0;
}

static int test_editor_lsp_autocomplete_accept_uses_insert_text(void) {
	editorLspTestResetMock();
	editorLspTestSetMockEnabled(1);
	E.lsp_clangd_enabled = 1;
	E.lsp_autocomplete_enabled = 1;
	E.lsp_autocomplete_max_items = 50;
	free(E.filename);
	E.filename = strdup("/tmp/auto.c");
	ASSERT_TRUE(E.filename != NULL);
	E.syntax_language = EDITOR_SYNTAX_C;
	add_row("foo");
	E.cy = 0;
	E.cx = 3;

	struct editorLspCompletionItem mock_items[1] = {
	        {.label = (char *)"foobar", .insert_text = (char *)"foobar_extra"},
	};
	editorLspTestSetMockCompletionResponse(mock_items, 1);
	editorAutocompleteOnCharInserted('o');
	editorLspTestDeliverPendingCompletion();
	ASSERT_TRUE(editorAutocompleteAcceptSelection());
	ASSERT_ROW_TEXT_EQ(0, "foobar_extra");

	editorAutocompleteShutdown();
	editorLspTestResetMock();
	editorLspTestSetMockEnabled(0);
	return 0;
}

static int test_editor_lsp_autocomplete_typing_narrows_popup_without_response(void) {
	editorLspTestResetMock();
	editorLspTestSetMockEnabled(1);
	E.lsp_clangd_enabled = 1;
	E.lsp_autocomplete_enabled = 1;
	E.lsp_autocomplete_max_items = 100;
	free(E.filename);
	E.filename = strdup("/tmp/auto.c");
	ASSERT_TRUE(E.filename != NULL);
	E.syntax_language = EDITOR_SYNTAX_C;
	add_row("f");
	E.cy = 0;
	E.cx = 1;

	struct editorLspCompletionItem mock_items[3] = {
	        {.label = (char *)"foo"},
	        {.label = (char *)"fbar"},
	        {.label = (char *)"fbaz"},
	};
	editorLspTestSetMockCompletionResponse(mock_items, 3);
	editorAutocompleteOnCharInserted('f');
	editorLspTestDeliverPendingCompletion();
	ASSERT_TRUE(editorAutocompleteIsVisible());
	ASSERT_EQ_INT(3, editorPopupItemCount());

	editorInsertChar('o');
	editorAutocompleteOnCharInserted('o');
	ASSERT_TRUE(editorAutocompleteIsVisible());
	ASSERT_EQ_INT(1, editorPopupItemCount());
	ASSERT_EQ_STR("foo", editorPopupSelectedLabel());

	editorAutocompleteShutdown();
	editorLspTestResetMock();
	editorLspTestSetMockEnabled(0);
	return 0;
}

static int test_editor_lsp_autocomplete_typing_narrows_popup_keeps_accept_correct(void) {
	editorLspTestResetMock();
	editorLspTestSetMockEnabled(1);
	E.lsp_clangd_enabled = 1;
	E.lsp_autocomplete_enabled = 1;
	E.lsp_autocomplete_max_items = 100;
	free(E.filename);
	E.filename = strdup("/tmp/auto.c");
	ASSERT_TRUE(E.filename != NULL);
	E.syntax_language = EDITOR_SYNTAX_C;
	add_row("f");
	E.cy = 0;
	E.cx = 1;

	struct editorLspCompletionItem mock_items[3] = {
	        {.label = (char *)"foo"},
	        {.label = (char *)"fbar"},
	        {.label = (char *)"fbaz"},
	};
	editorLspTestSetMockCompletionResponse(mock_items, 3);
	editorAutocompleteOnCharInserted('f');
	editorLspTestDeliverPendingCompletion();

	editorInsertChar('o');
	editorAutocompleteOnCharInserted('o');
	ASSERT_TRUE(editorAutocompleteIsVisible());
	ASSERT_EQ_INT(1, editorPopupItemCount());

	ASSERT_TRUE(editorAutocompleteAcceptSelection());
	ASSERT_ROW_TEXT_EQ(0, "foo");

	editorAutocompleteShutdown();
	editorLspTestResetMock();
	editorLspTestSetMockEnabled(0);
	return 0;
}

static int test_editor_lsp_autocomplete_dispatch_typing_keeps_popup_open(void) {
	editorLspTestResetMock();
	editorLspTestSetMockEnabled(1);
	E.lsp_clangd_enabled = 1;
	E.lsp_autocomplete_enabled = 1;
	E.lsp_autocomplete_max_items = 100;
	free(E.filename);
	E.filename = strdup("/tmp/auto.c");
	ASSERT_TRUE(E.filename != NULL);
	E.syntax_language = EDITOR_SYNTAX_C;
	add_row("f");
	E.cy = 0;
	E.cx = 1;

	struct editorLspCompletionItem mock_items[3] = {
	        {.label = (char *)"foo"},
	        {.label = (char *)"fbar"},
	        {.label = (char *)"fbaz"},
	};
	editorLspTestSetMockCompletionResponse(mock_items, 3);
	editorAutocompleteOnCharInserted('f');
	editorLspTestDeliverPendingCompletion();
	ASSERT_TRUE(editorAutocompleteIsVisible());
	ASSERT_EQ_INT(3, editorPopupItemCount());

	/*
	 * Route the keystroke through the dispatcher so the popup-handler path is exercised.
	 * Before the fix the popup would be dismissed by editorPopupHandleKey and then the
	 * dispatcher would cancel the autocomplete, leaving an empty popup until the LSP
	 * server replied.
	 */
	char input[] = {'o'};
	ASSERT_TRUE(editor_process_keypress_with_input(input, sizeof(input)) == 0);

	ASSERT_TRUE(editorAutocompleteIsVisible());
	ASSERT_TRUE(editorPopupIsVisible());
	ASSERT_EQ_INT(1, editorPopupItemCount());
	ASSERT_EQ_STR("foo", editorPopupSelectedLabel());

	editorAutocompleteShutdown();
	editorLspTestResetMock();
	editorLspTestSetMockEnabled(0);
	return 0;
}

static int test_editor_lsp_autocomplete_filters_clangd_decorated_labels(void) {
	editorLspTestResetMock();
	editorLspTestSetMockEnabled(1);
	E.lsp_clangd_enabled = 1;
	E.lsp_autocomplete_enabled = 1;
	E.lsp_autocomplete_max_items = 100;
	free(E.filename);
	E.filename = strdup("/tmp/auto.c");
	ASSERT_TRUE(E.filename != NULL);
	E.syntax_language = EDITOR_SYNTAX_C;
	add_row("client.");
	E.cy = 0;
	E.cx = 7;

	/* Mirror clangd: labels carry leading whitespace/markers; filterText holds the
	 * bare symbol that should drive prefix matching. */
	struct editorLspCompletionItem mock_items[3] = {
	        {.label = (char *)" completion_pending",
	         .filter_text = (char *)"completion_pending"},
	        {.label = (char *)" completion_supported",
	         .filter_text = (char *)"completion_supported"},
	        {.label = (char *)" flags", .filter_text = (char *)"flags"},
	};
	editorLspTestSetMockCompletionResponse(mock_items, 3);
	editorAutocompleteOnCharInserted('.');
	editorLspTestDeliverPendingCompletion();
	ASSERT_TRUE(editorAutocompleteIsVisible());
	ASSERT_EQ_INT(3, editorPopupItemCount());
	/* Display label must be stripped of leading space. */
	ASSERT_EQ_STR("completion_pending", editorPopupSelectedLabel());

	char input[] = {'c'};
	ASSERT_TRUE(editor_process_keypress_with_input(input, sizeof(input)) == 0);

	ASSERT_TRUE(editorAutocompleteIsVisible());
	ASSERT_TRUE(editorPopupIsVisible());
	ASSERT_EQ_INT(2, editorPopupItemCount());
	ASSERT_EQ_STR("completion_pending", editorPopupSelectedLabel());

	editorAutocompleteShutdown();
	editorLspTestResetMock();
	editorLspTestSetMockEnabled(0);
	return 0;
}

static int test_editor_lsp_autocomplete_accept_uses_filter_text_when_label_decorated(void) {
	editorLspTestResetMock();
	editorLspTestSetMockEnabled(1);
	E.lsp_clangd_enabled = 1;
	E.lsp_autocomplete_enabled = 1;
	E.lsp_autocomplete_max_items = 100;
	free(E.filename);
	E.filename = strdup("/tmp/auto.c");
	ASSERT_TRUE(E.filename != NULL);
	E.syntax_language = EDITOR_SYNTAX_C;
	add_row("client.");
	E.cy = 0;
	E.cx = 7;

	struct editorLspCompletionItem mock_items[1] = {
	        {.label = (char *)" completion_pending",
	         .filter_text = (char *)"completion_pending"},
	};
	editorLspTestSetMockCompletionResponse(mock_items, 1);
	editorAutocompleteOnCharInserted('.');
	editorLspTestDeliverPendingCompletion();
	ASSERT_TRUE(editorAutocompleteAcceptSelection());
	/* Inserted text must not carry the leading space from label. */
	ASSERT_ROW_TEXT_EQ(0, "client.completion_pending");

	editorAutocompleteShutdown();
	editorLspTestResetMock();
	editorLspTestSetMockEnabled(0);
	return 0;
}

static int test_editor_lsp_autocomplete_dispatch_typing_after_trigger_char_keeps_popup(void) {
	editorLspTestResetMock();
	editorLspTestSetMockEnabled(1);
	E.lsp_clangd_enabled = 1;
	E.lsp_autocomplete_enabled = 1;
	E.lsp_autocomplete_max_items = 100;
	free(E.filename);
	E.filename = strdup("/tmp/auto.c");
	ASSERT_TRUE(E.filename != NULL);
	E.syntax_language = EDITOR_SYNTAX_C;
	add_row("client.");
	E.cy = 0;
	E.cx = 7;

	struct editorLspCompletionItem mock_items[3] = {
	        {.label = (char *)"count"},
	        {.label = (char *)"connect"},
	        {.label = (char *)"flags"},
	};
	editorLspTestSetMockCompletionResponse(mock_items, 3);
	editorAutocompleteOnCharInserted('.');
	editorLspTestDeliverPendingCompletion();
	ASSERT_TRUE(editorAutocompleteIsVisible());
	ASSERT_EQ_INT(3, editorPopupItemCount());

	char input[] = {'c'};
	ASSERT_TRUE(editor_process_keypress_with_input(input, sizeof(input)) == 0);

	ASSERT_TRUE(editorAutocompleteIsVisible());
	ASSERT_TRUE(editorPopupIsVisible());
	ASSERT_EQ_INT(2, editorPopupItemCount());

	editorAutocompleteShutdown();
	editorLspTestResetMock();
	editorLspTestSetMockEnabled(0);
	return 0;
}

const struct editorTestCase g_lsp_completion_tests[] = {
        {"editor_lsp_autocomplete_disabled_by_config_does_not_trigger",
         test_editor_lsp_autocomplete_disabled_by_config_does_not_trigger},
        {"editor_lsp_autocomplete_identifier_trigger_opens_popup",
         test_editor_lsp_autocomplete_identifier_trigger_opens_popup},
        {"editor_lsp_autocomplete_stale_response_after_cursor_move_is_ignored",
         test_editor_lsp_autocomplete_stale_response_after_cursor_move_is_ignored},
        {"editor_lsp_autocomplete_accept_inserts_label",
         test_editor_lsp_autocomplete_accept_inserts_label},
        {"editor_lsp_autocomplete_accept_uses_insert_text",
         test_editor_lsp_autocomplete_accept_uses_insert_text},
        {"editor_lsp_autocomplete_typing_narrows_popup_without_response",
         test_editor_lsp_autocomplete_typing_narrows_popup_without_response},
        {"editor_lsp_autocomplete_typing_narrows_popup_keeps_accept_correct",
         test_editor_lsp_autocomplete_typing_narrows_popup_keeps_accept_correct},
        {"editor_lsp_autocomplete_dispatch_typing_keeps_popup_open",
         test_editor_lsp_autocomplete_dispatch_typing_keeps_popup_open},
        {"editor_lsp_autocomplete_filters_clangd_decorated_labels",
         test_editor_lsp_autocomplete_filters_clangd_decorated_labels},
        {"editor_lsp_autocomplete_accept_uses_filter_text_when_label_decorated",
         test_editor_lsp_autocomplete_accept_uses_filter_text_when_label_decorated},
        {"editor_lsp_autocomplete_dispatch_typing_after_trigger_char_keeps_popup",
         test_editor_lsp_autocomplete_dispatch_typing_after_trigger_char_keeps_popup},
};

const int g_lsp_completion_test_count =
        (int)(sizeof(g_lsp_completion_tests) / sizeof(g_lsp_completion_tests[0]));
#include "editing/edit.h"
#include "language/syntax.h"
#include "language/lsp.h"
#include <stddef.h>
#include <stdlib.h>
