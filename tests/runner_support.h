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

/*
 * Compare two byte buffers of length size, skipping byte ranges in
 * excludes (must be sorted by ascending offset and non-overlapping).
 * Returns 1 on match, 0 on diff; on diff, *first_diff_out (if non-NULL)
 * is set to the offset of the first differing byte.
 */
int runnerSnapshotCompare(const unsigned char *a, const unsigned char *b, size_t size,
		const struct snapshotExcludeRange *excludes, int exclude_count,
		size_t *first_diff_out);

struct testRunnerOptions {
	const char *filter;
	const char *include_tag;
	const char *exclude_tag;
	const char *quarantine_path;
	int list_only;
	int help_requested;
	int fail_fast;
	int repeat;
	int shuffle;
	int validate_reset;
	int no_quarantine;
	int parse_error;
	int seed_specified;
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

/*
 * Populate q from path. Returns 0 on success or if path does not exist
 * (clean checkouts work without a quarantine file). Returns -1 on parse or
 * I/O failure; on -1, *error_out (if non-NULL) is set to a freshly malloc'd
 * message and the caller owns the buffer.
 */
int quarantineListLoad(struct quarantineList *q, const char *path, char **error_out);

void runnerPrintUsage(void);

#endif
