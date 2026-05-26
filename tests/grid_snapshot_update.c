#define _DEFAULT_SOURCE

#include "grid_snapshot_update.h"

#include "test_grid_snapshot.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *stash_path(void) {
	const char *p = getenv("ROTIDE_UPDATE_GOLDEN_STASH");
	if (p == NULL || p[0] == '\0') {
		return NULL;
	}
	return p;
}

/* JSON-escape `text` into `out`. Used for the stash row. */
static void json_escape_into(FILE *out, const char *text) {
	for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
		unsigned char c = *p;
		switch (c) {
			case '"':
				(void)fputs("\\\"", out);
				break;
			case '\\':
				(void)fputs("\\\\", out);
				break;
			case '\b':
				(void)fputs("\\b", out);
				break;
			case '\f':
				(void)fputs("\\f", out);
				break;
			case '\n':
				(void)fputs("\\n", out);
				break;
			case '\r':
				(void)fputs("\\r", out);
				break;
			case '\t':
				(void)fputs("\\t", out);
				break;
			default:
				if (c < 0x20) {
					(void)fprintf(out, "\\u%04x", c);
				} else {
					(void)fputc(c, out);
				}
				break;
		}
	}
}

static int append_stash_row(const char *path, const char *file, int line, const char *actual) {
	int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
	if (fd < 0) {
		return -1;
	}
	/* Compose the row into a memory buffer first so we can write it as
	 * one atomic O_APPEND call. The stash row size is bounded by the
	 * grid size + escape overhead — even a 200×80 grid is well under
	 * 64 KiB. PIPE_BUF on Linux is 4 KiB, so we don't get strict atomic
	 * append for larger grids; ordering between writers is not load-
	 * bearing for this workflow (apply is sequential), but lines won't
	 * tear because we never partial-write the buffer below. */
	FILE *mem = NULL;
	char *buf = NULL;
	size_t cap = 32 * 1024;
	buf = (char *)malloc(cap);
	if (buf == NULL) {
		(void)close(fd);
		return -1;
	}
	mem = fmemopen(buf, cap, "w");
	if (mem == NULL) {
		free(buf);
		(void)close(fd);
		return -1;
	}
	(void)fputs("{\"file\":\"", mem);
	json_escape_into(mem, file);
	(void)fprintf(mem, "\",\"line\":%d,\"actual\":\"", line);
	json_escape_into(mem, actual);
	(void)fputs("\"}\n", mem);
	long len = ftell(mem);
	(void)fclose(mem);
	if (len <= 0) {
		free(buf);
		(void)close(fd);
		return -1;
	}
	ssize_t off = 0;
	while (off < len) {
		ssize_t n = write(fd, buf + off, (size_t)(len - off));
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			free(buf);
			(void)close(fd);
			return -1;
		}
		off += n;
	}
	free(buf);
	(void)close(fd);
	return 0;
}

int editor_grid_snapshot_check_or_stash(const char *expected, const char *actual, const char *file,
                                        int line) {
	const char *stash = stash_path();
	if (stash == NULL) {
		return editor_grid_snapshot_diff(expected, actual);
	}
	if (strcmp(expected, actual) == 0) {
		return 0;
	}
	if (append_stash_row(stash, file, line, actual) != 0) {
		(void)fprintf(stderr,
		              "grid-snapshot-update: warning: failed to write stash row "
		              "for %s:%d: %s\n",
		              file, line, strerror(errno));
	}
	return 0;
}
