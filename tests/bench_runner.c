#include "bench_runner.h"

#include "metrics_jsonl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BENCH_DEFAULT_ITERATIONS 20
#define BENCH_DEFAULT_INNER_OPS 1
#define BENCH_MAX_ITERATIONS 1024

static double monotonic_ns(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static int compare_double(const void *a, const void *b) {
	double da = *(const double *)a;
	double db = *(const double *)b;
	if (da < db) {
		return -1;
	}
	if (da > db) {
		return 1;
	}
	return 0;
}

/* Linear-interp percentile on sorted samples. */
static double percentile(const double *sorted, int n, double p) {
	if (n <= 0) {
		return 0.0;
	}
	if (n == 1) {
		return sorted[0];
	}
	double rank = p * (double)(n - 1);
	int lo = (int)rank;
	int hi = lo + 1 < n ? lo + 1 : lo;
	double frac = rank - (double)lo;
	return sorted[lo] + (sorted[hi] - sorted[lo]) * frac;
}

static int substring_match(const char *haystack, const char *needle) {
	if (needle == NULL || needle[0] == '\0') {
		return 1;
	}
	return strstr(haystack, needle) != NULL;
}

int editorBenchRun(const struct editorBenchCase *cases, int count,
                   const struct editorBenchOptions *options) {
	int iterations = options != NULL && options->iterations > 0 ? options->iterations
	                                                            : BENCH_DEFAULT_ITERATIONS;
	if (iterations > BENCH_MAX_ITERATIONS) {
		iterations = BENCH_MAX_ITERATIONS;
	}
	const char *filter = options != NULL ? options->filter : NULL;
	const char *json_path = options != NULL ? options->json_path : NULL;
	const char *metrics_path = options != NULL ? options->metrics_path : NULL;

	double *samples = (double *)malloc(sizeof(double) * (size_t)iterations);
	if (samples == NULL) {
		(void)fprintf(stderr, "bench: malloc samples failed\n");
		return 1;
	}

	FILE *json = NULL;
	if (json_path != NULL) {
		json = fopen(json_path, "w");
		if (json == NULL) {
			(void)fprintf(stderr, "bench: open %s failed\n", json_path);
			free(samples);
			return 1;
		}
		(void)fprintf(json, "{\n  \"iterations\": %d,\n  \"benches\": [\n", iterations);
	}

	int ran = 0;
	int failures = 0;
	for (int i = 0; i < count; i++) {
		const struct editorBenchCase *c = &cases[i];
		if (!substring_match(c->name, filter)) {
			continue;
		}
		int inner_ops = c->inner_ops > 0 ? c->inner_ops : BENCH_DEFAULT_INNER_OPS;

		int sample_count = 0;
		int setup_failed = 0;
		for (int it = 0; it < iterations; it++) {
			void *state = NULL;
			if (!c->setup(&state)) {
				(void)fprintf(stderr, "bench: %s setup failed on iter %d\n", c->name, it);
				setup_failed = 1;
				break;
			}
			double t0 = monotonic_ns();
			c->op(state, inner_ops);
			double t1 = monotonic_ns();
			c->teardown(state);

			double per_op = (t1 - t0) / (double)inner_ops;
			samples[sample_count++] = per_op;
		}
		if (setup_failed) {
			failures++;
			continue;
		}

		qsort(samples, (size_t)sample_count, sizeof(double), compare_double);
		double min = samples[0];
		double p50 = percentile(samples, sample_count, 0.50);
		double p95 = percentile(samples, sample_count, 0.95);
		double p25 = percentile(samples, sample_count, 0.25);
		double p75 = percentile(samples, sample_count, 0.75);
		double iqr = p75 - p25;

		printf("  %-44s min=%9.1f ns  p50=%9.1f ns  p95=%9.1f ns  "
		       "iqr=%9.1f ns  n=%d inner=%d\n",
		       c->name, min, p50, p95, iqr, sample_count, inner_ops);

		if (json != NULL) {
			(void)fprintf(json,
			        "%s    {\n"
			        "      \"name\": \"%s\",\n"
			        "      \"samples\": %d,\n"
			        "      \"inner_ops\": %d,\n"
			        "      \"min_ns\": %.3f,\n"
			        "      \"p50_ns\": %.3f,\n"
			        "      \"p95_ns\": %.3f,\n"
			        "      \"iqr_ns\": %.3f\n"
			        "    }",
			        ran > 0 ? ",\n" : "", c->name, sample_count, inner_ops, min, p50,
			        p95, iqr);
		}
		if (metrics_path != NULL && metrics_path[0] != '\0') {
			struct editorMetricsField fields[] = {
			        {"name", EDITOR_METRICS_STR, .v.s = c->name},
			        {"samples", EDITOR_METRICS_INT, .v.i = sample_count},
			        {"inner_ops", EDITOR_METRICS_INT, .v.i = inner_ops},
			        {"min_ns", EDITOR_METRICS_DOUBLE, .v.d = min},
			        {"p50_ns", EDITOR_METRICS_DOUBLE, .v.d = p50},
			        {"p95_ns", EDITOR_METRICS_DOUBLE, .v.d = p95},
			        {"iqr_ns", EDITOR_METRICS_DOUBLE, .v.d = iqr},
			};
			if (editorMetricsAppend(metrics_path, "bench", fields,
			                        (int)(sizeof(fields) / sizeof(fields[0]))) != 0) {
				(void)fprintf(stderr, "bench: warning: failed to append metrics to %s\n",
				        metrics_path);
			}
		}
		ran++;
	}

	if (json != NULL) {
		(void)fprintf(json, "\n  ]\n}\n");
		(void)fclose(json);
	}

	free(samples);

	if (ran == 0 && filter != NULL && filter[0] != '\0') {
		(void)fprintf(stderr, "bench: no cases matched filter \"%s\"\n", filter);
		return 1;
	}
	return failures == 0 ? 0 : 1;
}
