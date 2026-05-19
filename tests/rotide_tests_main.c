#include "editor_state_snapshot.h"
#include "parallel_runner.h"
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

#define SUITE_EXTERN(prefix) \
	extern const struct editorTestCase g_##prefix##_tests[]; \
	extern const int g_##prefix##_test_count

SUITE_EXTERN(document_text_editing);
SUITE_EXTERN(syntax_activation);
SUITE_EXTERN(syntax_parse);
SUITE_EXTERN(syntax_captures);
SUITE_EXTERN(syntax_background);
SUITE_EXTERN(syntax_state);
SUITE_EXTERN(syntax_registry);
SUITE_EXTERN(save_recovery);
SUITE_EXTERN(workspace_persistence);
SUITE_EXTERN(workspace_theme_config);
SUITE_EXTERN(workspace_keymap_view);
SUITE_EXTERN(workspace_io);
SUITE_EXTERN(dap);
SUITE_EXTERN(file_watch);
SUITE_EXTERN(lsp_framing);
SUITE_EXTERN(lsp_protocol);
SUITE_EXTERN(lsp_lifecycle);
SUITE_EXTERN(lsp_completion);
SUITE_EXTERN(lsp_diagnostics);
SUITE_EXTERN(lsp_navigation);
SUITE_EXTERN(input_actions);
SUITE_EXTERN(input_selection);
SUITE_EXTERN(input_mouse);
SUITE_EXTERN(input_search);
SUITE_EXTERN(input_undo);
SUITE_EXTERN(render_frame);
SUITE_EXTERN(render_chrome);
SUITE_EXTERN(render_panes);
SUITE_EXTERN(render_terminal);
SUITE_EXTERN(layout);
SUITE_EXTERN(pty);
SUITE_EXTERN(terminal_pane);
SUITE_EXTERN(text_invariants);
SUITE_EXTERN(text_summary);
SUITE_EXTERN(text_tree);
SUITE_EXTERN(syntax_incremental_equiv);
SUITE_EXTERN(runner_internals);
SUITE_EXTERN(long_session);
SUITE_EXTERN(grid_snapshot);

#define SUITE(name_str, tags_str, prefix) \
	{name_str, tags_str, g_##prefix##_tests, &g_##prefix##_test_count}

static const struct editorTestSuite k_suites[] = {
	SUITE("document_text_editing", "document", document_text_editing),
	SUITE("syntax_activation", "syntax", syntax_activation),
	SUITE("syntax_parse", "syntax", syntax_parse),
	SUITE("syntax_captures", "syntax", syntax_captures),
	SUITE("syntax_background", "syntax threads", syntax_background),
	SUITE("syntax_state", "syntax", syntax_state),
	SUITE("syntax_registry", "syntax", syntax_registry),
	SUITE("save_recovery", "save recovery", save_recovery),
	SUITE("workspace_persistence", "workspace", workspace_persistence),
	SUITE("workspace_theme_config", "workspace", workspace_theme_config),
	SUITE("workspace_keymap_view", "workspace", workspace_keymap_view),
	SUITE("workspace_io", "workspace", workspace_io),
	SUITE("dap", "dap slow", dap),
	SUITE("file_watch", "file_watch slow", file_watch),
	SUITE("lsp_framing", "lsp", lsp_framing),
	SUITE("lsp_protocol", "lsp", lsp_protocol),
	SUITE("lsp_lifecycle", "lsp", lsp_lifecycle),
	SUITE("lsp_completion", "lsp", lsp_completion),
	SUITE("lsp_diagnostics", "lsp", lsp_diagnostics),
	SUITE("lsp_navigation", "lsp", lsp_navigation),
	SUITE("input_actions", "input", input_actions),
	SUITE("input_selection", "input", input_selection),
	SUITE("input_mouse", "input", input_mouse),
	SUITE("input_search", "input", input_search),
	SUITE("input_undo", "input", input_undo),
	SUITE("render_frame", "render", render_frame),
	SUITE("render_chrome", "render", render_chrome),
	SUITE("render_panes", "render", render_panes),
	SUITE("render_terminal", "render", render_terminal),
	SUITE("layout", "layout", layout),
	SUITE("pty", "pty slow", pty),
	SUITE("terminal_pane", "pty terminal slow", terminal_pane),
	SUITE("text_invariants", "document property", text_invariants),
	SUITE("text_summary", "document property", text_summary),
	SUITE("text_tree", "document", text_tree),
	SUITE("syntax_incremental_equiv", "syntax property", syntax_incremental_equiv),
	SUITE("runner_internals", "runner", runner_internals),
	SUITE("long_session", "memory slow", long_session),
	SUITE("grid_snapshot", "render", grid_snapshot),
};

#define K_SUITE_COUNT ((int)(sizeof(k_suites) / sizeof(k_suites[0])))

struct selectedTest {
	int suite;
	int index_in_suite;
};

static int suitePassesTagFilter(const struct editorTestSuite *suite, const struct testRunnerOptions *opts) {
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

/* --validate-reset: snapshot E once, compare after each test. Excludes
 * are in editor_state_snapshot.c. Coverage is E only — singletons
 * outside E (LSP mock, syntax-worker queue, alloc probes) aren't checked. */

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
	for (int s = 0; s < K_SUITE_COUNT; s++) {
		total_candidates += *k_suites[s].count;
	}

	struct selectedTest *selected = calloc((size_t)(total_candidates > 0 ? total_candidates : 1), sizeof(*selected));
	if (selected == NULL) {
		fprintf(stderr, "rotide_tests: out of memory\n");
		quarantineListFree(&quarantine);
		return EXIT_FAILURE;
	}
	int selected_count = 0;
	int skipped_quarantine = 0;
	for (int s = 0; s < K_SUITE_COUNT; s++) {
		const struct editorTestSuite *suite = &k_suites[s];
		if (!suitePassesTagFilter(suite, &opts)) {
			continue;
		}
		int suite_count = *suite->count;
		for (int i = 0; i < suite_count; i++) {
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

	int total_runs = 0;
	int passed_runs = 0;
	int failed_unique = 0;
	int reset_violations = 0;
	int crashes = 0;
	unsigned char *snapshot = NULL;

	if (opts.jobs > 1) {
		struct suiteBatch *batches = calloc((size_t)K_SUITE_COUNT, sizeof(*batches));
		int *per_suite_capacity = calloc((size_t)K_SUITE_COUNT, sizeof(int));
		int batch_count = 0;
		if (batches == NULL || per_suite_capacity == NULL) {
			fprintf(stderr, "rotide_tests: out of memory for batches\n");
			free(batches);
			free(per_suite_capacity);
			free(order);
			free(selected);
			quarantineListFree(&quarantine);
			return EXIT_FAILURE;
		}
		int *suite_to_batch = malloc((size_t)K_SUITE_COUNT * sizeof(int));
		if (suite_to_batch == NULL) {
			fprintf(stderr, "rotide_tests: out of memory for batches\n");
			free(batches);
			free(per_suite_capacity);
			free(order);
			free(selected);
			quarantineListFree(&quarantine);
			return EXIT_FAILURE;
		}
		for (int s = 0; s < K_SUITE_COUNT; s++) {
			suite_to_batch[s] = -1;
		}
		for (int i = 0; i < selected_count; i++) {
			int s = selected[i].suite;
			if (suite_to_batch[s] < 0) {
				suite_to_batch[s] = batch_count;
				batches[batch_count].suite_idx = s;
				batches[batch_count].test_indices = NULL;
				batches[batch_count].count = 0;
				per_suite_capacity[batch_count] = 0;
				batch_count++;
			}
			int b = suite_to_batch[s];
			if (batches[b].count == per_suite_capacity[b]) {
				int new_cap = per_suite_capacity[b] == 0 ? 16 : per_suite_capacity[b] * 2;
				int *grown = realloc(batches[b].test_indices, (size_t)new_cap * sizeof(int));
				if (grown == NULL) {
					fprintf(stderr, "rotide_tests: out of memory for batches\n");
					for (int j = 0; j < batch_count; j++) {
						free(batches[j].test_indices);
					}
					free(suite_to_batch);
					free(per_suite_capacity);
					free(batches);
					free(order);
					free(selected);
					quarantineListFree(&quarantine);
					return EXIT_FAILURE;
				}
				batches[b].test_indices = grown;
				per_suite_capacity[b] = new_cap;
			}
			batches[b].test_indices[batches[b].count++] = selected[i].index_in_suite;
		}
		free(suite_to_batch);
		free(per_suite_capacity);

		struct parallelRunResult result = {0};
		(void)parallelRunBatches(&opts, k_suites, batches, batch_count, &result);

		for (int b = 0; b < batch_count; b++) {
			if (batches[b].output != NULL) {
				fwrite(batches[b].output, 1, batches[b].output_len, stdout);
				free(batches[b].output);
			}
			if (batches[b].crashed) {
				fprintf(stderr,
					"CRASH suite=%s test=%s signal=%d artifact=%s seed=0x%016llx\n",
					k_suites[batches[b].suite_idx].name,
					batches[b].crash_test_name[0] ? batches[b].crash_test_name : "(unknown)",
					batches[b].crash_signal, batches[b].crash_artifact_path,
					(unsigned long long)opts.seed);
			}
			free(batches[b].test_indices);
		}
		free(batches);

		total_runs = result.total_runs;
		passed_runs = result.passed_runs;
		failed_unique = result.failed_unique;
		reset_violations = result.reset_violations;
		crashes = result.crashes;
		goto done;
	}

	if (opts.validate_reset) {
		snapshot = malloc(EDITOR_STATE_SNAPSHOT_SIZE);
		if (snapshot == NULL) {
			fprintf(stderr, "rotide_tests: out of memory for snapshot\n");
			free(order);
			free(selected);
			quarantineListFree(&quarantine);
			return EXIT_FAILURE;
		}
		reset_editor_state();
		rotideTestSnapshotEditor(snapshot);
	}

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
				if (!rotideTestSnapshotMatchesEditor(snapshot, &diff_at)) {
					reset_violations++;
					const unsigned char *live = (const unsigned char *)&E;
					fprintf(stderr,
						"RESET-DRIFT after %s (repeat %d/%d): offset=%zu snap=0x%02x live=0x%02x\n",
						tc->name, rep + 1, opts.repeat, diff_at,
						snapshot[diff_at], live[diff_at]);
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
	if (crashes > 0) {
		printf(", %d crashed", crashes);
	}
	if (skipped_quarantine > 0) {
		printf(", %d quarantined", skipped_quarantine);
	}
	if (opts.validate_reset) {
		printf(", reset-drift=%d", reset_violations);
	}
	if (opts.jobs > 1) {
		printf(", jobs=%d", opts.jobs);
	}
	printf(", seed=0x%016llx\n", (unsigned long long)opts.seed);

	free(snapshot);
	free(order);
	free(selected);
	quarantineListFree(&quarantine);

	int exit_code = EXIT_SUCCESS;
	if (failed_unique > 0 || crashes > 0) {
		exit_code = EXIT_FAILURE;
	}
	if (reset_violations > 0) {
		exit_code = EXIT_FAILURE;
	}
	return exit_code;
}
