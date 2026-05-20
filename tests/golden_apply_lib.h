#ifndef TESTS_GOLDEN_APPLY_LIB_H
#define TESTS_GOLDEN_APPLY_LIB_H

#include <stddef.h>
#include <stdio.h>

/* In-memory view of a single stash row produced by ASSERT_GRID_EQ in
 * --update-golden mode. `actual` is heap-allocated; ownership transfers
 * to the caller via editor_golden_free_entries(). */
struct goldenStashEntry {
	char file[1024];
	int line;
	char *actual;
};

/* Parse a single JSONL row into `out`. Returns 1 on success, 0 if the
 * row is malformed. On success, *out has a malloc'd actual. */
int editor_golden_parse_stash_line(const char *line, struct goldenStashEntry *out);

/* Load every row of `path` into a heap-allocated array. *count_out gets
 * the entry count; the caller owns *entries_out via
 * editor_golden_free_entries(). Returns 0 on success, -1 on I/O failure.
 * Malformed rows are skipped (counted in *skipped_out if non-NULL).
 */
int editor_golden_load_stash(const char *path, struct goldenStashEntry **entries_out,
                             int *count_out, int *skipped_out);

void editor_golden_free_entries(struct goldenStashEntry *entries, int count);

/* Walk `text` (a source file's contents) and replace each
 * `/ * golden-start * / ... / * golden-end * /` block that follows a
 * stash entry's recorded line. Returns a newly-malloc'd rewritten
 * string (NUL-terminated) on success; caller frees with free().
 *
 * `entries` MUST be sorted ascending by `line` and all reference the
 * same source file (the caller is expected to group entries by file
 * before calling). Mismatched entries — those whose recorded line has
 * no corresponding golden-start marker — are reported via *skipped_out
 * (if non-NULL) and a one-line warning to `log` (if non-NULL).
 */
char *editor_golden_rewrite_text(const char *text, size_t text_len,
                                 const struct goldenStashEntry *entries, int entry_count,
                                 int *applied_out, int *skipped_out, FILE *log);

/* Convenience wrapper that loads `path`, rewrites it, and writes it back
 * atomically (temp file + rename). Returns 0 on success, -1 on I/O. */
int editor_golden_rewrite_file(const char *path, const struct goldenStashEntry *entries,
                               int entry_count, int *applied_out, int *skipped_out, FILE *log);

#endif
