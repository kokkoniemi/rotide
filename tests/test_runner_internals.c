#include "runner_support.h"
#include "seed.h"
#include "test_case.h"
#include "test_helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int test_runner_name_matches_substring(void) {
	ASSERT_TRUE(runnerNameMatches("lsp_protocol_handshake", "lsp"));
	ASSERT_TRUE(runnerNameMatches("foo_lsp_bar", "lsp"));
	ASSERT_TRUE(!runnerNameMatches("syntax_parse_basic", "lsp"));
	ASSERT_TRUE(runnerNameMatches("anything", NULL));
	ASSERT_TRUE(runnerNameMatches("anything", ""));
	return 0;
}

static int test_runner_tags_have_exact_token(void) {
	ASSERT_TRUE(runnerTagsHave("lsp slow", "lsp"));
	ASSERT_TRUE(runnerTagsHave("lsp slow", "slow"));
	ASSERT_TRUE(runnerTagsHave("  lsp\tslow ", "lsp"));
	ASSERT_TRUE(!runnerTagsHave("lsp slow", "ls"));
	ASSERT_TRUE(!runnerTagsHave("lsp slow", "slower"));
	ASSERT_TRUE(!runnerTagsHave("", "lsp"));
	ASSERT_TRUE(!runnerTagsHave(NULL, "lsp"));
	ASSERT_TRUE(!runnerTagsHave("lsp", NULL));
	ASSERT_TRUE(!runnerTagsHave("lsp", ""));
	return 0;
}

static int test_runner_options_parse_basic_flags(void) {
	char *argv[] = {
	        (char *)"rotide_tests", (char *)"--list",           (char *)"--fail-fast",
	        (char *)"--shuffle",    (char *)"--validate-reset", (char *)"--no-quarantine",
	};
	struct testRunnerOptions opts;
	runnerOptionsInit(&opts);
	ASSERT_EQ_INT(0, runnerOptionsParse(&opts, (int)(sizeof(argv) / sizeof(argv[0])), argv));
	ASSERT_TRUE(opts.list_only);
	ASSERT_TRUE(opts.fail_fast);
	ASSERT_TRUE(opts.shuffle);
	ASSERT_TRUE(opts.validate_reset);
	ASSERT_TRUE(opts.no_quarantine);
	ASSERT_EQ_INT(1, opts.repeat);
	return 0;
}

static int test_runner_options_parse_value_flags_split_and_eq(void) {
	char *argv[] = {
	        (char *)"rotide_tests", (char *)"--filter",      (char *)"sub",
	        (char *)"--tag=lsp",    (char *)"--exclude-tag", (char *)"slow",
	        (char *)"--repeat=3",   (char *)"--seed",        (char *)"0x2A",
	};
	struct testRunnerOptions opts;
	runnerOptionsInit(&opts);
	ASSERT_EQ_INT(0, runnerOptionsParse(&opts, (int)(sizeof(argv) / sizeof(argv[0])), argv));
	ASSERT_EQ_STR("sub", opts.filter);
	ASSERT_EQ_STR("lsp", opts.include_tag);
	ASSERT_EQ_STR("slow", opts.exclude_tag);
	ASSERT_EQ_INT(3, opts.repeat);
	ASSERT_TRUE(opts.seed_specified);
	ASSERT_EQ_INT(42, (int)opts.seed);
	return 0;
}

static int test_runner_options_parse_rejects_unknown(void) {
	char *argv[] = {(char *)"rotide_tests", (char *)"--mystery"};
	struct testRunnerOptions opts;
	runnerOptionsInit(&opts);
	ASSERT_EQ_INT(1, runnerOptionsParse(&opts, 2, argv));
	ASSERT_TRUE(opts.parse_error);
	return 0;
}

static int test_runner_options_parse_rejects_bad_repeat(void) {
	char *argv[] = {(char *)"rotide_tests", (char *)"--repeat", (char *)"-3"};
	struct testRunnerOptions opts;
	runnerOptionsInit(&opts);
	ASSERT_EQ_INT(1, runnerOptionsParse(&opts, 3, argv));
	ASSERT_TRUE(opts.parse_error);
	return 0;
}

static int test_runner_options_parse_rejects_missing_value(void) {
	char *argv[] = {(char *)"rotide_tests", (char *)"--filter"};
	struct testRunnerOptions opts;
	runnerOptionsInit(&opts);
	ASSERT_EQ_INT(1, runnerOptionsParse(&opts, 2, argv));
	ASSERT_TRUE(opts.parse_error);
	return 0;
}

static int test_runner_options_parse_metrics_out(void) {
	char *argv[] = {
	        (char *)"rotide_tests",
	        (char *)"--metrics-out",
	        (char *)"tests/metrics.jsonl",
	};
	struct testRunnerOptions opts;
	runnerOptionsInit(&opts);
	ASSERT_EQ_INT(0, runnerOptionsParse(&opts, 3, argv));
	ASSERT_TRUE(opts.metrics_out != NULL);
	ASSERT_EQ_STR("tests/metrics.jsonl", opts.metrics_out);

	char *argv2[] = {(char *)"rotide_tests", (char *)"--metrics-out=foo.jsonl"};
	struct testRunnerOptions opts2;
	runnerOptionsInit(&opts2);
	ASSERT_EQ_INT(0, runnerOptionsParse(&opts2, 2, argv2));
	ASSERT_EQ_STR("foo.jsonl", opts2.metrics_out);

	char *argv3[] = {(char *)"rotide_tests", (char *)"--metrics-out"};
	struct testRunnerOptions opts3;
	runnerOptionsInit(&opts3);
	ASSERT_EQ_INT(1, runnerOptionsParse(&opts3, 2, argv3));
	ASSERT_TRUE(opts3.parse_error);
	return 0;
}

static int test_runner_shuffle_is_deterministic_for_same_seed(void) {
	int a[16];
	int b[16];
	for (int i = 0; i < 16; i++) {
		a[i] = i;
		b[i] = i;
	}
	runnerShuffleIndices(a, 16, 0xDEADBEEFULL);
	runnerShuffleIndices(b, 16, 0xDEADBEEFULL);
	for (int i = 0; i < 16; i++) {
		ASSERT_EQ_INT(a[i], b[i]);
	}
	int seen[16] = {0};
	for (int i = 0; i < 16; i++) {
		ASSERT_TRUE(a[i] >= 0 && a[i] < 16);
		ASSERT_TRUE(!seen[a[i]]);
		seen[a[i]] = 1;
	}
	int identity = 1;
	for (int i = 0; i < 16; i++) {
		if (a[i] != i) {
			identity = 0;
			break;
		}
	}
	ASSERT_TRUE(!identity);
	return 0;
}

static int test_runner_shuffle_differs_for_different_seeds(void) {
	int a[32];
	int b[32];
	for (int i = 0; i < 32; i++) {
		a[i] = i;
		b[i] = i;
	}
	runnerShuffleIndices(a, 32, 1);
	runnerShuffleIndices(b, 32, 2);
	int identical = 1;
	for (int i = 0; i < 32; i++) {
		if (a[i] != b[i]) {
			identical = 0;
			break;
		}
	}
	ASSERT_TRUE(!identical);
	return 0;
}

static int test_quarantine_list_load_parses_dash_entries(void) {
	char path[] = "/tmp/rotide_quarantine_test_XXXXXX";
	int fd = mkstemp(path);
	ASSERT_TRUE(fd >= 0);
	const char *content = "# Header\n"
	                      "\n"
	                      "Random prose line.\n"
	                      "- alpha_test  # trailing comment\n"
	                      "  - beta_test\n"
	                      "\n"
	                      "```\n"
	                      "- ignored_in_fence\n"
	                      "```\n"
	                      "\n"
	                      "- gamma_test\n"
	                      "-not_an_entry\n";
	ssize_t wrote = write(fd, content, strlen(content));
	(void)close(fd);
	ASSERT_EQ_INT((int)strlen(content), (int)wrote);

	struct quarantineList q;
	quarantineListInit(&q);
	char *err = NULL;
	int rc = quarantineListLoad(&q, path, &err);
	(void)unlink(path);
	ASSERT_EQ_INT(0, rc);
	ASSERT_TRUE(err == NULL);
	ASSERT_EQ_INT(3, q.count);
	ASSERT_TRUE(quarantineListContains(&q, "alpha_test"));
	ASSERT_TRUE(quarantineListContains(&q, "beta_test"));
	ASSERT_TRUE(quarantineListContains(&q, "gamma_test"));
	ASSERT_TRUE(!quarantineListContains(&q, "ignored_in_fence"));
	ASSERT_TRUE(!quarantineListContains(&q, "not_an_entry"));
	quarantineListFree(&q);
	return 0;
}

static int test_quarantine_list_missing_file_is_not_error(void) {
	struct quarantineList q;
	quarantineListInit(&q);
	char *err = NULL;
	int rc = quarantineListLoad(&q, "/tmp/rotide_quarantine_definitely_missing_file_xyz", &err);
	ASSERT_EQ_INT(0, rc);
	ASSERT_TRUE(err == NULL);
	ASSERT_EQ_INT(0, q.count);
	quarantineListFree(&q);
	return 0;
}

static int test_snapshot_compare_equal_buffers(void) {
	unsigned char a[64];
	unsigned char b[64];
	for (int i = 0; i < 64; i++) {
		a[i] = (unsigned char)(i * 7 + 3);
		b[i] = a[i];
	}
	size_t diff = 0;
	ASSERT_EQ_INT(1, runnerSnapshotCompare(a, b, sizeof(a), NULL, 0, &diff));
	return 0;
}

static int test_snapshot_compare_reports_first_diff(void) {
	unsigned char a[32] = {0};
	unsigned char b[32] = {0};
	a[5] = 0xaa;
	b[5] = 0xbb;
	a[10] = 0x11;
	b[10] = 0x22;
	size_t diff = 999;
	ASSERT_EQ_INT(0, runnerSnapshotCompare(a, b, sizeof(a), NULL, 0, &diff));
	ASSERT_EQ_INT(5, (int)diff);
	return 0;
}

static int test_snapshot_compare_excludes_skipped_region(void) {
	unsigned char a[32] = {0};
	unsigned char b[32] = {0};
	a[12] = 0xff;
	b[12] = 0x01;
	a[13] = 0xff;
	b[13] = 0x02;
	struct snapshotExcludeRange excludes[] = {{12, 2}};
	size_t diff = 999;
	ASSERT_EQ_INT(1, runnerSnapshotCompare(a, b, sizeof(a), excludes, 1, &diff));
	return 0;
}

static int test_snapshot_compare_detects_diff_outside_excludes(void) {
	unsigned char a[32] = {0};
	unsigned char b[32] = {0};
	a[12] = 0xff;
	b[12] = 0x01;
	a[20] = 0x11;
	b[20] = 0x22;
	struct snapshotExcludeRange excludes[] = {{12, 2}};
	size_t diff = 999;
	ASSERT_EQ_INT(0, runnerSnapshotCompare(a, b, sizeof(a), excludes, 1, &diff));
	ASSERT_EQ_INT(20, (int)diff);
	return 0;
}

static int test_snapshot_compare_exclude_at_buffer_tail(void) {
	unsigned char a[32] = {0};
	unsigned char b[32] = {0};
	a[30] = 0x55;
	b[30] = 0x66;
	a[31] = 0x77;
	b[31] = 0x88;
	struct snapshotExcludeRange excludes[] = {{30, 2}};
	ASSERT_EQ_INT(1, runnerSnapshotCompare(a, b, sizeof(a), excludes, 1, NULL));
	return 0;
}

static int test_snapshot_compare_multiple_excludes(void) {
	unsigned char a[64] = {0};
	unsigned char b[64] = {0};
	a[5] = 1;
	b[5] = 2;
	a[20] = 3;
	b[20] = 4;
	a[50] = 0xff;
	b[50] = 0xee;
	struct snapshotExcludeRange excludes[] = {{5, 1}, {20, 1}};
	size_t diff = 999;
	ASSERT_EQ_INT(0, runnerSnapshotCompare(a, b, sizeof(a), excludes, 2, &diff));
	ASSERT_EQ_INT(50, (int)diff);
	return 0;
}

static int test_seed_setter_roundtrip(void) {
	unsigned long long prev = rotide_test_seed();
	rotide_test_seed_set(0xABCDEF0123456789ULL);
	ASSERT_TRUE(rotide_test_seed() == 0xABCDEF0123456789ULL);
	rotide_test_seed_set(prev);
	return 0;
}

const struct editorTestCase g_runner_internals_tests[] = {
        {"runner_name_matches_substring", test_runner_name_matches_substring},
        {"runner_tags_have_exact_token", test_runner_tags_have_exact_token},
        {"runner_options_parse_basic_flags", test_runner_options_parse_basic_flags},
        {"runner_options_parse_value_flags_split_and_eq",
         test_runner_options_parse_value_flags_split_and_eq},
        {"runner_options_parse_rejects_unknown", test_runner_options_parse_rejects_unknown},
        {"runner_options_parse_rejects_bad_repeat", test_runner_options_parse_rejects_bad_repeat},
        {"runner_options_parse_rejects_missing_value",
         test_runner_options_parse_rejects_missing_value},
        {"runner_options_parse_metrics_out", test_runner_options_parse_metrics_out},
        {"runner_shuffle_is_deterministic_for_same_seed",
         test_runner_shuffle_is_deterministic_for_same_seed},
        {"runner_shuffle_differs_for_different_seeds",
         test_runner_shuffle_differs_for_different_seeds},
        {"quarantine_list_load_parses_dash_entries", test_quarantine_list_load_parses_dash_entries},
        {"quarantine_list_missing_file_is_not_error",
         test_quarantine_list_missing_file_is_not_error},
        {"snapshot_compare_equal_buffers", test_snapshot_compare_equal_buffers},
        {"snapshot_compare_reports_first_diff", test_snapshot_compare_reports_first_diff},
        {"snapshot_compare_excludes_skipped_region", test_snapshot_compare_excludes_skipped_region},
        {"snapshot_compare_detects_diff_outside_excludes",
         test_snapshot_compare_detects_diff_outside_excludes},
        {"snapshot_compare_exclude_at_buffer_tail", test_snapshot_compare_exclude_at_buffer_tail},
        {"snapshot_compare_multiple_excludes", test_snapshot_compare_multiple_excludes},
        {"runner_seed_setter_roundtrip", test_seed_setter_roundtrip},
};

const int g_runner_internals_test_count =
        (int)(sizeof(g_runner_internals_tests) / sizeof(g_runner_internals_tests[0]));
