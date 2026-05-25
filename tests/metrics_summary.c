#define _DEFAULT_SOURCE

/* Thin CLI over metrics_summary_cmd.c. Subcommands:
 *   summary                    print recent rows grouped by kind/target/name
 *   check-fuzz-stale           exit 1 if any fuzz target gained zero edges in window
 *   check-bench-regression     exit 1 if any bench p50 moved > factor * prev_iqr
 *
 * Common options:
 *   --in PATH              input JSONL (default: tests/metrics.jsonl)
 *   --kind KIND            filter to one kind (test_run|bench|fuzz)
 *   --target NAME          filter fuzz rows
 *   --bench-name NAME      filter bench rows
 *   --since-hours N        only consider rows newer than N hours ago
 *
 * Subcommand-specific options:
 *   --limit N              summary rows per group (default 5)
 *   --window-hours N       check-fuzz-stale window (default 48)
 *   --factor F             check-bench-regression threshold factor (default 3.0)
 */

#include "metrics_jsonl_read.h"
#include "metrics_summary_cmd.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void usage(FILE *out) {
	(void)fprintf(out, "usage: metrics_summary <subcommand> [opts]\n"
	             "  Subcommands: summary | check-fuzz-stale | check-bench-regression\n"
	             "  Common: --in PATH --kind KIND --target NAME --bench-name NAME\n"
	             "          --since-hours N\n"
	             "  summary:                --limit N\n"
	             "  check-fuzz-stale:       --window-hours N\n"
	             "  check-bench-regression: --factor F\n");
}

static int parse_kind(const char *s, enum editorMetricsKind *out) {
	if (strcmp(s, "test_run") == 0) {
		*out = EDITOR_METRICS_KIND_TEST_RUN;
		return 1;
	}
	if (strcmp(s, "bench") == 0) {
		*out = EDITOR_METRICS_KIND_BENCH;
		return 1;
	}
	if (strcmp(s, "fuzz") == 0) {
		*out = EDITOR_METRICS_KIND_FUZZ;
		return 1;
	}
	return 0;
}

int main(int argc, char **argv) {
	if (argc < 2) {
		usage(stderr);
		return 2;
	}
	const char *sub = argv[1];
	if (strcmp(sub, "-h") == 0 || strcmp(sub, "--help") == 0) {
		usage(stdout);
		return 0;
	}

	const char *in_path = "tests/metrics.jsonl";
	struct editorMetricsCmdOptions opts;
	editorMetricsCmdOptionsInit(&opts);
	long long since_hours = 0;

	for (int i = 2; i < argc; i++) {
		const char *a = argv[i];
		const char *next = (i + 1 < argc) ? argv[i + 1] : NULL;
		if (strcmp(a, "--in") == 0 && next) {
			in_path = next;
			i++;
			continue;
		}
		if (strcmp(a, "--kind") == 0 && next) {
			if (!parse_kind(next, &opts.kind_filter)) {
				(void)fprintf(stderr, "metrics_summary: bad --kind: %s\n", next);
				return 2;
			}
			i++;
			continue;
		}
		if (strcmp(a, "--target") == 0 && next) {
			opts.target_filter = next;
			i++;
			continue;
		}
		if (strcmp(a, "--bench-name") == 0 && next) {
			opts.bench_name_filter = next;
			i++;
			continue;
		}
		if (strcmp(a, "--since-hours") == 0 && next) {
			char *end = NULL;
			since_hours = strtoll(next, &end, 10);
			if (end == NULL || *end != '\0' || since_hours < 0) {
				(void)fprintf(stderr, "metrics_summary: bad --since-hours\n");
				return 2;
			}
			i++;
			continue;
		}
		if (strcmp(a, "--limit") == 0 && next) {
			opts.summary_limit = (int)strtol(next, NULL, 10);
			i++;
			continue;
		}
		if (strcmp(a, "--window-hours") == 0 && next) {
			char *end = NULL;
			opts.window_hours = strtoll(next, &end, 10);
			if (end == NULL || *end != '\0' || opts.window_hours <= 0) {
				(void)fprintf(stderr, "metrics_summary: bad --window-hours\n");
				return 2;
			}
			i++;
			continue;
		}
		if (strcmp(a, "--factor") == 0 && next) {
			char *end = NULL;
			opts.regression_factor = strtod(next, &end);
			if (end == NULL || *end != '\0' || opts.regression_factor <= 0.0) {
				(void)fprintf(stderr, "metrics_summary: bad --factor\n");
				return 2;
			}
			i++;
			continue;
		}
		(void)fprintf(stderr, "metrics_summary: unknown arg: %s\n", a);
		usage(stderr);
		return 2;
	}

	if (since_hours > 0) {
		opts.since_unix = (long long)time(NULL) - since_hours * 3600;
	}

	struct editorMetricsRow *rows = NULL;
	int count = 0;
	int skipped = 0;
	if (editorMetricsRowsLoad(in_path, &rows, &count, &skipped) != 0) {
		(void)fprintf(stderr, "metrics_summary: cannot read %s: %s\n", in_path, strerror(errno));
		return 2;
	}
	if (skipped > 0) {
		(void)fprintf(stderr, "metrics_summary: %d malformed line(s) skipped\n", skipped);
	}

	int rc = 0;
	if (strcmp(sub, "summary") == 0) {
		rc = editorMetricsCmdSummary(rows, count, &opts, stdout);
	} else if (strcmp(sub, "check-fuzz-stale") == 0) {
		rc = editorMetricsCmdCheckFuzzStale(rows, count, &opts, stdout);
	} else if (strcmp(sub, "check-bench-regression") == 0) {
		rc = editorMetricsCmdCheckBenchRegression(rows, count, &opts, stdout);
	} else {
		(void)fprintf(stderr, "metrics_summary: unknown subcommand: %s\n", sub);
		usage(stderr);
		editorMetricsRowsFree(rows, count);
		return 2;
	}

	editorMetricsRowsFree(rows, count);
	return rc;
}
