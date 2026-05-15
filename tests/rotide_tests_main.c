#include "test_case.h"
#include "test_helpers.h"

#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern const struct editorTestCase g_document_text_editing_tests[];
extern const int g_document_text_editing_test_count;
extern const struct editorTestCase g_syntax_activation_tests[];
extern const int g_syntax_activation_test_count;
extern const struct editorTestCase g_syntax_parse_tests[];
extern const int g_syntax_parse_test_count;
extern const struct editorTestCase g_syntax_captures_tests[];
extern const int g_syntax_captures_test_count;
extern const struct editorTestCase g_syntax_background_tests[];
extern const int g_syntax_background_test_count;
extern const struct editorTestCase g_syntax_state_tests[];
extern const int g_syntax_state_test_count;
extern const struct editorTestCase g_syntax_registry_tests[];
extern const int g_syntax_registry_test_count;
extern const struct editorTestCase g_save_recovery_tests[];
extern const int g_save_recovery_test_count;
extern const struct editorTestCase g_workspace_persistence_tests[];
extern const int g_workspace_persistence_test_count;
extern const struct editorTestCase g_workspace_theme_config_tests[];
extern const int g_workspace_theme_config_test_count;
extern const struct editorTestCase g_workspace_keymap_view_tests[];
extern const int g_workspace_keymap_view_test_count;
extern const struct editorTestCase g_workspace_io_tests[];
extern const int g_workspace_io_test_count;
extern const struct editorTestCase g_dap_tests[];
extern const int g_dap_test_count;
extern const struct editorTestCase g_file_watch_tests[];
extern const int g_file_watch_test_count;
extern const struct editorTestCase g_lsp_protocol_tests[];
extern const int g_lsp_protocol_test_count;
extern const struct editorTestCase g_lsp_lifecycle_tests[];
extern const int g_lsp_lifecycle_test_count;
extern const struct editorTestCase g_lsp_completion_tests[];
extern const int g_lsp_completion_test_count;
extern const struct editorTestCase g_lsp_diagnostics_tests[];
extern const int g_lsp_diagnostics_test_count;
extern const struct editorTestCase g_lsp_navigation_tests[];
extern const int g_lsp_navigation_test_count;
extern const struct editorTestCase g_input_actions_tests[];
extern const int g_input_actions_test_count;
extern const struct editorTestCase g_input_selection_tests[];
extern const int g_input_selection_test_count;
extern const struct editorTestCase g_input_mouse_tests[];
extern const int g_input_mouse_test_count;
extern const struct editorTestCase g_input_search_tests[];
extern const int g_input_search_test_count;
extern const struct editorTestCase g_input_undo_tests[];
extern const int g_input_undo_test_count;
extern const struct editorTestCase g_render_frame_tests[];
extern const int g_render_frame_test_count;
extern const struct editorTestCase g_render_chrome_tests[];
extern const int g_render_chrome_test_count;
extern const struct editorTestCase g_render_panes_tests[];
extern const int g_render_panes_test_count;
extern const struct editorTestCase g_render_terminal_tests[];
extern const int g_render_terminal_test_count;
extern const struct editorTestCase g_layout_tests[];
extern const int g_layout_test_count;
extern const struct editorTestCase g_pty_tests[];
extern const int g_pty_test_count;
extern const struct editorTestCase g_terminal_pane_tests[];
extern const int g_terminal_pane_test_count;

struct editorTestSuite {
	const struct editorTestCase *tests;
	int count;
};

int main(void) {
	setlocale(LC_CTYPE, "");
	char *startup_cwd = getcwd(NULL, 0);
	if (startup_cwd == NULL) {
		fprintf(stderr, "Failed to capture startup cwd: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}
	testHelpersInitPaths(startup_cwd);
	free(startup_cwd);

	const struct editorTestSuite suites[] = {
		{g_document_text_editing_tests, g_document_text_editing_test_count},
		{g_syntax_activation_tests, g_syntax_activation_test_count},
		{g_syntax_parse_tests, g_syntax_parse_test_count},
		{g_syntax_captures_tests, g_syntax_captures_test_count},
		{g_syntax_background_tests, g_syntax_background_test_count},
		{g_syntax_state_tests, g_syntax_state_test_count},
		{g_syntax_registry_tests, g_syntax_registry_test_count},
		{g_save_recovery_tests, g_save_recovery_test_count},
		{g_workspace_persistence_tests, g_workspace_persistence_test_count},
		{g_workspace_theme_config_tests, g_workspace_theme_config_test_count},
		{g_workspace_keymap_view_tests, g_workspace_keymap_view_test_count},
		{g_workspace_io_tests, g_workspace_io_test_count},
		{g_dap_tests, g_dap_test_count},
		{g_file_watch_tests, g_file_watch_test_count},
		{g_lsp_protocol_tests, g_lsp_protocol_test_count},
		{g_lsp_lifecycle_tests, g_lsp_lifecycle_test_count},
		{g_lsp_completion_tests, g_lsp_completion_test_count},
		{g_lsp_diagnostics_tests, g_lsp_diagnostics_test_count},
		{g_lsp_navigation_tests, g_lsp_navigation_test_count},
		{g_input_actions_tests, g_input_actions_test_count},
		{g_input_selection_tests, g_input_selection_test_count},
		{g_input_mouse_tests, g_input_mouse_test_count},
		{g_input_search_tests, g_input_search_test_count},
		{g_input_undo_tests, g_input_undo_test_count},
		{g_render_frame_tests, g_render_frame_test_count},
		{g_render_chrome_tests, g_render_chrome_test_count},
		{g_render_panes_tests, g_render_panes_test_count},
		{g_render_terminal_tests, g_render_terminal_test_count},
		{g_layout_tests, g_layout_test_count},
		{g_pty_tests, g_pty_test_count},
		{g_terminal_pane_tests, g_terminal_pane_test_count},
	};

	int total = 0;
	int passed = 0;
	for (int suite_idx = 0; suite_idx < (int)(sizeof(suites) / sizeof(suites[0])); suite_idx++) {
		for (int i = 0; i < suites[suite_idx].count; i++) {
			total++;
			reset_editor_state();
			int failed = suites[suite_idx].tests[i].run();
			reset_editor_state();

			if (failed == 0) {
				passed++;
				printf("PASS %s\n", suites[suite_idx].tests[i].name);
			} else {
				printf("FAIL %s\n", suites[suite_idx].tests[i].name);
			}
		}
	}

	printf("\n%d/%d tests passed\n", passed, total);
	return (passed == total) ? EXIT_SUCCESS : EXIT_FAILURE;
}
