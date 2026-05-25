#define _DEFAULT_SOURCE

#include "metrics_jsonl_read.h"
#include "metrics_summary_cmd.h"
#include "test_case.h"
#include "test_helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int test_parse_test_run_row(void) {
	const char *line = "{\"kind\":\"test_run\",\"ts\":\"2026-05-19T13:35:42Z\","
	                   "\"git_sha\":\"deadbee\",\"wall_seconds\":4.812,"
	                   "\"total_runs\":820,\"passed_runs\":820,\"failed_unique\":0,"
	                   "\"crashes\":0,\"reset_violations\":0,\"flakes\":2,"
	                   "\"property_ops\":50000,\"property_ops_seconds\":0.5,"
	                   "\"jobs\":4,"
	                   "\"repeat\":1,\"seed\":\"0x0123456789abcdef\","
	                   "\"shuffle\":false,\"validate_reset\":true,\"exit_code\":0}";
	struct editorMetricsRow r;
	ASSERT_EQ_INT(1, editorMetricsRowParse(line, &r));
	ASSERT_TRUE(r.kind == EDITOR_METRICS_KIND_TEST_RUN);
	ASSERT_EQ_STR("2026-05-19T13:35:42Z", r.ts);
	ASSERT_TRUE(r.ts_unix > 0);
	ASSERT_EQ_STR("deadbee", r.git_sha);
	ASSERT_TRUE(r.wall_seconds > 4.811 && r.wall_seconds < 4.813);
	ASSERT_EQ_INT(820, (int)r.total_runs);
	ASSERT_EQ_INT(820, (int)r.passed_runs);
	ASSERT_EQ_INT(0, (int)r.failed_unique);
	ASSERT_EQ_INT(2, (int)r.flakes);
	ASSERT_EQ_INT(50000, (int)r.property_ops);
	ASSERT_TRUE(r.property_ops_seconds > 0.49 && r.property_ops_seconds < 0.51);
	ASSERT_EQ_INT(0, (int)r.exit_code);
	return 0;
}

static int test_parse_bench_row(void) {
	const char *line = "{\"kind\":\"bench\",\"ts\":\"2026-05-19T13:35:54Z\","
	                   "\"name\":\"screen_diff_unchanged_frame\","
	                   "\"samples\":20,\"inner_ops\":8,"
	                   "\"min_ns\":76137,\"p50_ns\":79550.625,"
	                   "\"p95_ns\":80218.425,\"iqr_ns\":2077.8125}";
	struct editorMetricsRow r;
	ASSERT_EQ_INT(1, editorMetricsRowParse(line, &r));
	ASSERT_TRUE(r.kind == EDITOR_METRICS_KIND_BENCH);
	ASSERT_EQ_STR("screen_diff_unchanged_frame", r.bench_name);
	ASSERT_EQ_INT(20, (int)r.samples);
	ASSERT_EQ_INT(8, (int)r.inner_ops);
	ASSERT_TRUE(r.p50_ns > 79550.0 && r.p50_ns < 79551.0);
	ASSERT_TRUE(r.iqr_ns > 2077.0 && r.iqr_ns < 2078.0);
	return 0;
}

static int test_parse_fuzz_row(void) {
	const char *line = "{\"kind\":\"fuzz\",\"ts\":\"2026-05-19T16:42:47Z\","
	                   "\"target\":\"lsp\",\"cov_edges\":64,\"ft_features\":193,"
	                   "\"corp_count\":35,\"corp_bytes\":1155,"
	                   "\"corpus_files\":45,\"corpus_bytes\":1556,"
	                   "\"executed_units\":500,\"avg_exec_per_sec\":0,"
	                   "\"new_units_added\":30,\"peak_rss_mb\":36,"
	                   "\"runtime_seconds\":0,\"has_final_stats\":true}";
	struct editorMetricsRow r;
	ASSERT_EQ_INT(1, editorMetricsRowParse(line, &r));
	ASSERT_TRUE(r.kind == EDITOR_METRICS_KIND_FUZZ);
	ASSERT_EQ_STR("lsp", r.fuzz_target);
	ASSERT_EQ_INT(64, (int)r.cov_edges);
	ASSERT_EQ_INT(193, (int)r.ft_features);
	ASSERT_EQ_INT(35, (int)r.corp_count);
	ASSERT_EQ_INT(1155, (int)r.corp_bytes);
	ASSERT_EQ_INT(45, (int)r.corpus_files);
	ASSERT_EQ_INT(1556, (int)r.corpus_bytes);
	ASSERT_EQ_INT(500, (int)r.executed_units);
	ASSERT_EQ_INT(30, (int)r.new_units_added);
	return 0;
}

static int test_parse_rejects_missing_kind(void) {
	const char *line = "{\"ts\":\"2026-05-19T13:00:00Z\",\"x\":1}";
	struct editorMetricsRow r;
	ASSERT_EQ_INT(0, editorMetricsRowParse(line, &r));
	return 0;
}

static int test_parse_rejects_missing_ts(void) {
	const char *line = "{\"kind\":\"test_run\",\"x\":1}";
	struct editorMetricsRow r;
	ASSERT_EQ_INT(0, editorMetricsRowParse(line, &r));
	return 0;
}

static int test_parse_unknown_kind_keeps_ts(void) {
	const char *line = "{\"kind\":\"future\",\"ts\":\"2026-05-19T13:00:00Z\"}";
	struct editorMetricsRow r;
	ASSERT_EQ_INT(1, editorMetricsRowParse(line, &r));
	ASSERT_TRUE(r.kind == EDITOR_METRICS_KIND_UNKNOWN);
	ASSERT_EQ_STR("2026-05-19T13:00:00Z", r.ts);
	return 0;
}

static int test_parse_unknown_keys_ignored(void) {
	const char *line = "{\"kind\":\"bench\",\"ts\":\"2026-05-19T13:00:00Z\","
	                   "\"name\":\"n\",\"unknown_future_field\":\"hello\","
	                   "\"p50_ns\":100}";
	struct editorMetricsRow r;
	ASSERT_EQ_INT(1, editorMetricsRowParse(line, &r));
	ASSERT_EQ_STR("n", r.bench_name);
	ASSERT_TRUE(r.p50_ns > 99.9 && r.p50_ns < 100.1);
	return 0;
}

static int test_parse_string_escapes(void) {
	const char *line = "{\"kind\":\"bench\",\"ts\":\"2026-05-19T13:00:00Z\","
	                   "\"name\":\"a\\\"b\\\\c\",\"p50_ns\":1}";
	struct editorMetricsRow r;
	ASSERT_EQ_INT(1, editorMetricsRowParse(line, &r));
	ASSERT_EQ_STR("a\"b\\c", r.bench_name);
	return 0;
}

static int test_parse_ts_iso_unix(void) {
	const char *line = "{\"kind\":\"test_run\",\"ts\":\"2025-05-19T11:54:56Z\"}";
	struct editorMetricsRow r;
	ASSERT_EQ_INT(1, editorMetricsRowParse(line, &r));
	ASSERT_EQ_INT(1747655696, (int)r.ts_unix);
	return 0;
}

static char *tmpfile_path(void) {
	const char *tmpdir = getenv("TMPDIR");
	if (tmpdir == NULL || tmpdir[0] == '\0') {
		tmpdir = "/tmp";
	}
	size_t need = strlen(tmpdir) + sizeof("/rotide-metrics-XXXXXX") + 1;
	char *path = (char *)malloc(need);
	(void)snprintf(path, need, "%s/rotide-metrics-XXXXXX", tmpdir);
	int fd = mkstemp(path);
	if (fd < 0) {
		free(path);
		return NULL;
	}
	(void)close(fd);
	return path;
}

static int write_file_contents(const char *path, const char *bytes) {
	FILE *f = fopen(path, "w");
	if (f == NULL) {
		return -1;
	}
	int n = fputs(bytes, f);
	(void)fclose(f);
	return (n >= 0) ? 0 : -1;
}

static int test_load_file_with_two_rows_and_blank_and_bad(void) {
	char *path = tmpfile_path();
	ASSERT_TRUE(path != NULL);
	const char *content =
	        "{\"kind\":\"test_run\",\"ts\":\"2026-05-19T13:00:00Z\",\"exit_code\":0}\n"
	        "\n"
	        "this is not json\n"
	        "{\"kind\":\"bench\",\"ts\":\"2026-05-19T13:01:00Z\",\"name\":\"b\",\"p50_ns\":1}"
	        "\n";
	ASSERT_EQ_INT(0, write_file_contents(path, content));

	struct editorMetricsRow *rows = NULL;
	int count = 0;
	int skipped = 0;
	ASSERT_EQ_INT(0, editorMetricsRowsLoad(path, &rows, &count, &skipped));
	ASSERT_EQ_INT(2, count);
	ASSERT_EQ_INT(1, skipped);
	ASSERT_TRUE(rows[0].kind == EDITOR_METRICS_KIND_TEST_RUN);
	ASSERT_TRUE(rows[1].kind == EDITOR_METRICS_KIND_BENCH);
	editorMetricsRowsFree(rows, count);
	(void)unlink(path);
	free(path);
	return 0;
}

static int test_load_missing_file_returns_error(void) {
	struct editorMetricsRow *rows = NULL;
	int count = 0;
	ASSERT_EQ_INT(-1, editorMetricsRowsLoad("/nonexistent-rotide-x123", &rows, &count, NULL));
	return 0;
}

/* Build a small in-memory row set with one entry per call so order is
 * deterministic. Each row carries a synthetic ts_unix offset, which
 * lets the comparator pick "latest" without depending on the parser's
 * timegm() behaviour on the host. */

static void seed_test_run(struct editorMetricsRow *r, long long ts_unix, int exit_code) {
	editorMetricsRowInit(r);
	r->kind = EDITOR_METRICS_KIND_TEST_RUN;
	(void)snprintf(r->ts, sizeof(r->ts), "ts+%lld", ts_unix);
	r->ts_unix = ts_unix;
	r->exit_code = exit_code;
}

static void seed_bench(struct editorMetricsRow *r, long long ts_unix, const char *name,
                       double p50_ns, double iqr_ns) {
	editorMetricsRowInit(r);
	r->kind = EDITOR_METRICS_KIND_BENCH;
	(void)snprintf(r->ts, sizeof(r->ts), "ts+%lld", ts_unix);
	r->ts_unix = ts_unix;
	(void)snprintf(r->bench_name, sizeof(r->bench_name), "%s", name);
	r->p50_ns = p50_ns;
	r->iqr_ns = iqr_ns;
}

static void seed_fuzz(struct editorMetricsRow *r, long long ts_unix, const char *target,
                      long long cov_edges) {
	editorMetricsRowInit(r);
	r->kind = EDITOR_METRICS_KIND_FUZZ;
	(void)snprintf(r->ts, sizeof(r->ts), "ts+%lld", ts_unix);
	r->ts_unix = ts_unix;
	(void)snprintf(r->fuzz_target, sizeof(r->fuzz_target), "%s", target);
	r->cov_edges = cov_edges;
}

/* tmpfile() + rewind() + fread() gives us a portable FILE* sink without
 * open_memstream (which requires _GNU_SOURCE, in conflict with the
 * project-wide _DEFAULT_SOURCE setting). */
static char *captured_stdout = NULL;

static FILE *capture_open(void) {
	free(captured_stdout);
	captured_stdout = NULL;
	return tmpfile();
}

static void capture_close(FILE *f) {
	if (f == NULL) {
		return;
	}
	(void)fflush(f);
	long len = ftell(f);
	if (len < 0) {
		(void)fclose(f);
		return;
	}
	rewind(f);
	captured_stdout = (char *)malloc((size_t)len + 1);
	if (captured_stdout != NULL) {
		size_t got = fread(captured_stdout, 1, (size_t)len, f);
		captured_stdout[got] = '\0';
	}
	(void)fclose(f);
}

static int test_summary_groups_kinds(void) {
	struct editorMetricsRow rows[4];
	seed_test_run(&rows[0], 1000, 0);
	seed_bench(&rows[1], 1100, "bench_a", 100.0, 5.0);
	seed_bench(&rows[2], 1200, "bench_a", 110.0, 5.0);
	seed_fuzz(&rows[3], 1300, "vterm", 200);

	struct editorMetricsCmdOptions opts;
	editorMetricsCmdOptionsInit(&opts);
	FILE *m = capture_open();
	ASSERT_EQ_INT(0, editorMetricsCmdSummary(rows, 4, &opts, m));
	capture_close(m);

	ASSERT_TRUE(strstr(captured_stdout, "== test_run") != NULL);
	ASSERT_TRUE(strstr(captured_stdout, "== bench: bench_a") != NULL);
	ASSERT_TRUE(strstr(captured_stdout, "== fuzz: vterm") != NULL);
	free(captured_stdout);
	captured_stdout = NULL;
	return 0;
}

static int test_summary_kind_filter(void) {
	struct editorMetricsRow rows[3];
	seed_test_run(&rows[0], 1000, 0);
	seed_bench(&rows[1], 1100, "b", 100.0, 5.0);
	seed_fuzz(&rows[2], 1200, "vterm", 1);

	struct editorMetricsCmdOptions opts;
	editorMetricsCmdOptionsInit(&opts);
	opts.kind_filter = EDITOR_METRICS_KIND_BENCH;
	FILE *m = capture_open();
	editorMetricsCmdSummary(rows, 3, &opts, m);
	capture_close(m);

	ASSERT_TRUE(strstr(captured_stdout, "== bench: b") != NULL);
	ASSERT_TRUE(strstr(captured_stdout, "test_run") == NULL);
	ASSERT_TRUE(strstr(captured_stdout, "fuzz") == NULL);
	free(captured_stdout);
	captured_stdout = NULL;
	return 0;
}

static int test_summary_limit_truncates_oldest(void) {
	struct editorMetricsRow rows[5];
	for (int i = 0; i < 5; i++) {
		seed_bench(&rows[i], 1000 + i, "b", 100.0 + i, 5.0);
	}
	struct editorMetricsCmdOptions opts;
	editorMetricsCmdOptionsInit(&opts);
	opts.summary_limit = 3;
	FILE *m = capture_open();
	editorMetricsCmdSummary(rows, 5, &opts, m);
	capture_close(m);

	ASSERT_TRUE(strstr(captured_stdout, "2 earlier omitted") != NULL);
	/* Earliest two should be elided. */
	ASSERT_TRUE(strstr(captured_stdout, "ts+1000") == NULL);
	ASSERT_TRUE(strstr(captured_stdout, "ts+1001") == NULL);
	ASSERT_TRUE(strstr(captured_stdout, "ts+1004") != NULL);
	free(captured_stdout);
	captured_stdout = NULL;
	return 0;
}

static int test_fuzz_stale_unchanged_returns_one(void) {
	struct editorMetricsRow rows[2];
	seed_fuzz(&rows[0], 1000, "vterm", 200);
	seed_fuzz(&rows[1], 1000 + 24 * 3600, "vterm", 200); /* 24h later, no growth */

	struct editorMetricsCmdOptions opts;
	editorMetricsCmdOptionsInit(&opts);
	opts.window_hours = 48;
	FILE *m = capture_open();
	int rc = editorMetricsCmdCheckFuzzStale(rows, 2, &opts, m);
	capture_close(m);

	ASSERT_EQ_INT(1, rc);
	ASSERT_TRUE(strstr(captured_stdout, "STALE") != NULL);
	free(captured_stdout);
	captured_stdout = NULL;
	return 0;
}

static int test_fuzz_stale_growing_returns_zero(void) {
	struct editorMetricsRow rows[2];
	seed_fuzz(&rows[0], 1000, "vterm", 200);
	seed_fuzz(&rows[1], 1000 + 24 * 3600, "vterm", 230);

	struct editorMetricsCmdOptions opts;
	editorMetricsCmdOptionsInit(&opts);
	opts.window_hours = 48;
	FILE *m = capture_open();
	int rc = editorMetricsCmdCheckFuzzStale(rows, 2, &opts, m);
	capture_close(m);

	ASSERT_EQ_INT(0, rc);
	ASSERT_TRUE(strstr(captured_stdout, "ok") != NULL);
	ASSERT_TRUE(strstr(captured_stdout, "STALE") == NULL);
	free(captured_stdout);
	captured_stdout = NULL;
	return 0;
}

static int test_fuzz_stale_single_row_in_window_skips(void) {
	struct editorMetricsRow rows[1];
	seed_fuzz(&rows[0], 1000, "vterm", 100);

	struct editorMetricsCmdOptions opts;
	editorMetricsCmdOptionsInit(&opts);
	opts.window_hours = 1;
	FILE *m = capture_open();
	int rc = editorMetricsCmdCheckFuzzStale(rows, 1, &opts, m);
	capture_close(m);

	ASSERT_EQ_INT(0, rc);
	ASSERT_TRUE(strstr(captured_stdout, "insufficient data") != NULL);
	free(captured_stdout);
	captured_stdout = NULL;
	return 0;
}

static int test_fuzz_stale_target_filter(void) {
	struct editorMetricsRow rows[4];
	seed_fuzz(&rows[0], 1000, "vterm", 100);
	seed_fuzz(&rows[1], 2000, "vterm", 100); /* stale */
	seed_fuzz(&rows[2], 1000, "lsp", 50);
	seed_fuzz(&rows[3], 2000, "lsp", 80); /* growing */

	struct editorMetricsCmdOptions opts;
	editorMetricsCmdOptionsInit(&opts);
	opts.window_hours = 48;
	opts.target_filter = "lsp";
	FILE *m = capture_open();
	int rc = editorMetricsCmdCheckFuzzStale(rows, 4, &opts, m);
	capture_close(m);

	ASSERT_EQ_INT(0, rc);
	ASSERT_TRUE(strstr(captured_stdout, "target=lsp") != NULL);
	/* vterm row should be filtered out entirely. */
	ASSERT_TRUE(strstr(captured_stdout, "target=vterm") == NULL);
	free(captured_stdout);
	captured_stdout = NULL;
	return 0;
}

static int test_bench_regression_p50_jump_returns_one(void) {
	struct editorMetricsRow rows[2];
	seed_bench(&rows[0], 1000, "b", 100.0, 5.0);
	seed_bench(&rows[1], 2000, "b", 200.0, 5.0); /* delta 100 > 3*5 */

	struct editorMetricsCmdOptions opts;
	editorMetricsCmdOptionsInit(&opts);
	FILE *m = capture_open();
	int rc = editorMetricsCmdCheckBenchRegression(rows, 2, &opts, m);
	capture_close(m);

	ASSERT_EQ_INT(1, rc);
	ASSERT_TRUE(strstr(captured_stdout, "REGRESSION") != NULL);
	free(captured_stdout);
	captured_stdout = NULL;
	return 0;
}

static int test_bench_regression_within_iqr_band_returns_zero(void) {
	struct editorMetricsRow rows[2];
	seed_bench(&rows[0], 1000, "b", 100.0, 5.0);
	/* delta 10 <= 3 * 5 = 15 */
	seed_bench(&rows[1], 2000, "b", 110.0, 5.0);

	struct editorMetricsCmdOptions opts;
	editorMetricsCmdOptionsInit(&opts);
	FILE *m = capture_open();
	int rc = editorMetricsCmdCheckBenchRegression(rows, 2, &opts, m);
	capture_close(m);

	ASSERT_EQ_INT(0, rc);
	ASSERT_TRUE(strstr(captured_stdout, "ok") != NULL);
	free(captured_stdout);
	captured_stdout = NULL;
	return 0;
}

static int test_bench_regression_single_row_skips(void) {
	struct editorMetricsRow rows[1];
	seed_bench(&rows[0], 1000, "b", 100.0, 5.0);

	struct editorMetricsCmdOptions opts;
	editorMetricsCmdOptionsInit(&opts);
	FILE *m = capture_open();
	int rc = editorMetricsCmdCheckBenchRegression(rows, 1, &opts, m);
	capture_close(m);

	ASSERT_EQ_INT(0, rc);
	ASSERT_TRUE(strstr(captured_stdout, "need >= 2") != NULL);
	free(captured_stdout);
	captured_stdout = NULL;
	return 0;
}

static int test_bench_regression_factor_override(void) {
	struct editorMetricsRow rows[2];
	seed_bench(&rows[0], 1000, "b", 100.0, 5.0);
	seed_bench(&rows[1], 2000, "b", 120.0, 5.0); /* delta 20 */

	struct editorMetricsCmdOptions opts;
	editorMetricsCmdOptionsInit(&opts);
	/* With factor 3.0 default: threshold 15 → regression. */
	FILE *m = capture_open();
	ASSERT_EQ_INT(1, editorMetricsCmdCheckBenchRegression(rows, 2, &opts, m));
	capture_close(m);
	free(captured_stdout);
	captured_stdout = NULL;

	/* With factor 5.0: threshold 25 → ok. */
	opts.regression_factor = 5.0;
	m = capture_open();
	ASSERT_EQ_INT(0, editorMetricsCmdCheckBenchRegression(rows, 2, &opts, m));
	capture_close(m);
	free(captured_stdout);
	captured_stdout = NULL;
	return 0;
}

const struct editorTestCase g_metrics_summary_tests[] = {
        {"metrics_parse_test_run_row", test_parse_test_run_row},
        {"metrics_parse_bench_row", test_parse_bench_row},
        {"metrics_parse_fuzz_row", test_parse_fuzz_row},
        {"metrics_parse_rejects_missing_kind", test_parse_rejects_missing_kind},
        {"metrics_parse_rejects_missing_ts", test_parse_rejects_missing_ts},
        {"metrics_parse_unknown_kind_keeps_ts", test_parse_unknown_kind_keeps_ts},
        {"metrics_parse_unknown_keys_ignored", test_parse_unknown_keys_ignored},
        {"metrics_parse_string_escapes", test_parse_string_escapes},
        {"metrics_parse_ts_iso_unix", test_parse_ts_iso_unix},
        {"metrics_load_file_with_two_rows_and_blank_and_bad",
         test_load_file_with_two_rows_and_blank_and_bad},
        {"metrics_load_missing_file_returns_error", test_load_missing_file_returns_error},
        {"metrics_summary_groups_kinds", test_summary_groups_kinds},
        {"metrics_summary_kind_filter", test_summary_kind_filter},
        {"metrics_summary_limit_truncates_oldest", test_summary_limit_truncates_oldest},
        {"metrics_fuzz_stale_unchanged_returns_one", test_fuzz_stale_unchanged_returns_one},
        {"metrics_fuzz_stale_growing_returns_zero", test_fuzz_stale_growing_returns_zero},
        {"metrics_fuzz_stale_single_row_in_window_skips",
         test_fuzz_stale_single_row_in_window_skips},
        {"metrics_fuzz_stale_target_filter", test_fuzz_stale_target_filter},
        {"metrics_bench_regression_p50_jump_returns_one",
         test_bench_regression_p50_jump_returns_one},
        {"metrics_bench_regression_within_iqr_band_returns_zero",
         test_bench_regression_within_iqr_band_returns_zero},
        {"metrics_bench_regression_single_row_skips", test_bench_regression_single_row_skips},
        {"metrics_bench_regression_factor_override", test_bench_regression_factor_override},
};

const int g_metrics_summary_test_count =
        (int)(sizeof(g_metrics_summary_tests) / sizeof(g_metrics_summary_tests[0]));
