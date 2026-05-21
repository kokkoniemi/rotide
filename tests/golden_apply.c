#define _DEFAULT_SOURCE
#define _GNU_SOURCE

/* Read a JSONL stash produced by `rotide_tests --update-golden` and
 * rewrite each referenced source file's `/ * golden-start * / ... / *
 * golden-end * /` block with the captured grid.
 *
 * Usage:
 *   golden_apply [--stash tests/artifacts/goldens.jsonl]
 *
 * Always reports the number of regions applied / skipped per source
 * file. Exits 0 when at least one entry applied cleanly and no
 * unexpected errors were hit; non-zero on I/O failure or when *every*
 * entry was skipped (signalling the markers don't match the recorded
 * lines).
 */

#include "golden_apply_lib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int compareByFileThenLine(const void *a, const void *b) {
	const struct goldenStashEntry *ea = (const struct goldenStashEntry *)a;
	const struct goldenStashEntry *eb = (const struct goldenStashEntry *)b;
	int c = strcmp(ea->file, eb->file);
	if (c != 0) {
		return c;
	}
	if (ea->line < eb->line)
		return -1;
	if (ea->line > eb->line)
		return 1;
	return 0;
}

int main(int argc, char **argv) {
	const char *stash_path = "tests/artifacts/goldens.jsonl";
	for (int i = 1; i < argc; i++) {
		if ((strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)) {
			fprintf(stdout, "usage: golden_apply [--stash PATH]\n");
			return 0;
		}
		if (strcmp(argv[i], "--stash") == 0 && i + 1 < argc) {
			stash_path = argv[++i];
			continue;
		}
		fprintf(stderr, "golden_apply: unknown arg: %s\n", argv[i]);
		return 2;
	}

	struct goldenStashEntry *entries = NULL;
	int count = 0;
	int skipped_parse = 0;
	if (editor_golden_load_stash(stash_path, &entries, &count, &skipped_parse) != 0) {
		fprintf(stderr, "golden_apply: cannot read stash %s\n", stash_path);
		return 1;
	}
	if (skipped_parse > 0) {
		fprintf(stderr, "golden_apply: %d malformed stash row(s) skipped\n", skipped_parse);
	}
	if (count == 0) {
		fprintf(stdout, "golden_apply: stash is empty — nothing to do\n");
		editor_golden_free_entries(entries, count);
		return 0;
	}

	qsort(entries, (size_t)count, sizeof(*entries), compareByFileThenLine);

	int total_applied = 0;
	int total_skipped = 0;
	int file_failures = 0;

	int i = 0;
	while (i < count) {
		int j = i + 1;
		while (j < count && strcmp(entries[i].file, entries[j].file) == 0) {
			j++;
		}
		int applied = 0;
		int skipped = 0;
		if (editor_golden_rewrite_file(entries[i].file, &entries[i], j - i, &applied,
		                               &skipped, stderr) != 0) {
			fprintf(stderr, "golden_apply: failed to rewrite %s — leaving untouched\n",
			        entries[i].file);
			file_failures++;
		} else {
			fprintf(stdout, "golden_apply: %s — applied=%d skipped=%d\n",
			        entries[i].file, applied, skipped);
			total_applied += applied;
			total_skipped += skipped;
		}
		i = j;
	}

	editor_golden_free_entries(entries, count);

	fprintf(stdout, "golden_apply: total applied=%d skipped=%d file_errors=%d\n", total_applied,
	        total_skipped, file_failures);
	if (file_failures > 0) {
		return 1;
	}
	if (total_applied == 0 && total_skipped > 0) {
		return 1;
	}
	return 0;
}
