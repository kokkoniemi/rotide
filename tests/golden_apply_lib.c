#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include "golden_apply_lib.h"

#include "grid_snapshot_format.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Same constrained JSONL format we use elsewhere: a flat object with simple
 * `"key":"value"` or `"key":NUMBER` pairs. Strings contain only the
 * limited escape set our writer emits: \" \\ \n \t \r \b \f \uXXXX.
 */

static const char *json_find_key(const char *line, const char *key) {
	char needle[128];
	int n = snprintf(needle, sizeof(needle), "\"%s\":", key);
	if (n < 0 || (size_t)n >= sizeof(needle)) {
		return NULL;
	}
	return strstr(line, needle);
}

/* Decode a JSON string starting at the opening `"`. *p must point at
 * the `"`. Writes the decoded bytes into a newly malloc'd buffer
 * (caller frees) and stores it in *out. Returns 1 on success, 0 on
 * malformed input. */
static int json_decode_string(const char *p, char **out) {
	if (*p != '"') {
		return 0;
	}
	p++;
	size_t cap = 64;
	size_t len = 0;
	char *buf = (char *)malloc(cap);
	if (buf == NULL) {
		return 0;
	}
	while (*p != '\0' && *p != '"') {
		char c;
		if (*p == '\\' && p[1] != '\0') {
			switch (p[1]) {
			case '"':  c = '"'; p += 2; break;
			case '\\': c = '\\'; p += 2; break;
			case 'n':  c = '\n'; p += 2; break;
			case 't':  c = '\t'; p += 2; break;
			case 'r':  c = '\r'; p += 2; break;
			case 'b':  c = '\b'; p += 2; break;
			case 'f':  c = '\f'; p += 2; break;
			case 'u': {
				if (!isxdigit((unsigned char)p[2]) || !isxdigit((unsigned char)p[3]) ||
						!isxdigit((unsigned char)p[4]) || !isxdigit((unsigned char)p[5])) {
					free(buf);
					return 0;
				}
				char hex[5] = {p[2], p[3], p[4], p[5], '\0'};
				unsigned v = (unsigned)strtoul(hex, NULL, 16);
				if (v > 0x7F) {
					/* Our writer only emits \uXXXX for control bytes;
					 * higher code points appear as raw UTF-8. Refuse to
					 * decode anything we wouldn't have emitted. */
					free(buf);
					return 0;
				}
				c = (char)v;
				p += 6;
				break;
			}
			default:
				free(buf);
				return 0;
			}
		} else {
			c = *p++;
		}
		if (len + 1 >= cap) {
			size_t new_cap = cap * 2;
			char *grown = (char *)realloc(buf, new_cap);
			if (grown == NULL) {
				free(buf);
				return 0;
			}
			buf = grown;
			cap = new_cap;
		}
		buf[len++] = c;
	}
	if (*p != '"') {
		free(buf);
		return 0;
	}
	buf[len] = '\0';
	*out = buf;
	return 1;
}

int editor_golden_parse_stash_line(const char *line, struct goldenStashEntry *out) {
	if (line == NULL || out == NULL) {
		return 0;
	}
	memset(out, 0, sizeof(*out));

	const char *file_pos = json_find_key(line, "file");
	const char *line_pos = json_find_key(line, "line");
	const char *actual_pos = json_find_key(line, "actual");
	if (file_pos == NULL || line_pos == NULL || actual_pos == NULL) {
		return 0;
	}

	char *file_str = NULL;
	if (!json_decode_string(file_pos + strlen("\"file\":"), &file_str)) {
		return 0;
	}
	size_t flen = strlen(file_str);
	if (flen >= sizeof(out->file)) {
		flen = sizeof(out->file) - 1;
	}
	memcpy(out->file, file_str, flen);
	out->file[flen] = '\0';
	free(file_str);

	char *end = NULL;
	long ln = strtol(line_pos + strlen("\"line\":"), &end, 10);
	if (ln <= 0 || end == NULL) {
		return 0;
	}
	out->line = (int)ln;

	if (!json_decode_string(actual_pos + strlen("\"actual\":"), &out->actual)) {
		return 0;
	}
	return 1;
}

int editor_golden_load_stash(const char *path,
		struct goldenStashEntry **entries_out, int *count_out,
		int *skipped_out) {
	if (path == NULL || entries_out == NULL || count_out == NULL) {
		return -1;
	}
	*entries_out = NULL;
	*count_out = 0;
	if (skipped_out != NULL) {
		*skipped_out = 0;
	}
	FILE *f = fopen(path, "r");
	if (f == NULL) {
		return -1;
	}
	struct goldenStashEntry *arr = NULL;
	int cap = 0;
	int count = 0;
	int skipped = 0;
	char *line = NULL;
	size_t line_cap = 0;
	ssize_t len;
	while ((len = getline(&line, &line_cap, f)) != -1) {
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
			line[--len] = '\0';
		}
		if (len == 0) {
			continue;
		}
		if (count == cap) {
			int new_cap = cap == 0 ? 16 : cap * 2;
			struct goldenStashEntry *grown = (struct goldenStashEntry *)realloc(
				arr, (size_t)new_cap * sizeof(*arr));
			if (grown == NULL) {
				free(line);
				editor_golden_free_entries(arr, count);
				(void)fclose(f);
				return -1;
			}
			arr = grown;
			cap = new_cap;
		}
		if (!editor_golden_parse_stash_line(line, &arr[count])) {
			skipped++;
			continue;
		}
		count++;
	}
	free(line);
	(void)fclose(f);
	*entries_out = arr;
	*count_out = count;
	if (skipped_out != NULL) {
		*skipped_out = skipped;
	}
	return 0;
}

void editor_golden_free_entries(struct goldenStashEntry *entries, int count) {
	if (entries == NULL) {
		return;
	}
	for (int i = 0; i < count; i++) {
		free(entries[i].actual);
	}
	free(entries);
}

struct outBuf {
	char *data;
	size_t len;
	size_t cap;
};

static int out_reserve(struct outBuf *o, size_t extra) {
	if (o->len + extra + 1 <= o->cap) {
		return 0;
	}
	size_t new_cap = o->cap == 0 ? 4096 : o->cap;
	while (new_cap < o->len + extra + 1) {
		new_cap *= 2;
	}
	char *grown = (char *)realloc(o->data, new_cap);
	if (grown == NULL) {
		return -1;
	}
	o->data = grown;
	o->cap = new_cap;
	return 0;
}

static int out_append(struct outBuf *o, const char *s, size_t n) {
	if (out_reserve(o, n) != 0) {
		return -1;
	}
	memcpy(o->data + o->len, s, n);
	o->len += n;
	o->data[o->len] = '\0';
	return 0;
}

/* Scan `text` from `from` for the next occurrence of `needle` that
 * appears at the start of a line (allowing leading whitespace). Returns
 * the offset of the marker word (not the indent) or (size_t)-1 if
 * missing. `*indent_len_out` (if non-NULL) gets the length of the
 * leading whitespace before the marker. `*line_end_out` gets the offset
 * of the first byte AFTER the line containing the marker (the byte
 * after '\n', or text_len if no trailing newline). */
static size_t find_marker_line(const char *text, size_t from, size_t text_len,
		const char *needle, size_t *indent_len_out, size_t *line_end_out) {
	size_t nlen = strlen(needle);
	size_t pos = from;
	while (pos < text_len) {
		/* Find the start of the next line. */
		size_t line_start = pos;
		size_t i = line_start;
		while (i < text_len && (text[i] == ' ' || text[i] == '\t')) {
			i++;
		}
		size_t after_indent = i;
		if (after_indent + nlen <= text_len
				&& memcmp(text + after_indent, needle, nlen) == 0) {
			if (indent_len_out != NULL) {
				*indent_len_out = after_indent - line_start;
			}
			/* Compute line end. */
			size_t end = after_indent + nlen;
			while (end < text_len && text[end] != '\n') {
				end++;
			}
			if (end < text_len) {
				end++; /* include the newline */
			}
			if (line_end_out != NULL) {
				*line_end_out = end;
			}
			return after_indent;
		}
		/* Skip to next line. */
		while (i < text_len && text[i] != '\n') {
			i++;
		}
		if (i < text_len) {
			i++;
		}
		if (i == pos) {
			break;
		}
		pos = i;
	}
	return (size_t)-1;
}

/* Locate the offset in `text` where the line numbered `target_line`
 * begins. Returns text_len if `target_line` is past the end. */
static size_t offset_of_line(const char *text, size_t text_len, int target_line) {
	int line = 1;
	size_t i = 0;
	while (i < text_len && line < target_line) {
		if (text[i] == '\n') {
			line++;
		}
		i++;
	}
	return i;
}

char *editor_golden_rewrite_text(const char *text, size_t text_len,
		const struct goldenStashEntry *entries, int entry_count,
		int *applied_out, int *skipped_out, FILE *log) {
	if (applied_out != NULL) {
		*applied_out = 0;
	}
	if (skipped_out != NULL) {
		*skipped_out = 0;
	}
	if (text == NULL) {
		return NULL;
	}
	struct outBuf out = {0};
	size_t copied = 0;

	for (int e = 0; e < entry_count; e++) {
		const struct goldenStashEntry *ent = &entries[e];
		size_t entry_offset = offset_of_line(text, text_len, ent->line);
		if (entry_offset < copied) {
			/* Already past this line (entries out of order or
			 * overlapping); skip with a warning. */
			if (log != NULL) {
				fprintf(log,
					"golden_apply: %s:%d entry overlaps a previous one — skipped\n",
					ent->file, ent->line);
			}
			if (skipped_out != NULL) {
				(*skipped_out)++;
			}
			continue;
		}
		size_t indent_len = 0;
		size_t start_line_end = 0;
		size_t start_pos = find_marker_line(text, entry_offset, text_len,
			"/* golden-start */", &indent_len, &start_line_end);
		if (start_pos == (size_t)-1) {
			if (log != NULL) {
				fprintf(log,
					"golden_apply: %s:%d no /* golden-start */ marker after line — skipped\n",
					ent->file, ent->line);
			}
			if (skipped_out != NULL) {
				(*skipped_out)++;
			}
			continue;
		}
		size_t end_line_end = 0;
		size_t end_pos = find_marker_line(text, start_line_end, text_len,
			"/* golden-end */", NULL, &end_line_end);
		if (end_pos == (size_t)-1) {
			if (log != NULL) {
				fprintf(log,
					"golden_apply: %s:%d /* golden-start */ at offset %zu lacks closing /* golden-end */ — skipped\n",
					ent->file, ent->line, start_pos);
			}
			if (skipped_out != NULL) {
				(*skipped_out)++;
			}
			continue;
		}

		/* Copy everything up through the golden-start line. */
		if (out_append(&out, text + copied, start_line_end - copied) != 0) {
			goto oom;
		}

		/* Format the new content as concatenated C string literals,
		 * indented to match the start marker. */
		char indent_buf[128];
		size_t use = indent_len < sizeof(indent_buf) - 1
			? indent_len : sizeof(indent_buf) - 1;
		memcpy(indent_buf, text + start_pos - indent_len, use);
		indent_buf[use] = '\0';

		char *literal_buf = NULL;
		size_t literal_len = 0;
		FILE *m = open_memstream(&literal_buf, &literal_len);
		if (m == NULL) {
			goto oom;
		}
		editor_grid_snapshot_emit_c_string(ent->actual, indent_buf, m);
		(void)fclose(m);
		int append_rc = literal_buf != NULL
			? out_append(&out, literal_buf, literal_len) : 0;
		free(literal_buf);
		if (append_rc != 0) {
			goto oom;
		}

		/* Find the start of the line containing the golden-end marker
		 * and copy it verbatim. */
		size_t end_line_start = end_pos;
		while (end_line_start > 0 && text[end_line_start - 1] != '\n') {
			end_line_start--;
		}
		if (out_append(&out, text + end_line_start, end_line_end - end_line_start) != 0) {
			goto oom;
		}
		copied = end_line_end;
		if (applied_out != NULL) {
			(*applied_out)++;
		}
	}

	/* Copy the tail. */
	if (out_append(&out, text + copied, text_len - copied) != 0) {
		goto oom;
	}
	return out.data;

oom:
	free(out.data);
	return NULL;
}

int editor_golden_rewrite_file(const char *path,
		const struct goldenStashEntry *entries, int entry_count,
		int *applied_out, int *skipped_out, FILE *log) {
	if (path == NULL) {
		return -1;
	}
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		return -1;
	}
	struct stat st;
	if (fstat(fd, &st) != 0) {
		(void)close(fd);
		return -1;
	}
	size_t size = (size_t)st.st_size;
	char *buf = (char *)malloc(size + 1);
	if (buf == NULL) {
		(void)close(fd);
		return -1;
	}
	size_t off = 0;
	while (off < size) {
		ssize_t n = read(fd, buf + off, size - off);
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			free(buf);
			(void)close(fd);
			return -1;
		}
		if (n == 0) {
			break;
		}
		off += (size_t)n;
	}
	(void)close(fd);
	buf[off] = '\0';

	char *rewritten = editor_golden_rewrite_text(buf, off, entries, entry_count,
		applied_out, skipped_out, log);
	free(buf);
	if (rewritten == NULL) {
		return -1;
	}
	size_t rewritten_len = strlen(rewritten);

	char tmp_path[1100];
	if ((size_t)snprintf(tmp_path, sizeof(tmp_path), "%s.tmp.XXXXXX", path)
			>= sizeof(tmp_path)) {
		free(rewritten);
		return -1;
	}
	int wfd = mkstemp(tmp_path);
	if (wfd < 0) {
		free(rewritten);
		return -1;
	}
	off = 0;
	while (off < rewritten_len) {
		ssize_t n = write(wfd, rewritten + off, rewritten_len - off);
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			(void)close(wfd);
			(void)unlink(tmp_path);
			free(rewritten);
			return -1;
		}
		off += (size_t)n;
	}
	(void)fchmod(wfd, st.st_mode & 0777);
	(void)close(wfd);
	free(rewritten);
	if (rename(tmp_path, path) != 0) {
		(void)unlink(tmp_path);
		return -1;
	}
	return 0;
}
