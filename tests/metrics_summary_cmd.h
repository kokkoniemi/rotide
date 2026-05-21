#ifndef TESTS_METRICS_SUMMARY_CMD_H
#define TESTS_METRICS_SUMMARY_CMD_H

#include "metrics_jsonl_read.h"

#include <stdio.h>

/* Shared CLI options for the metrics_summary subcommands. Each command
 * uses only the subset relevant to it; unused fields are ignored. */
struct editorMetricsCmdOptions {
	enum editorMetricsKind kind_filter; /* UNKNOWN = no filter */
	const char *target_filter;          /* fuzz target; NULL = all */
	const char *bench_name_filter;      /* bench name; NULL = all */
	long long since_unix;               /* only rows with ts_unix >= this; 0 = no filter */

	int summary_limit;        /* rows per group in summary; 0 → default 5 */
	long long window_hours;   /* for check-fuzz-stale; 0 → default 48 */
	double regression_factor; /* for check-bench-regression; 0 → default 3.0 */
};

void editorMetricsCmdOptionsInit(struct editorMetricsCmdOptions *opts);

/* summary: print recent rows grouped by (kind, target/name). Always
 * succeeds; returns 0. */
int editorMetricsCmdSummary(const struct editorMetricsRow *rows, int count,
                            const struct editorMetricsCmdOptions *opts, FILE *out);

/* check-fuzz-stale: for each fuzz target (or just the one matching
 * --target), look at rows within the window and fail (return 1) if
 * cov_edges didn't grow between earliest and latest. Per-target progress
 * is logged to `out` regardless. Returns 0 if no stale target found, 1
 * if at least one target is stale, -1 on bad arguments. */
int editorMetricsCmdCheckFuzzStale(const struct editorMetricsRow *rows, int count,
                                   const struct editorMetricsCmdOptions *opts, FILE *out);

/* check-bench-regression: for each bench (or just the one matching
 * --bench-name), compare the latest row to the previous one and flag a
 * regression when `latest.p50_ns - prev.p50_ns > factor * prev.iqr_ns`.
 * Returns 0 if no regression, 1 if at least one bench regressed. */
int editorMetricsCmdCheckBenchRegression(const struct editorMetricsRow *rows, int count,
                                         const struct editorMetricsCmdOptions *opts, FILE *out);

#endif
