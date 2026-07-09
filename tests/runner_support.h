#ifndef TESTS_RUNNER_SUPPORT_H
#define TESTS_RUNNER_SUPPORT_H

#include "test_case.h"

#include <stddef.h>

struct editorTestSuite {
	const char *name;
	const char *tags;
	const struct editorTestCase *tests;
	const int *count;
};

struct snapshotExcludeRange {
	size_t offset;
	size_t size;
};

/* excludes must be sorted by ascending offset and non-overlapping. */
int runnerSnapshotCompare(const unsigned char *a, const unsigned char *b, size_t size,
                          const struct snapshotExcludeRange *excludes, int exclude_count,
                          size_t *first_diff_out);

struct testRunnerOptions {
	const char *filter;
	const char *include_tag;
	const char *exclude_tag;
	const char *metrics_out;
	const char *update_golden_stash;
	int list_only;
	int help_requested;
	int fail_fast;
	int repeat;
	int shuffle;
	int validate_reset;
	int parse_error;
	int seed_specified;
	int jobs;
	unsigned long long seed;
	const char *error_msg;
};

void runnerOptionsInit(struct testRunnerOptions *opts);
int runnerOptionsParse(struct testRunnerOptions *opts, int argc, char **argv);

int runnerNameMatches(const char *name, const char *filter);
int runnerTagsHave(const char *tags, const char *tag);

unsigned long long runnerRngNext(unsigned long long *state);
unsigned long long runnerSeedFromOsEntropy(void);
void runnerShuffleIndices(int *indices, int count, unsigned long long seed);

/* Deterministic per-repeat seed derived from the single recorded base seed.
 * rep==0 returns base_seed unchanged so a --repeat 1 run is byte-identical to
 * passing --seed directly; rep>0 advances runnerRngNext() rep times. Reproducible
 * from the one base seed, so varying the seed per repeat keeps the run replayable. */
unsigned long long runnerSeedForRepeat(unsigned long long base_seed, int rep);

void runnerPrintUsage(void);

#endif
