#ifndef TESTS_METRICS_JSONL_READ_H
#define TESTS_METRICS_JSONL_READ_H

#include <stddef.h>
#include <time.h>

/* In-memory view of one tests/metrics.jsonl row. Lossy — only known
 * fields are kept; unknown keys are ignored. Presence-of-field is
 * recorded so consumers can tell "field absent" from "field present and
 * zero". */

enum editorMetricsKind {
	EDITOR_METRICS_KIND_UNKNOWN = 0,
	EDITOR_METRICS_KIND_TEST_RUN,
	EDITOR_METRICS_KIND_BENCH,
	EDITOR_METRICS_KIND_FUZZ,
};

struct editorMetricsRow {
	enum editorMetricsKind kind;
	char ts[32];    /* ISO 8601 UTC, e.g. 2026-05-19T13:35:42Z. */
	time_t ts_unix; /* 0 if ts didn't parse. */

	/* env enrichment */
	char git_sha[64];
	char git_ref[128];
	char ci_run_id[64];

	/* test_run */
	double wall_seconds;
	long long total_runs;
	long long passed_runs;
	long long failed_unique;
	long long crashes;
	long long reset_violations;
	long long flakes;
	long long property_ops;
	double property_ops_seconds;
	long long repeat;
	long long exit_code;

	/* bench */
	char bench_name[128];
	long long samples;
	long long inner_ops;
	double min_ns;
	double p50_ns;
	double p95_ns;
	double iqr_ns;

	/* fuzz */
	char fuzz_target[64];
	long long cov_edges;
	long long ft_features;
	long long corp_count;
	long long corp_bytes;
	long long corpus_files;
	long long corpus_bytes;
	long long executed_units;
	long long new_units_added;
	long long runtime_seconds;
};

void editorMetricsRowInit(struct editorMetricsRow *row);

/* Parse a single JSONL line into `row`. Returns 1 on success (at least
 * `kind` and `ts` were recognised), 0 if the line is unparseable.
 * Trailing whitespace / newlines are tolerated. */
int editorMetricsRowParse(const char *line, struct editorMetricsRow *row);

/* Read a whole file into a heap-allocated array. On success the caller
 * owns *rows_out and must call editorMetricsRowsFree(). Returns 0 on
 * success, -1 on I/O failure. Lines that fail to parse are skipped
 * (counted via *skipped_out if non-NULL). */
int editorMetricsRowsLoad(const char *path, struct editorMetricsRow **rows_out, int *count_out,
                          int *skipped_out);

void editorMetricsRowsFree(struct editorMetricsRow *rows, int count);

/* Helpers exposed so the summary CLI can group/filter without
 * duplicating the kind dispatch. */
const char *editorMetricsKindName(enum editorMetricsKind k);

#endif
