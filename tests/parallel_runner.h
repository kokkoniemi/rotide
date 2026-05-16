#ifndef TESTS_PARALLEL_RUNNER_H
#define TESTS_PARALLEL_RUNNER_H

#include "runner_support.h"

#include <stddef.h>

/*
 * One suite worth of tests selected for execution. The parent fills
 * suite_idx, test_indices, and count before scheduling. The child fills
 * the output fields and exits; the parent reads them back via the
 * artifact-log file written under tests/artifacts/.
 */
struct suiteBatch {
	int suite_idx;
	int *test_indices;
	int count;

	int total_runs;
	int passed_runs;
	int failed_unique;
	int reset_violations;
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
	int crashes;
	int aborted_for_fail_fast;
};

/*
 * Runs the supplied batches across a worker pool of opts->jobs children,
 * one suite per child. Each child's stdout/stderr lands in
 * tests/artifacts/logs/<suite>.log. Crashes drop a stack dump in
 * tests/artifacts/crashes/<suite>/<test>.crash and surface as
 * batches[i].crashed = 1.
 *
 * After return, batches[i].output is owned by the caller and must be freed
 * with free(). The function emits no output itself; the caller iterates
 * batches in its preferred order and prints batch.output.
 *
 * Returns 0 on a clean run (every selected test PASS), 1 on any FAIL or
 * crash, or a negative errno-style code on infrastructure failure
 * (failed to fork, failed to open artifacts dir, etc.).
 */
int parallelRunBatches(
	const struct testRunnerOptions *opts,
	const struct editorTestSuite *suites,
	struct suiteBatch *batches,
	int batch_count,
	struct parallelRunResult *result_out);

/*
 * In-child entry point. Runs every test in batch->test_indices against
 * the named suite, with the same per-test reset_editor_state +
 * validate-reset semantics the sequential runner uses. Writes outcome
 * lines to stdout; assertion noise lands on stderr.
 *
 * Installs signal handlers for SIGSEGV/SIGABRT/SIGBUS/SIGFPE/SIGILL that
 * write a crash artifact and re-raise the signal so the parent sees
 * WIFSIGNALED. Async-signal-safety follows the standard caveat: writes
 * via write(2) and pre-formatted buffers; backtrace_symbols_fd is best
 * effort.
 *
 * Returns the exit code the child should pass to _exit().
 */
int parallelChildRunBatch(
	const struct testRunnerOptions *opts,
	const struct editorTestSuite *suite,
	struct suiteBatch *batch);

void parallelEnsureArtifactDirs(const char *root);

#endif
