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
	const char *quarantine_path;
	const char *metrics_out;
	int list_only;
	int help_requested;
	int fail_fast;
	int repeat;
	int shuffle;
	int validate_reset;
	int no_quarantine;
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

struct quarantineList {
	char **names;
	int count;
	int cap;
};

void quarantineListInit(struct quarantineList *q);
void quarantineListFree(struct quarantineList *q);
int quarantineListAppend(struct quarantineList *q, const char *name);
int quarantineListContains(const struct quarantineList *q, const char *name);

/* Missing path is success with empty list. On -1, *error_out (if non-NULL)
 * receives a malloc'd message the caller owns. */
int quarantineListLoad(struct quarantineList *q, const char *path, char **error_out);

void runnerPrintUsage(void);

#endif
