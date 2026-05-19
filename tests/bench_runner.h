#ifndef TESTS_BENCH_RUNNER_H
#define TESTS_BENCH_RUNNER_H

#include <stddef.h>

/* Microbench harness. Each case provides setup/op/teardown. The harness
 * runs `iterations` samples (default 20). Per sample: setup → time
 * op(state, inner_ops) → teardown. Reported metrics are per-inner-op
 * nanoseconds: min, p50, p95.
 */

struct editorBenchCase {
	const char *name;
	int (*setup)(void **state_out);  /* return 1 on success, 0 on failure */
	void (*op)(void *state, int n);  /* run op n times against state */
	void (*teardown)(void *state);
	int inner_ops;                   /* batch size per sample; 0 → 1 */
};

struct editorBenchOptions {
	int iterations;          /* samples per case; 0 → 20 */
	const char *filter;      /* optional substring filter on name; NULL → all */
	const char *json_path;   /* optional JSON output path; NULL → text only */
	const char *metrics_path; /* optional JSONL output path (append); NULL → off */
};

int editorBenchRun(const struct editorBenchCase *cases, int count,
		const struct editorBenchOptions *options);

#endif
