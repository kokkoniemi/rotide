/* Read a JSONL stash and print a unified-style line diff per entry,
 * comparing the actual capture to the literal that currently lives
 * between the source file's golden markers. Never modifies the source
 * files — preview-only counterpart to golden_apply.
 *
 * Usage:
 *   golden_diff_report [--stash tests/artifacts/goldens.jsonl]
 *
 * Exits 0 if the stash is empty (no pending updates), 1 if any entry
 * has a non-empty diff, 2 on I/O failure or unparseable rows.
 */

#include "golden_apply_lib.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *read_whole_file(const char *path, size_t *len_out) {
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		return NULL;
	}
	struct stat st;
	if (fstat(fd, &st) != 0) {
		(void)close(fd);
		return NULL;
	}
	size_t size = (size_t)st.st_size;
	char *buf = (char *)malloc(size + 1);
	if (buf == NULL) {
		(void)close(fd);
		return NULL;
	}
	size_t off = 0;
	while (off < size) {
		ssize_t n = read(fd, buf + off, size - off);
		if (n <= 0) {
			break;
		}
		off += (size_t)n;
	}
	(void)close(fd);
	buf[off] = '\0';
	if (len_out != NULL) {
		*len_out = off;
	}
	return buf;
}

/* Find the next `/ * golden-start * /` at or after `from_line` in
 * `text`. On success returns a pointer to the byte AFTER the end of the
 * marker line, and sets *end_of_block to point at the start of the
 * line containing the matching `/ * golden-end * /`. Returns NULL on
 * miss. */
static const char *find_block_after_line(const char *text, size_t text_len, int from_line,
                                         const char **end_of_block) {
	int line = 1;
	size_t i = 0;
	while (i < text_len && line < from_line) {
		if (text[i] == '\n') {
			line++;
		}
		i++;
	}
	const char *start_needle = "/* golden-start */";
	const char *end_needle = "/* golden-end */";
	const char *p = strstr(text + i, start_needle);
	if (p == NULL) {
		return NULL;
	}
	/* advance past the line containing the start marker */
	const char *after = strchr(p, '\n');
	if (after == NULL) {
		return NULL;
	}
	after++;
	const char *end_marker = strstr(after, end_needle);
	if (end_marker == NULL) {
		return NULL;
	}
	/* back up to start of that line */
	const char *end_line_start = end_marker;
	while (end_line_start > text && *(end_line_start - 1) != '\n') {
		end_line_start--;
	}
	*end_of_block = end_line_start;
	return after;
}

/* Parse the source's existing block back into a plain text grid by
 * stripping the surrounding `"..."` quoting and decoding the standard
 * C escapes our writer/formatter emit. Returns a malloc'd string
 * (caller frees) or NULL on parse failure. */
static char *decode_existing_block(const char *block, size_t block_len) {
	char *out = (char *)malloc(block_len + 1);
	if (out == NULL) {
		return NULL;
	}
	size_t outlen = 0;
	const char *p = block;
	const char *end = block + block_len;
	while (p < end) {
		while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
			p++;
		}
		if (p >= end) {
			break;
		}
		if (*p != '"') {
			/* Unexpected token — bail out. */
			free(out);
			return NULL;
		}
		p++;
		while (p < end && *p != '"') {
			if (*p == '\\' && p + 1 < end) {
				switch (p[1]) {
					case 'n':
						out[outlen++] = '\n';
						p += 2;
						break;
					case 't':
						out[outlen++] = '\t';
						p += 2;
						break;
					case 'r':
						out[outlen++] = '\r';
						p += 2;
						break;
					case 'b':
						out[outlen++] = '\b';
						p += 2;
						break;
					case 'f':
						out[outlen++] = '\f';
						p += 2;
						break;
					case '\\':
						out[outlen++] = '\\';
						p += 2;
						break;
					case '"':
						out[outlen++] = '"';
						p += 2;
						break;
					case '0':
					case '1':
					case '2':
					case '3':
					case '4':
					case '5':
					case '6':
					case '7': {
						int octal_chars = 0;
						int v = 0;
						p++;
						while (octal_chars < 3 && p < end && *p >= '0' &&
						       *p <= '7') {
							v = v * 8 + (*p - '0');
							p++;
							octal_chars++;
						}
						out[outlen++] = (char)v;
						break;
					}
					default:
						out[outlen++] = p[1];
						p += 2;
						break;
				}
			} else {
				out[outlen++] = *p++;
			}
		}
		if (p < end && *p == '"') {
			p++;
		}
	}
	out[outlen] = '\0';
	return out;
}

/* Print a single unified-ish diff: lines that match get a space prefix,
 * extra lines in `expected` get `-`, extra in `actual` get `+`, matching
 * editor_grid_snapshot_diff's stderr format. */
static void print_line_diff(const char *expected, const char *actual, FILE *out) {
	const char *ep = expected;
	const char *ap = actual;
	while (*ep != '\0' || *ap != '\0') {
		const char *e_eol = strchr(ep, '\n');
		const char *a_eol = strchr(ap, '\n');
		size_t elen = e_eol != NULL ? (size_t)(e_eol - ep) : strlen(ep);
		size_t alen = a_eol != NULL ? (size_t)(a_eol - ap) : strlen(ap);
		int same = (elen == alen) && memcmp(ep, ap, elen) == 0;
		if (same) {
			(void)fprintf(out, "  %.*s\n", (int)elen, ep);
		} else {
			if (*ep != '\0') {
				(void)fprintf(out, "- %.*s\n", (int)elen, ep);
			}
			if (*ap != '\0') {
				(void)fprintf(out, "+ %.*s\n", (int)alen, ap);
			}
		}
		ep += elen;
		if (*ep == '\n') {
			ep++;
		}
		ap += alen;
		if (*ap == '\n') {
			ap++;
		}
	}
}

int main(int argc, char **argv) {
	const char *stash_path = "tests/artifacts/goldens.jsonl";
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			(void)fprintf(stdout, "usage: golden_diff_report [--stash PATH]\n");
			return 0;
		}
		if (strcmp(argv[i], "--stash") == 0 && i + 1 < argc) {
			stash_path = argv[++i];
			continue;
		}
		(void)fprintf(stderr, "golden_diff_report: unknown arg: %s\n", argv[i]);
		return 2;
	}

	struct goldenStashEntry *entries = NULL;
	int count = 0;
	int skipped_parse = 0;
	if (editor_golden_load_stash(stash_path, &entries, &count, &skipped_parse) != 0) {
		(void)fprintf(stderr, "golden_diff_report: cannot read %s\n", stash_path);
		return 2;
	}
	if (skipped_parse > 0) {
		(void)fprintf(stderr, "golden_diff_report: %d malformed stash row(s) skipped\n",
		              skipped_parse);
	}
	if (count == 0) {
		(void)fprintf(stdout, "golden_diff_report: stash is empty\n");
		editor_golden_free_entries(entries, count);
		return 0;
	}

	int any_diff = 0;
	int ok = 0;
	for (int i = 0; i < count; i++) {
		const struct goldenStashEntry *ent = &entries[i];
		size_t src_len = 0;
		char *src = read_whole_file(ent->file, &src_len);
		if (src == NULL) {
			(void)fprintf(stderr, "golden_diff_report: %s:%d cannot read source file\n",
			              ent->file, ent->line);
			continue;
		}
		const char *block_end = NULL;
		const char *block_start =
		        find_block_after_line(src, src_len, ent->line, &block_end);
		if (block_start == NULL || block_end == NULL || block_end <= block_start) {
			(void)fprintf(
			        stderr,
			        "golden_diff_report: %s:%d no golden-start/end pair after recorded "
			        "line\n",
			        ent->file, ent->line);
			free(src);
			continue;
		}
		char *existing =
		        decode_existing_block(block_start, (size_t)(block_end - block_start));
		if (existing == NULL) {
			(void)fprintf(stderr,
			              "golden_diff_report: %s:%d cannot decode existing literal\n",
			              ent->file, ent->line);
			free(src);
			continue;
		}
		(void)fprintf(stdout, "=== %s:%d ===\n", ent->file, ent->line);
		if (strcmp(existing, ent->actual) == 0) {
			(void)fprintf(stdout, "  (no change)\n");
		} else {
			any_diff = 1;
			print_line_diff(existing, ent->actual, stdout);
		}
		free(existing);
		free(src);
		ok++;
	}

	editor_golden_free_entries(entries, count);
	if (ok == 0) {
		return 2;
	}
	return any_diff ? 1 : 0;
}
