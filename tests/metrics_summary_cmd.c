#define _DEFAULT_SOURCE

#include "metrics_summary_cmd.h"

#include <stdlib.h>
#include <string.h>

#define DEFAULT_SUMMARY_LIMIT 5
#define DEFAULT_WINDOW_HOURS 48
#define DEFAULT_REGRESSION_FACTOR 3.0

void editorMetricsCmdOptionsInit(struct editorMetricsCmdOptions *opts) {
	if (opts == NULL) {
		return;
	}
	memset(opts, 0, sizeof(*opts));
}

static int rowPassesFilters(const struct editorMetricsRow *r,
		const struct editorMetricsCmdOptions *opts) {
	if (opts->kind_filter != EDITOR_METRICS_KIND_UNKNOWN
			&& r->kind != opts->kind_filter) {
		return 0;
	}
	if (opts->since_unix > 0 && r->ts_unix < opts->since_unix) {
		return 0;
	}
	if (r->kind == EDITOR_METRICS_KIND_FUZZ
			&& opts->target_filter != NULL && opts->target_filter[0] != '\0'
			&& strcmp(r->fuzz_target, opts->target_filter) != 0) {
		return 0;
	}
	if (r->kind == EDITOR_METRICS_KIND_BENCH
			&& opts->bench_name_filter != NULL && opts->bench_name_filter[0] != '\0'
			&& strcmp(r->bench_name, opts->bench_name_filter) != 0) {
		return 0;
	}
	return 1;
}

static int compareByTs(const void *a, const void *b) {
	const struct editorMetricsRow *ra = *(const struct editorMetricsRow *const *)a;
	const struct editorMetricsRow *rb = *(const struct editorMetricsRow *const *)b;
	if (ra->ts_unix < rb->ts_unix) return -1;
	if (ra->ts_unix > rb->ts_unix) return 1;
	return 0;
}

/* Build a sorted (oldest → newest) pointer array of rows that match
 * the filters AND belong to one logical group. The group is described
 * by `kind` plus an optional `group_key` (target for fuzz, name for
 * bench, NULL for test_run). Caller owns the returned array via free(). */
static struct editorMetricsRow **collectGroup(
		const struct editorMetricsRow *rows, int count,
		enum editorMetricsKind kind, const char *group_key,
		const struct editorMetricsCmdOptions *opts, int *out_count) {
	*out_count = 0;
	struct editorMetricsRow **out = (struct editorMetricsRow **)malloc(
		(size_t)(count > 0 ? count : 1) * sizeof(*out));
	if (out == NULL) {
		return NULL;
	}
	int n = 0;
	for (int i = 0; i < count; i++) {
		const struct editorMetricsRow *r = &rows[i];
		if (r->kind != kind) {
			continue;
		}
		if (!rowPassesFilters(r, opts)) {
			continue;
		}
		if (group_key != NULL) {
			const char *rk = kind == EDITOR_METRICS_KIND_FUZZ
				? r->fuzz_target : r->bench_name;
			if (strcmp(rk, group_key) != 0) {
				continue;
			}
		}
		out[n++] = (struct editorMetricsRow *)r;
	}
	qsort(out, (size_t)n, sizeof(*out), compareByTs);
	*out_count = n;
	return out;
}

/* Distinct values of `field` (target or name) across rows of `kind` that
 * pass filters, in first-seen order. Caller owns the returned char**
 * array; each string is a pointer into the row data so do NOT free the
 * inner pointers, only the outer array. */
static const char **collectGroupKeys(
		const struct editorMetricsRow *rows, int count,
		enum editorMetricsKind kind,
		const struct editorMetricsCmdOptions *opts, int *out_count) {
	*out_count = 0;
	const char **keys = (const char **)malloc(
		(size_t)(count > 0 ? count : 1) * sizeof(*keys));
	if (keys == NULL) {
		return NULL;
	}
	int n = 0;
	for (int i = 0; i < count; i++) {
		const struct editorMetricsRow *r = &rows[i];
		if (r->kind != kind || !rowPassesFilters(r, opts)) {
			continue;
		}
		const char *k = kind == EDITOR_METRICS_KIND_FUZZ
			? r->fuzz_target : r->bench_name;
		int seen = 0;
		for (int j = 0; j < n; j++) {
			if (strcmp(keys[j], k) == 0) {
				seen = 1;
				break;
			}
		}
		if (!seen) {
			keys[n++] = k;
		}
	}
	*out_count = n;
	return keys;
}

/* ---- summary ---- */

static void printTestRunRow(const struct editorMetricsRow *r, FILE *out) {
	fprintf(out, "  %s  wall=%.3fs  runs=%lld/%lld  fail=%lld  crash=%lld  exit=%lld",
		r->ts, r->wall_seconds, r->passed_runs, r->total_runs,
		r->failed_unique, r->crashes, r->exit_code);
	if (r->git_sha[0] != '\0') {
		fprintf(out, "  sha=%s", r->git_sha);
	}
	fprintf(out, "\n");
}

static void printBenchRow(const struct editorMetricsRow *r, FILE *out) {
	fprintf(out,
		"  %s  p50=%.0fns  p95=%.0fns  iqr=%.0fns  n=%lld inner=%lld\n",
		r->ts, r->p50_ns, r->p95_ns, r->iqr_ns, r->samples, r->inner_ops);
}

static void printFuzzRow(const struct editorMetricsRow *r, FILE *out) {
	fprintf(out,
		"  %s  cov=%lld  ft=%lld  corp=%lld/%lldb  on_disk=%lld/%lldb  "
		"exec=%lld  runtime=%llds\n",
		r->ts, r->cov_edges, r->ft_features, r->corp_count, r->corp_bytes,
		r->corpus_files, r->corpus_bytes, r->executed_units, r->runtime_seconds);
}

static void summarizeGroup(const struct editorMetricsRow *rows, int count,
		enum editorMetricsKind kind, const char *group_key,
		const struct editorMetricsCmdOptions *opts, FILE *out) {
	int group_count = 0;
	struct editorMetricsRow **group = collectGroup(rows, count, kind, group_key,
		opts, &group_count);
	if (group == NULL || group_count == 0) {
		free(group);
		return;
	}
	int limit = opts->summary_limit > 0 ? opts->summary_limit : DEFAULT_SUMMARY_LIMIT;
	int start = group_count > limit ? group_count - limit : 0;
	int shown = group_count - start;

	if (group_key != NULL) {
		fprintf(out, "== %s: %s (%d row%s shown",
			editorMetricsKindName(kind), group_key,
			shown, shown == 1 ? "" : "s");
	} else {
		fprintf(out, "== %s (%d row%s shown",
			editorMetricsKindName(kind),
			shown, shown == 1 ? "" : "s");
	}
	if (start > 0) {
		fprintf(out, ", %d earlier omitted", start);
	}
	fprintf(out, ") ==\n");

	for (int i = start; i < group_count; i++) {
		const struct editorMetricsRow *r = group[i];
		switch (kind) {
		case EDITOR_METRICS_KIND_TEST_RUN: printTestRunRow(r, out); break;
		case EDITOR_METRICS_KIND_BENCH:    printBenchRow(r, out); break;
		case EDITOR_METRICS_KIND_FUZZ:     printFuzzRow(r, out); break;
		default: break;
		}
	}
	free(group);
}

int editorMetricsCmdSummary(const struct editorMetricsRow *rows, int count,
		const struct editorMetricsCmdOptions *opts, FILE *out) {
	if (rows == NULL || opts == NULL || out == NULL) {
		return 0;
	}

	if (opts->kind_filter == EDITOR_METRICS_KIND_UNKNOWN
			|| opts->kind_filter == EDITOR_METRICS_KIND_TEST_RUN) {
		summarizeGroup(rows, count, EDITOR_METRICS_KIND_TEST_RUN, NULL, opts, out);
	}
	if (opts->kind_filter == EDITOR_METRICS_KIND_UNKNOWN
			|| opts->kind_filter == EDITOR_METRICS_KIND_BENCH) {
		int n = 0;
		const char **keys = collectGroupKeys(rows, count,
			EDITOR_METRICS_KIND_BENCH, opts, &n);
		for (int i = 0; i < n; i++) {
			summarizeGroup(rows, count, EDITOR_METRICS_KIND_BENCH, keys[i], opts, out);
		}
		free(keys);
	}
	if (opts->kind_filter == EDITOR_METRICS_KIND_UNKNOWN
			|| opts->kind_filter == EDITOR_METRICS_KIND_FUZZ) {
		int n = 0;
		const char **keys = collectGroupKeys(rows, count,
			EDITOR_METRICS_KIND_FUZZ, opts, &n);
		for (int i = 0; i < n; i++) {
			summarizeGroup(rows, count, EDITOR_METRICS_KIND_FUZZ, keys[i], opts, out);
		}
		free(keys);
	}
	return 0;
}

/* ---- check-fuzz-stale ---- */

int editorMetricsCmdCheckFuzzStale(const struct editorMetricsRow *rows, int count,
		const struct editorMetricsCmdOptions *opts, FILE *out) {
	if (rows == NULL || opts == NULL || out == NULL) {
		return -1;
	}
	long long window_hours = opts->window_hours > 0
		? opts->window_hours : DEFAULT_WINDOW_HOURS;
	long long window_seconds = window_hours * 3600;

	struct editorMetricsCmdOptions effective = *opts;
	effective.kind_filter = EDITOR_METRICS_KIND_FUZZ;

	int n_keys = 0;
	const char **keys = collectGroupKeys(rows, count,
		EDITOR_METRICS_KIND_FUZZ, &effective, &n_keys);
	if (keys == NULL || n_keys == 0) {
		fprintf(out, "fuzz-stale: no fuzz rows match the given filters\n");
		free(keys);
		return 0;
	}

	int any_stale = 0;
	for (int i = 0; i < n_keys; i++) {
		int gc = 0;
		struct editorMetricsRow **group = collectGroup(rows, count,
			EDITOR_METRICS_KIND_FUZZ, keys[i], &effective, &gc);
		if (group == NULL || gc == 0) {
			free(group);
			continue;
		}
		const struct editorMetricsRow *latest = group[gc - 1];
		long long cutoff = latest->ts_unix - window_seconds;
		const struct editorMetricsRow *baseline = NULL;
		for (int j = 0; j < gc; j++) {
			if (group[j]->ts_unix >= cutoff) {
				baseline = group[j];
				break;
			}
		}
		if (baseline == NULL || baseline == latest) {
			fprintf(out,
				"fuzz-stale: target=%s window=%lldh "
				"insufficient data (only 1 row in window) — skipping\n",
				keys[i], window_hours);
			free(group);
			continue;
		}
		long long delta = latest->cov_edges - baseline->cov_edges;
		if (delta <= 0) {
			fprintf(out,
				"fuzz-stale: target=%s window=%lldh "
				"cov_edges unchanged at %lld across %d run(s) in window — STALE\n",
				keys[i], window_hours, latest->cov_edges, gc);
			any_stale = 1;
		} else {
			fprintf(out,
				"fuzz-stale: target=%s window=%lldh "
				"cov_edges grew %lld -> %lld (+%lld) across %d run(s) — ok\n",
				keys[i], window_hours, baseline->cov_edges,
				latest->cov_edges, delta, gc);
		}
		free(group);
	}
	free(keys);
	return any_stale ? 1 : 0;
}

/* ---- check-bench-regression ---- */

int editorMetricsCmdCheckBenchRegression(const struct editorMetricsRow *rows, int count,
		const struct editorMetricsCmdOptions *opts, FILE *out) {
	if (rows == NULL || opts == NULL || out == NULL) {
		return -1;
	}
	double factor = opts->regression_factor > 0
		? opts->regression_factor : DEFAULT_REGRESSION_FACTOR;

	struct editorMetricsCmdOptions effective = *opts;
	effective.kind_filter = EDITOR_METRICS_KIND_BENCH;

	int n_keys = 0;
	const char **keys = collectGroupKeys(rows, count,
		EDITOR_METRICS_KIND_BENCH, &effective, &n_keys);
	if (keys == NULL || n_keys == 0) {
		fprintf(out, "bench-regression: no bench rows match the given filters\n");
		free(keys);
		return 0;
	}

	int any_regressed = 0;
	for (int i = 0; i < n_keys; i++) {
		int gc = 0;
		struct editorMetricsRow **group = collectGroup(rows, count,
			EDITOR_METRICS_KIND_BENCH, keys[i], &effective, &gc);
		if (group == NULL || gc < 2) {
			fprintf(out,
				"bench-regression: name=%s only %d row — need >= 2 to compare\n",
				keys[i], gc);
			free(group);
			continue;
		}
		const struct editorMetricsRow *prev = group[gc - 2];
		const struct editorMetricsRow *latest = group[gc - 1];
		double delta = latest->p50_ns - prev->p50_ns;
		double threshold = factor * prev->iqr_ns;
		if (delta > threshold) {
			fprintf(out,
				"bench-regression: name=%s prev_p50=%.0fns latest_p50=%.0fns "
				"delta=%.0fns > %.1f*prev_iqr (%.0fns) — REGRESSION\n",
				keys[i], prev->p50_ns, latest->p50_ns, delta, factor, threshold);
			any_regressed = 1;
		} else {
			fprintf(out,
				"bench-regression: name=%s prev_p50=%.0fns latest_p50=%.0fns "
				"delta=%.0fns within %.1f*prev_iqr (%.0fns) — ok\n",
				keys[i], prev->p50_ns, latest->p50_ns, delta, factor, threshold);
		}
		free(group);
	}
	free(keys);
	return any_regressed ? 1 : 0;
}
