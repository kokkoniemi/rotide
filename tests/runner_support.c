#include "runner_support.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void runnerOptionsInit(struct testRunnerOptions *opts) {
	memset(opts, 0, sizeof(*opts));
	opts->repeat = 1;
	opts->jobs = 1;
	opts->quarantine_path = "tests/QUARANTINE.md";
}

void runnerPrintUsage(void) {
	(void)fprintf(
	        stderr,
	        "Usage: rotide_tests [options]\n"
	        "\n"
	        "Selection:\n"
	        "  --filter <substr>      Run tests whose name contains <substr>\n"
	        "  --tag <tag>            Run only suites tagged <tag>\n"
	        "  --exclude-tag <tag>    Skip suites tagged <tag>\n"
	        "  --no-quarantine        Run tests listed in tests/QUARANTINE.md too\n"
	        "  --quarantine <path>    Override the quarantine list path\n"
	        "\n"
	        "Execution:\n"
	        "  --list                 Print selected tests (suite\\tname\\ttags) and exit\n"
	        "  --fail-fast            Stop at the first FAIL\n"
	        "  --repeat <N>           Run each selected test N times\n"
	        "  --seed <u64>           Seed for randomized tests and --shuffle\n"
	        "  --shuffle              Shuffle test order (deterministic with --seed)\n"
	        "  --validate-reset       Assert that reset_editor_state restores E "
	        "byte-identically\n"
	        "  --jobs <N>             Run up to N suites in parallel as forked children\n"
	        "  --metrics-out <path>   Append one JSONL row summarising the run\n"
	        "  --update-golden [path] Capture grid-snapshot mismatches to a stash\n"
	        "                         instead of failing (default: "
	        "tests/artifacts/goldens.jsonl)\n"
	        "  -h, --help             Show this help\n");
}

static int parse_long_arg(const char *arg, const char *name, const char *next,
                          const char **value_out, int *consumed_next) {
	size_t nlen = strlen(name);
	if (strncmp(arg, name, nlen) != 0) {
		return 0;
	}
	if (arg[nlen] == '=') {
		*value_out = arg + nlen + 1;
		*consumed_next = 0;
		return 1;
	}
	if (arg[nlen] == '\0') {
		if (next == NULL) {
			*value_out = NULL;
		} else {
			*value_out = next;
		}
		*consumed_next = (next != NULL) ? 1 : 0;
		return 1;
	}
	return 0;
}

static int parse_u64(const char *s, unsigned long long *out) {
	if (s == NULL || *s == '\0') {
		return 0;
	}
	char *end = NULL;
	errno = 0;
	int base = 10;
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		base = 16;
	}
	unsigned long long v = strtoull(s, &end, base);
	if (errno != 0 || end == NULL || *end != '\0') {
		return 0;
	}
	*out = v;
	return 1;
}

static int parse_int(const char *s, int *out) {
	if (s == NULL || *s == '\0') {
		return 0;
	}
	char *end = NULL;
	errno = 0;
	long v = strtol(s, &end, 10);
	if (errno != 0 || end == NULL || *end != '\0') {
		return 0;
	}
	if (v < 1 || v > 1000000) {
		return 0;
	}
	*out = (int)v;
	return 1;
}

int runnerOptionsParse(struct testRunnerOptions *opts, int argc, char **argv) {
	for (int i = 1; i < argc; i++) {
		const char *arg = argv[i];
		const char *next = (i + 1 < argc) ? argv[i + 1] : NULL;
		const char *value = NULL;
		int consumed_next = 0;

		if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
			opts->help_requested = 1;
			return 0;
		}
		if (strcmp(arg, "--list") == 0) {
			opts->list_only = 1;
			continue;
		}
		if (strcmp(arg, "--fail-fast") == 0) {
			opts->fail_fast = 1;
			continue;
		}
		if (strcmp(arg, "--shuffle") == 0) {
			opts->shuffle = 1;
			continue;
		}
		if (strcmp(arg, "--validate-reset") == 0) {
			opts->validate_reset = 1;
			continue;
		}
		if (strcmp(arg, "--no-quarantine") == 0) {
			opts->no_quarantine = 1;
			continue;
		}
		if (parse_long_arg(arg, "--filter", next, &value, &consumed_next)) {
			if (value == NULL) {
				opts->parse_error = 1;
				opts->error_msg = "--filter requires an argument";
				return 1;
			}
			opts->filter = value;
			i += consumed_next;
			continue;
		}
		if (parse_long_arg(arg, "--tag", next, &value, &consumed_next)) {
			if (value == NULL) {
				opts->parse_error = 1;
				opts->error_msg = "--tag requires an argument";
				return 1;
			}
			opts->include_tag = value;
			i += consumed_next;
			continue;
		}
		if (parse_long_arg(arg, "--exclude-tag", next, &value, &consumed_next)) {
			if (value == NULL) {
				opts->parse_error = 1;
				opts->error_msg = "--exclude-tag requires an argument";
				return 1;
			}
			opts->exclude_tag = value;
			i += consumed_next;
			continue;
		}
		if (parse_long_arg(arg, "--quarantine", next, &value, &consumed_next)) {
			if (value == NULL) {
				opts->parse_error = 1;
				opts->error_msg = "--quarantine requires an argument";
				return 1;
			}
			opts->quarantine_path = value;
			i += consumed_next;
			continue;
		}
		if (parse_long_arg(arg, "--repeat", next, &value, &consumed_next)) {
			int n = 0;
			if (value == NULL || !parse_int(value, &n)) {
				opts->parse_error = 1;
				opts->error_msg = "--repeat requires a positive integer";
				return 1;
			}
			opts->repeat = n;
			i += consumed_next;
			continue;
		}
		if (parse_long_arg(arg, "--jobs", next, &value, &consumed_next)) {
			int n = 0;
			if (value == NULL || !parse_int(value, &n) || n < 1) {
				opts->parse_error = 1;
				opts->error_msg = "--jobs requires a positive integer";
				return 1;
			}
			opts->jobs = n;
			i += consumed_next;
			continue;
		}
		if (parse_long_arg(arg, "--metrics-out", next, &value, &consumed_next)) {
			if (value == NULL) {
				opts->parse_error = 1;
				opts->error_msg = "--metrics-out requires an argument";
				return 1;
			}
			opts->metrics_out = value;
			i += consumed_next;
			continue;
		}
		/* --update-golden takes an optional path. If the next argv slot
		 * is missing or looks like another flag, fall back to a default
		 * stash location under tests/artifacts/. */
		if (parse_long_arg(arg, "--update-golden", next, &value, &consumed_next)) {
			if (value != NULL && !(consumed_next && value[0] == '-')) {
				opts->update_golden_stash = value;
				i += consumed_next;
			} else {
				opts->update_golden_stash = "tests/artifacts/goldens.jsonl";
			}
			continue;
		}
		if (parse_long_arg(arg, "--seed", next, &value, &consumed_next)) {
			unsigned long long v = 0;
			if (value == NULL || !parse_u64(value, &v)) {
				opts->parse_error = 1;
				opts->error_msg = "--seed requires an unsigned 64-bit integer";
				return 1;
			}
			opts->seed = v;
			opts->seed_specified = 1;
			i += consumed_next;
			continue;
		}

		opts->parse_error = 1;
		opts->error_msg = "unrecognized option";
		return 1;
	}
	return 0;
}

int runnerNameMatches(const char *name, const char *filter) {
	if (filter == NULL || filter[0] == '\0') {
		return 1;
	}
	return strstr(name, filter) != NULL;
}

int runnerTagsHave(const char *tags, const char *tag) {
	if (tag == NULL || tag[0] == '\0') {
		return 0;
	}
	if (tags == NULL || tags[0] == '\0') {
		return 0;
	}
	size_t tlen = strlen(tag);
	const char *p = tags;
	while (*p) {
		while (*p == ' ' || *p == '\t') {
			p++;
		}
		const char *start = p;
		while (*p && *p != ' ' && *p != '\t') {
			p++;
		}
		size_t len = (size_t)(p - start);
		if (len == tlen && memcmp(start, tag, tlen) == 0) {
			return 1;
		}
	}
	return 0;
}

unsigned long long runnerRngNext(unsigned long long *state) {
	unsigned long long x = *state;
	if (x == 0) {
		x = 0x9E3779B97F4A7C15ULL;
	}
	x ^= x >> 12;
	x ^= x << 25;
	x ^= x >> 27;
	*state = x;
	return x * 0x2545F4914F6CDD1DULL;
}

unsigned long long runnerSeedFromOsEntropy(void) {
	unsigned long long seed = 0;
	int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	if (fd >= 0) {
		ssize_t r = read(fd, &seed, sizeof(seed));
		(void)close(fd);
		if (r == (ssize_t)sizeof(seed) && seed != 0) {
			return seed;
		}
	}
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
		seed = (unsigned long long)ts.tv_sec * 1000000000ULL +
		       (unsigned long long)ts.tv_nsec;
	} else {
		seed = (unsigned long long)time(NULL);
	}
	seed ^= (unsigned long long)getpid() << 16;
	if (seed == 0) {
		seed = 0x9E3779B97F4A7C15ULL;
	}
	return seed;
}

static int find_first_diff(const unsigned char *a, const unsigned char *b, size_t start, size_t end,
                           size_t *first_diff_out) {
	for (size_t i = start; i < end; i++) {
		if (a[i] != b[i]) {
			if (first_diff_out != NULL) {
				*first_diff_out = i;
			}
			return 0;
		}
	}
	return 1;
}

int runnerSnapshotCompare(const unsigned char *a, const unsigned char *b, size_t size,
                          const struct snapshotExcludeRange *excludes, int exclude_count,
                          size_t *first_diff_out) {
	size_t cursor = 0;
	for (int i = 0; i < exclude_count; i++) {
		size_t off = excludes[i].offset;
		size_t end = off + excludes[i].size;
		if (off > cursor) {
			if (memcmp(a + cursor, b + cursor, off - cursor) != 0) {
				return find_first_diff(a, b, cursor, off, first_diff_out);
			}
		}
		cursor = end;
	}
	if (cursor < size) {
		if (memcmp(a + cursor, b + cursor, size - cursor) != 0) {
			return find_first_diff(a, b, cursor, size, first_diff_out);
		}
	}
	return 1;
}

unsigned long long runnerSeedForRepeat(unsigned long long base_seed, int rep) {
	if (rep <= 0) {
		return base_seed;
	}
	unsigned long long state = base_seed;
	unsigned long long seed = base_seed;
	for (int i = 0; i < rep; i++) {
		seed = runnerRngNext(&state);
	}
	return seed;
}

void runnerShuffleIndices(int *indices, int count, unsigned long long seed) {
	if (count <= 1) {
		return;
	}
	unsigned long long state = seed != 0 ? seed : 0x9E3779B97F4A7C15ULL;
	for (int i = count - 1; i > 0; i--) {
		unsigned long long r = runnerRngNext(&state);
		int j = (int)(r % (unsigned long long)(i + 1));
		int tmp = indices[i];
		indices[i] = indices[j];
		indices[j] = tmp;
	}
}

void quarantineListInit(struct quarantineList *q) {
	q->names = NULL;
	q->count = 0;
	q->cap = 0;
}

void quarantineListFree(struct quarantineList *q) {
	if (q->names != NULL) {
		for (int i = 0; i < q->count; i++) {
			free(q->names[i]);
		}
		free(q->names);
	}
	q->names = NULL;
	q->count = 0;
	q->cap = 0;
}

int quarantineListAppend(struct quarantineList *q, const char *name) {
	if (q->count == q->cap) {
		int new_cap = q->cap == 0 ? 8 : q->cap * 2;
		char **grown = (char **)realloc(q->names, (size_t)new_cap * sizeof(*grown));
		if (grown == NULL) {
			return -1;
		}
		q->names = grown;
		q->cap = new_cap;
	}
	char *dup = strdup(name);
	if (dup == NULL) {
		return -1;
	}
	q->names[q->count++] = dup;
	return 0;
}

int quarantineListContains(const struct quarantineList *q, const char *name) {
	for (int i = 0; i < q->count; i++) {
		if (strcmp(q->names[i], name) == 0) {
			return 1;
		}
	}
	return 0;
}

static int is_name_char(int c) {
	return c == '_' || isalnum(c);
}

int quarantineListLoad(struct quarantineList *q, const char *path, char **error_out) {
	if (error_out != NULL) {
		*error_out = NULL;
	}
	FILE *f = fopen(path, "r");
	if (f == NULL) {
		if (errno == ENOENT) {
			return 0;
		}
		if (error_out != NULL) {
			char buf[256];
			(void)snprintf(buf, sizeof(buf), "failed to open %s: %s", path,
			               strerror(errno));
			*error_out = strdup(buf);
		}
		return -1;
	}

	char *line = NULL;
	size_t cap = 0;
	ssize_t len;
	int in_fence = 0;
	while ((len = getline(&line, &cap, f)) != -1) {
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
			line[--len] = '\0';
		}
		const char *p = line;
		while (*p == ' ' || *p == '\t') {
			p++;
		}
		if (strncmp(p, "```", 3) == 0) {
			in_fence = !in_fence;
			continue;
		}
		if (in_fence) {
			continue;
		}
		if (*p != '-') {
			continue;
		}
		p++;
		if (*p != ' ' && *p != '\t') {
			continue;
		}
		while (*p == ' ' || *p == '\t') {
			p++;
		}
		const char *start = p;
		while (is_name_char((unsigned char)*p)) {
			p++;
		}
		if (p == start) {
			continue;
		}
		size_t nlen = (size_t)(p - start);
		char name[256];
		if (nlen >= sizeof(name)) {
			nlen = sizeof(name) - 1;
		}
		memcpy(name, start, nlen);
		name[nlen] = '\0';
		if (quarantineListAppend(q, name) != 0) {
			if (error_out != NULL) {
				*error_out = strdup("out of memory while loading quarantine list");
			}
			free(line);
			(void)fclose(f);
			return -1;
		}
	}
	free(line);
	(void)fclose(f);
	return 0;
}
