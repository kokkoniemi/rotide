#ifndef TESTS_PARALLEL_RUNNER_H
#define TESTS_PARALLEL_RUNNER_H

#include "runner_support.h"

#include <stddef.h>

struct suiteBatch {
	int suite_idx;
	int *test_indices;
	int count;

	int total_runs;
	int passed_runs;
	int failed_unique;
	int reset_violations;
	int flakes;
	long long property_ops;
	double property_ops_seconds;
	double exec_seconds_total;
	int skipped_remaining;

	int crashed;
	int crash_signal;
	char crash_test_name[256];
	char crash_artifact_path[1024];

	char *output;
	size_t output_len;

	char log_path[1024];
	char marker_path[1024];
};

struct parallelRunResult {
	int total_runs;
	int passed_runs;
	int failed_unique;
	int reset_violations;
	int flakes;
	long long property_ops;
	double property_ops_seconds;
	double exec_seconds_total;
	int crashes;
	int aborted_for_fail_fast;
};

/* batches[i].output is malloc'd; caller frees. */
int parallelRunBatches(const struct testRunnerOptions *opts, const struct editorTestSuite *suites,
                       struct suiteBatch *batches, int batch_count,
                       struct parallelRunResult *result_out);

int parallelChildRunBatch(const struct testRunnerOptions *opts, const struct editorTestSuite *suite,
                          struct suiteBatch *batch);

void parallelEnsureArtifactDirs(const char *root);

#endif
