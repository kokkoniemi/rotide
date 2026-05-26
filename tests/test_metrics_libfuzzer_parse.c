#define _GNU_SOURCE

#include "metrics_libfuzzer_parse.h"
#include "test_case.h"
#include "test_helpers.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

/* Representative output from a `-runs=500 -print_final_stats=1` run of
 * tests/fuzz/lsp/fuzz_lsp against tests/fuzz/lsp/corpus. Kept short by
 * trimming the run lines between the seed pass and the
 * final ones. The exact byte/edge counts are what we assert against. */
static const char *k_smoke_log_lsp =
        "INFO: Running with entropic power schedule (0xFF, 100).\n"
        "INFO: Seed: 1234567890\n"
        "INFO: Loaded 1 modules   (487 inline 8-bit counters): 487 [0x..., 0x...),\n"
        "INFO: Loaded 1 PC tables (487 PCs): 487 [0x..., 0x...),\n"
        "INFO: seed corpus: files: 21 min: 8b max: 64b total: 350b rss: 32Mb\n"
        "#22\tINITED cov: 56 ft: 110 corp: 14/281b exec/s: 0 rss: 33Mb\n"
        "#59\tNEW    cov: 63 ft: 129 corp: 16/445b lim: 64 exec/s: 0 rss: 34Mb L: 23/64 MS: 1 "
        "CopyPart-\n"
        "#196\tREDUCE cov: 64 ft: 152 corp: 24/734b lim: 64 exec/s: 0 rss: 35Mb L: 46/64 MS: 1 "
        "EraseBytes-\n"
        "#457\tNEW    cov: 64 ft: 162 corp: 28/795b lim: 64 exec/s: 0 rss: 36Mb L: 40/56 MS: 1 "
        "ChangeByte-\n"
        "#500\tDONE   cov: 64 ft: 162 corp: 28/795b lim: 64 exec/s: 0 rss: 36Mb\n"
        "###### Recommended dictionary. ######\n"
        "###### End of recommended dictionary. ######\n"
        "Done 500 runs in 0 second(s)\n"
        "stat::number_of_executed_units: 500\n"
        "stat::average_exec_per_sec:     0\n"
        "stat::new_units_added:          23\n"
        "stat::slowest_unit_time_sec:    0\n"
        "stat::peak_rss_mb:              36\n";

static int test_parse_smoke_log_extracts_final_cov_ft_corp(void) {
	struct editorLibFuzzerStats s;
	editorLibFuzzerStatsParse(k_smoke_log_lsp, &s);

	ASSERT_TRUE(s.has_cov_line);
	ASSERT_EQ_INT(64, (int)s.cov_edges);
	ASSERT_EQ_INT(162, (int)s.ft_features);
	ASSERT_EQ_INT(28, (int)s.corp_count);
	ASSERT_EQ_INT(795, (int)s.corp_bytes);
	return 0;
}

static int test_parse_smoke_log_extracts_final_stats(void) {
	struct editorLibFuzzerStats s;
	editorLibFuzzerStatsParse(k_smoke_log_lsp, &s);

	ASSERT_TRUE(s.has_final_stats);
	ASSERT_EQ_INT(500, (int)s.executed_units);
	ASSERT_EQ_INT(0, (int)s.avg_exec_per_sec);
	ASSERT_EQ_INT(23, (int)s.new_units_added);
	ASSERT_EQ_INT(36, (int)s.peak_rss_mb);
	ASSERT_TRUE(s.has_runtime);
	ASSERT_EQ_INT(0, (int)s.runtime_seconds);
	return 0;
}

static int test_parse_unit_suffixes_kb_mb(void) {
	const char *log = "#10\tNEW cov: 1 ft: 2 corp: 1/3Kb lim: 4 exec/s: 0 rss: 10Mb\n";
	struct editorLibFuzzerStats s;
	editorLibFuzzerStatsParse(log, &s);
	ASSERT_TRUE(s.has_cov_line);
	ASSERT_EQ_INT(1024 * 3, (int)s.corp_bytes);

	const char *log2 = "#10\tNEW cov: 1 ft: 2 corp: 1/5Mb lim: 4 exec/s: 0 rss: 10Mb\n";
	editorLibFuzzerStatsParse(log2, &s);
	ASSERT_TRUE(s.has_cov_line);
	ASSERT_EQ_INT(5 * 1024 * 1024, (int)s.corp_bytes);
	return 0;
}

static int test_parse_keeps_last_cov_line(void) {
	const char *log = "#1\tNEW cov: 10 ft: 20 corp: 1/3b lim: 4 exec/s: 0 rss: 1Mb\n"
	                  "#2\tNEW cov: 11 ft: 22 corp: 2/7b lim: 4 exec/s: 0 rss: 1Mb\n"
	                  "#3\tNEW cov: 12 ft: 25 corp: 3/11b lim: 4 exec/s: 0 rss: 1Mb\n";
	struct editorLibFuzzerStats s;
	editorLibFuzzerStatsParse(log, &s);
	ASSERT_TRUE(s.has_cov_line);
	ASSERT_EQ_INT(12, (int)s.cov_edges);
	ASSERT_EQ_INT(25, (int)s.ft_features);
	ASSERT_EQ_INT(3, (int)s.corp_count);
	ASSERT_EQ_INT(11, (int)s.corp_bytes);
	return 0;
}

static int test_parse_no_cov_line_flags_off(void) {
	const char *log = "INFO: Running with entropic power schedule (0xFF, 100).\n"
	                  "some unrelated noise\n"
	                  "Done 1 runs in 0 second(s)\n";
	struct editorLibFuzzerStats s;
	editorLibFuzzerStatsParse(log, &s);
	ASSERT_TRUE(!s.has_cov_line);
	ASSERT_TRUE(s.has_runtime);
	ASSERT_EQ_INT(0, (int)s.runtime_seconds);
	/* "Done N runs" populates executed_units fallback even without
	 * print_final_stats; has_final_stats stays 0 because we never saw
	 * stat:: lines. */
	ASSERT_EQ_INT(1, (int)s.executed_units);
	ASSERT_TRUE(!s.has_final_stats);
	return 0;
}

static int test_parse_handles_empty_and_null(void) {
	struct editorLibFuzzerStats s;
	editorLibFuzzerStatsParse(NULL, &s);
	ASSERT_TRUE(!s.has_cov_line);
	ASSERT_TRUE(!s.has_final_stats);
	ASSERT_TRUE(!s.has_runtime);

	editorLibFuzzerStatsParse("", &s);
	ASSERT_TRUE(!s.has_cov_line);
	return 0;
}

static int test_parse_runtime_seconds_nonzero(void) {
	const char *log = "#1\tNEW cov: 1 ft: 1 corp: 1/3b lim: 4 exec/s: 0 rss: 1Mb\n"
	                  "Done 50000 runs in 42 second(s)\n";
	struct editorLibFuzzerStats s;
	editorLibFuzzerStatsParse(log, &s);
	ASSERT_TRUE(s.has_runtime);
	ASSERT_EQ_INT(42, (int)s.runtime_seconds);
	return 0;
}

static int test_parse_rejects_corp_without_b_suffix(void) {
	/* Defensive: if the format ever drifts and the byte suffix is
	 * missing, we should refuse the line rather than silently report
	 * a wildly wrong byte count. */
	const char *log = "#1\tNEW cov: 1 ft: 1 corp: 1/3 lim: 4 exec/s: 0 rss: 1Mb\n";
	struct editorLibFuzzerStats s;
	editorLibFuzzerStatsParse(log, &s);
	ASSERT_TRUE(!s.has_cov_line);
	return 0;
}

static char *make_corpus_dir(void) {
	const char *tmpdir = getenv("TMPDIR");
	if (tmpdir == NULL || tmpdir[0] == '\0') {
		tmpdir = "/tmp";
	}
	size_t need = strlen(tmpdir) + sizeof("/rotide-corpus-XXXXXX") + 1;
	char *path = (char *)malloc(need);
	if (path == NULL) {
		return NULL;
	}
	(void)snprintf(path, need, "%s/rotide-corpus-XXXXXX", tmpdir);
	if (mkdtemp(path) == NULL) {
		free(path);
		return NULL;
	}
	return path;
}

static int write_file_bytes(const char *dir, const char *name, const char *bytes, size_t len) {
	char path[4096];
	int n = snprintf(path, sizeof(path), "%s/%s", dir, name);
	if (n < 0 || (size_t)n >= sizeof(path)) {
		return -1;
	}
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		return -1;
	}
	ssize_t w = write(fd, bytes, len);
	(void)close(fd);
	return (w == (ssize_t)len) ? 0 : -1;
}

static void rmdir_recursive_simple(const char *dir) {
	DIR *d = opendir(dir);
	if (d == NULL) {
		return;
	}
	struct dirent *ent;
	char path[4096];
	while ((ent = readdir(d)) != NULL) {
		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
			continue;
		}
		(void)snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
		(void)unlink(path);
	}
	(void)closedir(d);
	(void)rmdir(dir);
}

static int test_scan_corpus_counts_files_and_bytes(void) {
	char *dir = make_corpus_dir();
	ASSERT_TRUE(dir != NULL);
	ASSERT_EQ_INT(0, write_file_bytes(dir, "a", "abc", 3));
	ASSERT_EQ_INT(0, write_file_bytes(dir, "b", "12345", 5));
	ASSERT_EQ_INT(0, write_file_bytes(dir, "c", "", 0));
	/* Hidden file must be ignored. */
	ASSERT_EQ_INT(0, write_file_bytes(dir, ".hidden", "xxx", 3));

	long long count = -1;
	long long bytes = -1;
	ASSERT_EQ_INT(0, editorLibFuzzerScanCorpus(dir, &count, &bytes));
	ASSERT_EQ_INT(3, (int)count);
	ASSERT_EQ_INT(8, (int)bytes);

	rmdir_recursive_simple(dir);
	free(dir);
	return 0;
}

static int test_scan_corpus_missing_dir_reports_error(void) {
	long long count = 99;
	long long bytes = 99;
	int rc = editorLibFuzzerScanCorpus("/nonexistent-rotide-corpus-xyz-1234", &count, &bytes);
	ASSERT_TRUE(rc != 0);
	/* Zeroed on failure for safe defaults. */
	ASSERT_EQ_INT(0, (int)count);
	ASSERT_EQ_INT(0, (int)bytes);
	return 0;
}

const struct editorTestCase g_metrics_libfuzzer_parse_tests[] = {
        {"libfuzzer_parse_smoke_log_extracts_final_cov_ft_corp",
         test_parse_smoke_log_extracts_final_cov_ft_corp},
        {"libfuzzer_parse_smoke_log_extracts_final_stats",
         test_parse_smoke_log_extracts_final_stats},
        {"libfuzzer_parse_unit_suffixes_kb_mb", test_parse_unit_suffixes_kb_mb},
        {"libfuzzer_parse_keeps_last_cov_line", test_parse_keeps_last_cov_line},
        {"libfuzzer_parse_no_cov_line_flags_off", test_parse_no_cov_line_flags_off},
        {"libfuzzer_parse_handles_empty_and_null", test_parse_handles_empty_and_null},
        {"libfuzzer_parse_runtime_seconds_nonzero", test_parse_runtime_seconds_nonzero},
        {"libfuzzer_parse_rejects_corp_without_b_suffix", test_parse_rejects_corp_without_b_suffix},
        {"libfuzzer_scan_corpus_counts_files_and_bytes", test_scan_corpus_counts_files_and_bytes},
        {"libfuzzer_scan_corpus_missing_dir_reports_error",
         test_scan_corpus_missing_dir_reports_error},
};

const int g_metrics_libfuzzer_parse_test_count =
        (int)(sizeof(g_metrics_libfuzzer_parse_tests) / sizeof(g_metrics_libfuzzer_parse_tests[0]));
