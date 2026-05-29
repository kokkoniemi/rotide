#include "metrics_jsonl_read.h"
#include "metrics_render_svg.h"
#include "metrics_summary_cmd.h"
#include "test_case.h"
#include "test_helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static char *captured = NULL;

static FILE *capture_open(void) {
	free(captured);
	captured = NULL;
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
	captured = (char *)malloc((size_t)len + 1);
	if (captured != NULL) {
		size_t got = fread(captured, 1, (size_t)len, f);
		captured[got] = '\0';
	}
	(void)fclose(f);
}

static long long iso_date_to_unix(const char *iso_date) {
	char *end = NULL;
	long y = strtol(iso_date, &end, 10);
	if (end == NULL || *end != '-') {
		return 0;
	}
	long mo = strtol(end + 1, &end, 10);
	if (end == NULL || *end != '-') {
		return 0;
	}
	long d = strtol(end + 1, &end, 10);
	struct tm tm_buf = {0};
	tm_buf.tm_year = (int)y - 1900;
	tm_buf.tm_mon = (int)mo - 1;
	tm_buf.tm_mday = (int)d;
	tm_buf.tm_hour = 12;
	return (long long)timegm(&tm_buf);
}

static void seed_bench(struct editorMetricsRow *r, const char *iso_date, const char *name,
                       double p50, double p95) {
	editorMetricsRowInit(r);
	r->kind = EDITOR_METRICS_KIND_BENCH;
	(void)snprintf(r->ts, sizeof(r->ts), "%sT12:00:00Z", iso_date);
	r->ts_unix = iso_date_to_unix(iso_date);
	(void)snprintf(r->bench_name, sizeof(r->bench_name), "%s", name);
	r->p50_ns = p50;
	r->p95_ns = p95;
}

static void seed_fuzz(struct editorMetricsRow *r, const char *iso_date, const char *target,
                      long long cov, long long corpus_bytes) {
	editorMetricsRowInit(r);
	r->kind = EDITOR_METRICS_KIND_FUZZ;
	(void)snprintf(r->ts, sizeof(r->ts), "%sT12:00:00Z", iso_date);
	r->ts_unix = iso_date_to_unix(iso_date);
	(void)snprintf(r->fuzz_target, sizeof(r->fuzz_target), "%s", target);
	r->cov_edges = cov;
	r->corpus_bytes = corpus_bytes;
}

static int test_chart_emits_svg_header_and_polyline(void) {
	const double v_p50[3] = {1000, 1050, 1100};
	const double v_p95[3] = {2000, 2100, 2200};
	const char *labels[3] = {"2026-05-24", "2026-05-25", "2026-05-26"};
	struct editorSvgSeries ser[2];
	ser[0].values = v_p50;
	ser[0].label = "p50";
	ser[0].color = "#1f77b4";
	ser[1].values = v_p95;
	ser[1].label = "p95";
	ser[1].color = "#ff7f0e";

	FILE *f = capture_open();
	editorMetricsRenderSvgChart(f, "demo", "ns", labels, 3, ser, 2);
	capture_close(f);

	ASSERT_TRUE(strstr(captured, "<?xml version=\"1.0\"") != NULL);
	ASSERT_TRUE(strstr(captured, "<svg xmlns=\"http://www.w3.org/2000/svg\"") != NULL);
	ASSERT_TRUE(strstr(captured, ">demo</text>") != NULL);
	ASSERT_TRUE(strstr(captured, "stroke=\"#1f77b4\"") != NULL);
	ASSERT_TRUE(strstr(captured, "stroke=\"#ff7f0e\"") != NULL);
	/* n <= SVG_MAX_X_TICKS so every date label survives thinning. */
	ASSERT_TRUE(strstr(captured, ">2026-05-24</text>") != NULL);
	ASSERT_TRUE(strstr(captured, ">2026-05-25</text>") != NULL);
	ASSERT_TRUE(strstr(captured, ">2026-05-26</text>") != NULL);
	ASSERT_TRUE(strstr(captured, ">p50</text>") != NULL);
	ASSERT_TRUE(strstr(captured, ">p95</text>") != NULL);
	ASSERT_TRUE(strstr(captured, "</svg>") != NULL);
	free(captured);
	captured = NULL;
	return 0;
}

static int test_chart_xml_escapes_title(void) {
	const double v[2] = {1, 2};
	const char *labels[2] = {"a", "b"};
	struct editorSvgSeries ser = {v, "s", "#000"};
	FILE *f = capture_open();
	editorMetricsRenderSvgChart(f, "<script>&\"bad", NULL, labels, 2, &ser, 1);
	capture_close(f);
	/* The raw substring must NOT appear; the escaped form must. */
	ASSERT_TRUE(strstr(captured, "<script>&\"bad") == NULL);
	ASSERT_TRUE(strstr(captured, "&lt;script&gt;&amp;&quot;bad") != NULL);
	free(captured);
	captured = NULL;
	return 0;
}

/* For 30 points we expect ~6 date ticks rendered (not 30), so labels stay
 * legible. Verify count is well under 30. */
static int test_chart_thins_ticks_for_many_points(void) {
	double v[30];
	const char *labels[30];
	char label_buf[30][32];
	for (int i = 0; i < 30; i++) {
		v[i] = (double)i;
		(void)snprintf(label_buf[i], sizeof(label_buf[i]), "2026-05-%02d", i + 1);
		labels[i] = label_buf[i];
	}
	struct editorSvgSeries ser = {v, "v", "#000"};
	FILE *f = capture_open();
	editorMetricsRenderSvgChart(f, "many", "v", labels, 30, &ser, 1);
	capture_close(f);

	/* Count rendered date labels by looking for the "2026-05-" prefix
	 * inside <text>...</text>. */
	int label_hits = 0;
	const char *p = captured;
	while ((p = strstr(p, ">2026-05-")) != NULL) {
		label_hits++;
		p++;
	}
	ASSERT_TRUE(label_hits >= 2);
	ASSERT_TRUE(label_hits <= 8);
	/* First and last point's labels must always be in the rendered set. */
	ASSERT_TRUE(strstr(captured, ">2026-05-01</text>") != NULL);
	ASSERT_TRUE(strstr(captured, ">2026-05-30</text>") != NULL);
	free(captured);
	captured = NULL;
	return 0;
}

static int test_chart_skips_when_fewer_than_two_points(void) {
	const double v[1] = {42.0};
	const char *labels[1] = {"a"};
	struct editorSvgSeries ser = {v, "s", "#000"};
	FILE *f = capture_open();
	editorMetricsRenderSvgChart(f, "t", NULL, labels, 1, &ser, 1);
	capture_close(f);
	/* With n_points<2 the renderer emits nothing. */
	ASSERT_EQ_INT(0, (int)strlen(captured));
	free(captured);
	captured = NULL;
	return 0;
}

static int file_exists(const char *path) {
	struct stat st;
	return stat(path, &st) == 0 ? 1 : 0;
}

static int read_file_into(const char *path, char *buf, size_t buf_sz) {
	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		return 0;
	}
	size_t got = fread(buf, 1, buf_sz - 1, f);
	buf[got] = '\0';
	(void)fclose(f);
	return 1;
}

static void make_tmpdir(char *out, size_t out_sz) {
	(void)snprintf(out, out_sz, "/tmp/rotide_metrics_svg_XXXXXX");
	if (mkdtemp(out) == NULL) {
		out[0] = '\0';
	}
}

static int rmrf_dir(const char *dir) {
	char cmd[1024];
	(void)snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
	return system(cmd);
}

static int test_cmd_render_writes_files_and_manifest(void) {
	struct editorMetricsRow rows[5];
	seed_bench(&rows[0], "2026-05-24", "screen_diff", 1000, 2000);
	seed_bench(&rows[1], "2026-05-25", "screen_diff", 1050, 2100);
	seed_bench(&rows[2], "2026-05-26", "screen_diff", 1100, 2200);
	seed_fuzz(&rows[3], "2026-05-25", "lsp", 100, 5000);
	seed_fuzz(&rows[4], "2026-05-26", "lsp", 120, 6000);

	struct editorMetricsCmdOptions opts;
	editorMetricsCmdOptionsInit(&opts);
	char dir[64];
	make_tmpdir(dir, sizeof(dir));
	ASSERT_TRUE(dir[0] != '\0');

	int n = editorMetricsCmdRenderSvg(rows, 5, &opts, 0, dir);
	ASSERT_EQ_INT(4, n);

	char p[256];
	(void)snprintf(p, sizeof(p), "%s/bench-screen_diff.svg", dir);
	ASSERT_TRUE(file_exists(p));
	(void)snprintf(p, sizeof(p), "%s/fuzz-lsp-cov.svg", dir);
	ASSERT_TRUE(file_exists(p));
	(void)snprintf(p, sizeof(p), "%s/fuzz-lsp-corpus.svg", dir);
	ASSERT_TRUE(file_exists(p));
	(void)snprintf(p, sizeof(p), "%s/fuzz-lsp-throughput.svg", dir);
	ASSERT_TRUE(file_exists(p));

	char manifest[2048];
	(void)snprintf(p, sizeof(p), "%s/index.txt", dir);
	ASSERT_TRUE(read_file_into(p, manifest, sizeof(manifest)));
	ASSERT_TRUE(strstr(manifest, "bench-screen_diff.svg") != NULL);
	ASSERT_TRUE(strstr(manifest, "fuzz-lsp-cov.svg") != NULL);
	ASSERT_TRUE(strstr(manifest, "fuzz-lsp-corpus.svg") != NULL);

	(void)rmrf_dir(dir);
	return 0;
}

static int test_cmd_render_sanitizes_filenames(void) {
	struct editorMetricsRow rows[2];
	seed_bench(&rows[0], "2026-05-25", "weird name/with$chars", 100, 200);
	seed_bench(&rows[1], "2026-05-26", "weird name/with$chars", 110, 210);

	struct editorMetricsCmdOptions opts;
	editorMetricsCmdOptionsInit(&opts);
	char dir[64];
	make_tmpdir(dir, sizeof(dir));
	ASSERT_TRUE(dir[0] != '\0');

	int n = editorMetricsCmdRenderSvg(rows, 2, &opts, 0, dir);
	ASSERT_EQ_INT(1, n);

	char p[256];
	(void)snprintf(p, sizeof(p), "%s/bench-weird_name_with_chars.svg", dir);
	ASSERT_TRUE(file_exists(p));

	(void)rmrf_dir(dir);
	return 0;
}

static int test_cmd_render_skips_single_point_series(void) {
	struct editorMetricsRow rows[2];
	seed_bench(&rows[0], "2026-05-25", "only_one", 100, 200);
	seed_fuzz(&rows[1], "2026-05-26", "lone_target", 50, 1000);

	struct editorMetricsCmdOptions opts;
	editorMetricsCmdOptionsInit(&opts);
	char dir[64];
	make_tmpdir(dir, sizeof(dir));

	int n = editorMetricsCmdRenderSvg(rows, 2, &opts, 0, dir);
	ASSERT_EQ_INT(0, n);
	(void)rmrf_dir(dir);
	return 0;
}

static void seed_test_run(struct editorMetricsRow *r, const char *iso_date, double wall_seconds,
                          long long crashes, long long failed, long long flakes, long long repeat) {
	editorMetricsRowInit(r);
	r->kind = EDITOR_METRICS_KIND_TEST_RUN;
	(void)snprintf(r->ts, sizeof(r->ts), "%sT12:00:00Z", iso_date);
	r->ts_unix = iso_date_to_unix(iso_date);
	r->wall_seconds = wall_seconds;
	r->crashes = crashes;
	r->failed_unique = failed;
	r->flakes = flakes;
	r->repeat = repeat;
}

static int test_cmd_render_emits_test_run_charts(void) {
	/* Per-commit rows: --repeat 1, so no flake chart is produced. */
	struct editorMetricsRow rows[3];
	seed_test_run(&rows[0], "2026-05-24", 12.5, 0, 0, 0, 1);
	seed_test_run(&rows[1], "2026-05-25", 13.1, 0, 2, 0, 1);
	seed_test_run(&rows[2], "2026-05-26", 12.9, 1, 0, 0, 1);

	struct editorMetricsCmdOptions opts;
	editorMetricsCmdOptionsInit(&opts);
	char dir[64];
	make_tmpdir(dir, sizeof(dir));

	int n = editorMetricsCmdRenderSvg(rows, 3, &opts, 0, dir);
	ASSERT_EQ_INT(2, n);

	char p[256];
	(void)snprintf(p, sizeof(p), "%s/test-wall-seconds.svg", dir);
	ASSERT_TRUE(file_exists(p));
	(void)snprintf(p, sizeof(p), "%s/test-stability.svg", dir);
	ASSERT_TRUE(file_exists(p));

	char body[4096];
	ASSERT_TRUE(read_file_into(p, body, sizeof(body)));
	/* Stability chart carries crashes + failed (per-commit signal). Flakes
	 * moved to their own chart sourced from --repeat>1 rows. */
	ASSERT_TRUE(strstr(body, ">crashes</text>") != NULL);
	ASSERT_TRUE(strstr(body, ">failed</text>") != NULL);
	ASSERT_TRUE(strstr(body, ">flakes</text>") == NULL);

	/* No repeat>1 rows here → no flake chart. */
	(void)snprintf(p, sizeof(p), "%s/test-flakes.svg", dir);
	ASSERT_TRUE(!file_exists(p));

	(void)rmrf_dir(dir);
	return 0;
}

static int test_cmd_render_flakes_chart_from_repeat_rows(void) {
	/* Only --repeat>1 rows feed the flakiness chart. The lone repeat==1 row
	 * is ignored there but still counts toward stability. */
	struct editorMetricsRow rows[3];
	seed_test_run(&rows[0], "2026-05-24", 12.5, 0, 0, 1, 20);
	seed_test_run(&rows[1], "2026-05-25", 13.1, 0, 0, 0, 20);
	seed_test_run(&rows[2], "2026-05-26", 12.9, 0, 0, 0, 1);

	struct editorMetricsCmdOptions opts;
	editorMetricsCmdOptionsInit(&opts);
	char dir[64];
	make_tmpdir(dir, sizeof(dir));

	int n = editorMetricsCmdRenderSvg(rows, 3, &opts, 0, dir);
	ASSERT_EQ_INT(3, n);

	char p[256];
	(void)snprintf(p, sizeof(p), "%s/test-flakes.svg", dir);
	ASSERT_TRUE(file_exists(p));

	/* Single-series chart: the legend (and its series label) is suppressed,
	 * so identify the chart by its title text instead. */
	char body[4096];
	ASSERT_TRUE(read_file_into(p, body, sizeof(body)));
	ASSERT_TRUE(strstr(body, "Test flakiness") != NULL);

	(void)rmrf_dir(dir);
	return 0;
}

static int test_cmd_render_bench_chart_has_min_series(void) {
	struct editorMetricsRow rows[3];
	seed_bench(&rows[0], "2026-05-24", "b", 100, 200);
	seed_bench(&rows[1], "2026-05-25", "b", 110, 210);
	seed_bench(&rows[2], "2026-05-26", "b", 120, 220);
	rows[0].min_ns = 80;
	rows[1].min_ns = 85;
	rows[2].min_ns = 90;

	struct editorMetricsCmdOptions opts;
	editorMetricsCmdOptionsInit(&opts);
	char dir[64];
	make_tmpdir(dir, sizeof(dir));

	int n = editorMetricsCmdRenderSvg(rows, 3, &opts, 0, dir);
	ASSERT_EQ_INT(1, n);

	char p[256];
	(void)snprintf(p, sizeof(p), "%s/bench-b.svg", dir);
	char body[8192];
	ASSERT_TRUE(read_file_into(p, body, sizeof(body)));
	ASSERT_TRUE(strstr(body, ">p50</text>") != NULL);
	ASSERT_TRUE(strstr(body, ">p95</text>") != NULL);
	ASSERT_TRUE(strstr(body, ">min</text>") != NULL);
	/* All three legend colors must appear as polyline strokes. */
	ASSERT_TRUE(strstr(body, "stroke=\"#1f77b4\"") != NULL);
	ASSERT_TRUE(strstr(body, "stroke=\"#ff7f0e\"") != NULL);
	ASSERT_TRUE(strstr(body, "stroke=\"#2ca02c\"") != NULL);

	(void)rmrf_dir(dir);
	return 0;
}

static int test_cmd_render_fuzz_throughput_zero_runtime_safe(void) {
	struct editorMetricsRow rows[2];
	seed_fuzz(&rows[0], "2026-05-25", "lsp", 50, 1000);
	seed_fuzz(&rows[1], "2026-05-26", "lsp", 60, 1100);
	rows[0].executed_units = 1000;
	rows[0].runtime_seconds = 10; /* 100 exec/s */
	rows[1].executed_units = 5000;
	rows[1].runtime_seconds = 0; /* must not divide by zero */

	struct editorMetricsCmdOptions opts;
	editorMetricsCmdOptionsInit(&opts);
	char dir[64];
	make_tmpdir(dir, sizeof(dir));

	int n = editorMetricsCmdRenderSvg(rows, 2, &opts, 0, dir);
	ASSERT_EQ_INT(3, n);

	char p[256];
	(void)snprintf(p, sizeof(p), "%s/fuzz-lsp-throughput.svg", dir);
	ASSERT_TRUE(file_exists(p));

	(void)rmrf_dir(dir);
	return 0;
}

static int test_cmd_render_kind_filter(void) {
	struct editorMetricsRow rows[4];
	seed_bench(&rows[0], "2026-05-25", "b", 100, 200);
	seed_bench(&rows[1], "2026-05-26", "b", 110, 210);
	seed_fuzz(&rows[2], "2026-05-25", "lsp", 50, 1000);
	seed_fuzz(&rows[3], "2026-05-26", "lsp", 60, 1100);

	struct editorMetricsCmdOptions opts;
	editorMetricsCmdOptionsInit(&opts);
	opts.kind_filter = EDITOR_METRICS_KIND_BENCH;
	char dir[64];
	make_tmpdir(dir, sizeof(dir));

	int n = editorMetricsCmdRenderSvg(rows, 4, &opts, 0, dir);
	ASSERT_EQ_INT(1, n);
	char p[256];
	(void)snprintf(p, sizeof(p), "%s/bench-b.svg", dir);
	ASSERT_TRUE(file_exists(p));
	(void)snprintf(p, sizeof(p), "%s/fuzz-lsp-cov.svg", dir);
	ASSERT_TRUE(!file_exists(p));
	(void)rmrf_dir(dir);
	return 0;
}

const struct editorTestCase g_metrics_render_svg_tests[] = {
        {"metrics_svg_chart_emits_header_and_polyline", test_chart_emits_svg_header_and_polyline},
        {"metrics_svg_chart_xml_escapes_title", test_chart_xml_escapes_title},
        {"metrics_svg_chart_thins_ticks_for_many_points", test_chart_thins_ticks_for_many_points},
        {"metrics_svg_chart_skips_when_fewer_than_two_points",
         test_chart_skips_when_fewer_than_two_points},
        {"metrics_svg_cmd_writes_files_and_manifest", test_cmd_render_writes_files_and_manifest},
        {"metrics_svg_cmd_sanitizes_filenames", test_cmd_render_sanitizes_filenames},
        {"metrics_svg_cmd_skips_single_point_series", test_cmd_render_skips_single_point_series},
        {"metrics_svg_cmd_kind_filter", test_cmd_render_kind_filter},
        {"metrics_svg_cmd_emits_test_run_charts", test_cmd_render_emits_test_run_charts},
        {"metrics_svg_cmd_flakes_chart_from_repeat_rows",
         test_cmd_render_flakes_chart_from_repeat_rows},
        {"metrics_svg_cmd_bench_chart_has_min_series", test_cmd_render_bench_chart_has_min_series},
        {"metrics_svg_cmd_fuzz_throughput_zero_runtime_safe",
         test_cmd_render_fuzz_throughput_zero_runtime_safe},
};

const int g_metrics_render_svg_test_count =
        (int)(sizeof(g_metrics_render_svg_tests) / sizeof(g_metrics_render_svg_tests[0]));
