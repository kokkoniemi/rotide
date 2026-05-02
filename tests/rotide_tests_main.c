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
extern const struct editorTestCase g_syntax_tests[];
extern const int g_syntax_test_count;
extern const struct editorTestCase g_syntax_registry_tests[];
extern const int g_syntax_registry_test_count;
extern const struct editorTestCase g_save_recovery_tests[];
extern const int g_save_recovery_test_count;
extern const struct editorTestCase g_workspace_config_tests[];
extern const int g_workspace_config_test_count;
extern const struct editorTestCase g_file_watch_tests[];
extern const int g_file_watch_test_count;
extern const struct editorTestCase g_lsp_tests[];
extern const int g_lsp_test_count;
extern const struct editorTestCase g_input_search_tests[];
extern const int g_input_search_test_count;
extern const struct editorTestCase g_render_terminal_tests[];
extern const int g_render_terminal_test_count;

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
		{g_syntax_tests, g_syntax_test_count},
		{g_syntax_registry_tests, g_syntax_registry_test_count},
		{g_save_recovery_tests, g_save_recovery_test_count},
		{g_workspace_config_tests, g_workspace_config_test_count},
		{g_file_watch_tests, g_file_watch_test_count},
		{g_lsp_tests, g_lsp_test_count},
		{g_input_search_tests, g_input_search_test_count},
		{g_render_terminal_tests, g_render_terminal_test_count},
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
