#include "rotide.h"
#include "runner_support.h"
#include "seed.h"
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
extern const struct editorTestCase g_runner_internals_tests[];
extern const int g_runner_internals_test_count;

static const struct editorTestSuite k_suites[] = {
	{"document_text_editing", "document", g_document_text_editing_tests, 0},
	{"syntax_activation", "syntax", g_syntax_activation_tests, 0},
	{"syntax_parse", "syntax", g_syntax_parse_tests, 0},
	{"syntax_captures", "syntax", g_syntax_captures_tests, 0},
	{"syntax_background", "syntax threads", g_syntax_background_tests, 0},
	{"syntax_state", "syntax", g_syntax_state_tests, 0},
	{"syntax_registry", "syntax", g_syntax_registry_tests, 0},
	{"save_recovery", "save recovery", g_save_recovery_tests, 0},
	{"workspace_persistence", "workspace", g_workspace_persistence_tests, 0},
	{"workspace_theme_config", "workspace", g_workspace_theme_config_tests, 0},
	{"workspace_keymap_view", "workspace", g_workspace_keymap_view_tests, 0},
	{"workspace_io", "workspace", g_workspace_io_tests, 0},
	{"dap", "dap slow", g_dap_tests, 0},
	{"file_watch", "file_watch slow", g_file_watch_tests, 0},
	{"lsp_protocol", "lsp", g_lsp_protocol_tests, 0},
	{"lsp_lifecycle", "lsp", g_lsp_lifecycle_tests, 0},
	{"lsp_completion", "lsp", g_lsp_completion_tests, 0},
	{"lsp_diagnostics", "lsp", g_lsp_diagnostics_tests, 0},
	{"lsp_navigation", "lsp", g_lsp_navigation_tests, 0},
	{"input_actions", "input", g_input_actions_tests, 0},
	{"input_selection", "input", g_input_selection_tests, 0},
	{"input_mouse", "input", g_input_mouse_tests, 0},
	{"input_search", "input", g_input_search_tests, 0},
	{"input_undo", "input", g_input_undo_tests, 0},
	{"render_frame", "render", g_render_frame_tests, 0},
	{"render_chrome", "render", g_render_chrome_tests, 0},
	{"render_panes", "render", g_render_panes_tests, 0},
	{"render_terminal", "render", g_render_terminal_tests, 0},
	{"layout", "layout", g_layout_tests, 0},
	{"pty", "pty slow", g_pty_tests, 0},
	{"terminal_pane", "pty terminal slow", g_terminal_pane_tests, 0},
	{"runner_internals", "runner", g_runner_internals_tests, 0},
};

struct selectedTest {
	int suite;
	int index_in_suite;
};

static int select_suites(int *suite_counts) {
	suite_counts[0] = g_document_text_editing_test_count;
	suite_counts[1] = g_syntax_activation_test_count;
	suite_counts[2] = g_syntax_parse_test_count;
	suite_counts[3] = g_syntax_captures_test_count;
	suite_counts[4] = g_syntax_background_test_count;
	suite_counts[5] = g_syntax_state_test_count;
	suite_counts[6] = g_syntax_registry_test_count;
	suite_counts[7] = g_save_recovery_test_count;
	suite_counts[8] = g_workspace_persistence_test_count;
	suite_counts[9] = g_workspace_theme_config_test_count;
	suite_counts[10] = g_workspace_keymap_view_test_count;
	suite_counts[11] = g_workspace_io_test_count;
	suite_counts[12] = g_dap_test_count;
	suite_counts[13] = g_file_watch_test_count;
	suite_counts[14] = g_lsp_protocol_test_count;
	suite_counts[15] = g_lsp_lifecycle_test_count;
	suite_counts[16] = g_lsp_completion_test_count;
	suite_counts[17] = g_lsp_diagnostics_test_count;
	suite_counts[18] = g_lsp_navigation_test_count;
	suite_counts[19] = g_input_actions_test_count;
	suite_counts[20] = g_input_selection_test_count;
	suite_counts[21] = g_input_mouse_test_count;
	suite_counts[22] = g_input_search_test_count;
	suite_counts[23] = g_input_undo_test_count;
	suite_counts[24] = g_render_frame_test_count;
	suite_counts[25] = g_render_chrome_test_count;
	suite_counts[26] = g_render_panes_test_count;
	suite_counts[27] = g_render_terminal_test_count;
	suite_counts[28] = g_layout_test_count;
	suite_counts[29] = g_pty_test_count;
	suite_counts[30] = g_terminal_pane_test_count;
	suite_counts[31] = g_runner_internals_test_count;
	return (int)(sizeof(k_suites) / sizeof(k_suites[0]));
}

static int suite_passes_tag_filter(const struct editorTestSuite *suite, const struct testRunnerOptions *opts) {
	if (opts->include_tag != NULL && opts->include_tag[0] != '\0') {
		if (!runnerTagsHave(suite->tags, opts->include_tag)) {
			return 0;
		}
	}
	if (opts->exclude_tag != NULL && opts->exclude_tag[0] != '\0') {
		if (runnerTagsHave(suite->tags, opts->exclude_tag)) {
			return 0;
		}
	}
	return 1;
}

static int compare_snapshot(const unsigned char *before, const unsigned char *after, size_t size,
		size_t *first_diff_out) {
	for (size_t i = 0; i < size; i++) {
		if (before[i] != after[i]) {
			if (first_diff_out != NULL) {
				*first_diff_out = i;
			}
			return 0;
		}
	}
	return 1;
}

int main(int argc, char **argv) {
	setlocale(LC_CTYPE, "");
	char *startup_cwd = getcwd(NULL, 0);
	if (startup_cwd == NULL) {
		fprintf(stderr, "Failed to capture startup cwd: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}
	testHelpersInitPaths(startup_cwd);
	free(startup_cwd);

	struct testRunnerOptions opts;
	runnerOptionsInit(&opts);
	if (runnerOptionsParse(&opts, argc, argv) != 0) {
		fprintf(stderr, "rotide_tests: %s\n", opts.error_msg ? opts.error_msg : "argument parse error");
		runnerPrintUsage();
		return EXIT_FAILURE;
	}
	if (opts.help_requested) {
		runnerPrintUsage();
		return EXIT_SUCCESS;
	}

	if (!opts.seed_specified) {
		opts.seed = runnerSeedFromOsEntropy();
	}
	rotide_test_seed_set(opts.seed);

	int suite_counts[sizeof(k_suites) / sizeof(k_suites[0])];
	int suite_total = select_suites(suite_counts);

	struct quarantineList quarantine;
	quarantineListInit(&quarantine);
	if (!opts.no_quarantine) {
		char *err = NULL;
		if (quarantineListLoad(&quarantine, opts.quarantine_path, &err) != 0) {
			fprintf(stderr, "rotide_tests: %s\n", err ? err : "failed to load quarantine list");
			free(err);
			quarantineListFree(&quarantine);
			return EXIT_FAILURE;
		}
	}

	int total_candidates = 0;
	for (int s = 0; s < suite_total; s++) {
		total_candidates += suite_counts[s];
	}

	struct selectedTest *selected = calloc((size_t)(total_candidates > 0 ? total_candidates : 1), sizeof(*selected));
	if (selected == NULL) {
		fprintf(stderr, "rotide_tests: out of memory\n");
		quarantineListFree(&quarantine);
		return EXIT_FAILURE;
	}
	int selected_count = 0;
	int skipped_quarantine = 0;
	for (int s = 0; s < suite_total; s++) {
		const struct editorTestSuite *suite = &k_suites[s];
		if (!suite_passes_tag_filter(suite, &opts)) {
			continue;
		}
		for (int i = 0; i < suite_counts[s]; i++) {
			const char *name = suite->tests[i].name;
			if (!runnerNameMatches(name, opts.filter)) {
				continue;
			}
			if (!opts.no_quarantine && quarantineListContains(&quarantine, name)) {
				printf("SKIP %s (quarantined)\n", name);
				skipped_quarantine++;
				continue;
			}
			selected[selected_count].suite = s;
			selected[selected_count].index_in_suite = i;
			selected_count++;
		}
	}

	if (opts.list_only) {
		for (int i = 0; i < selected_count; i++) {
			const struct editorTestSuite *suite = &k_suites[selected[i].suite];
			const char *name = suite->tests[selected[i].index_in_suite].name;
			printf("%s\t%s\t%s\n", suite->name, name, suite->tags ? suite->tags : "");
		}
		free(selected);
		quarantineListFree(&quarantine);
		return EXIT_SUCCESS;
	}

	int *order = calloc((size_t)(selected_count > 0 ? selected_count : 1), sizeof(*order));
	if (order == NULL) {
		fprintf(stderr, "rotide_tests: out of memory\n");
		free(selected);
		quarantineListFree(&quarantine);
		return EXIT_FAILURE;
	}
	for (int i = 0; i < selected_count; i++) {
		order[i] = i;
	}
	if (opts.shuffle) {
		runnerShuffleIndices(order, selected_count, opts.seed);
	}

	unsigned char *snapshot = NULL;
	if (opts.validate_reset) {
		snapshot = malloc(sizeof(E));
		if (snapshot == NULL) {
			fprintf(stderr, "rotide_tests: out of memory for snapshot\n");
			free(order);
			free(selected);
			quarantineListFree(&quarantine);
			return EXIT_FAILURE;
		}
		reset_editor_state();
		memcpy(snapshot, &E, sizeof(E));
	}

	int total_runs = 0;
	int passed_runs = 0;
	int failed_unique = 0;
	int reset_violations = 0;

	for (int slot = 0; slot < selected_count; slot++) {
		int sel = order[slot];
		const struct editorTestSuite *suite = &k_suites[selected[sel].suite];
		const struct editorTestCase *tc = &suite->tests[selected[sel].index_in_suite];
		int local_failed = 0;
		for (int rep = 0; rep < opts.repeat; rep++) {
			total_runs++;
			reset_editor_state();
			int failed = tc->run();
			reset_editor_state();
			if (opts.validate_reset) {
				size_t diff_at = 0;
				if (!compare_snapshot(snapshot, (const unsigned char *)&E, sizeof(E), &diff_at)) {
					reset_violations++;
					fprintf(stderr,
						"RESET-DRIFT after %s (repeat %d/%d): first differing byte at offset %zu\n",
						tc->name, rep + 1, opts.repeat, diff_at);
				}
			}
			if (failed == 0) {
				passed_runs++;
				printf("PASS %s", tc->name);
				if (opts.repeat > 1) {
					printf(" (%d/%d)", rep + 1, opts.repeat);
				}
				printf("\n");
			} else {
				local_failed = 1;
				printf("FAIL %s", tc->name);
				if (opts.repeat > 1) {
					printf(" (%d/%d)", rep + 1, opts.repeat);
				}
				printf(" seed=0x%016llx\n", (unsigned long long)opts.seed);
				if (opts.fail_fast) {
					failed_unique++;
					goto done;
				}
			}
		}
		if (local_failed) {
			failed_unique++;
		}
	}

done:
	printf("\n%d/%d test runs passed", passed_runs, total_runs);
	if (failed_unique > 0) {
		printf(" (%d test%s failed)", failed_unique, failed_unique == 1 ? "" : "s");
	}
	if (skipped_quarantine > 0) {
		printf(", %d quarantined", skipped_quarantine);
	}
	if (opts.validate_reset) {
		printf(", reset-drift=%d", reset_violations);
	}
	printf(", seed=0x%016llx\n", (unsigned long long)opts.seed);

	free(snapshot);
	free(order);
	free(selected);
	quarantineListFree(&quarantine);

	int exit_code = EXIT_SUCCESS;
	if (failed_unique > 0) {
		exit_code = EXIT_FAILURE;
	}
	if (reset_violations > 0) {
		exit_code = EXIT_FAILURE;
	}
	return exit_code;
}
