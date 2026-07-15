#include "render/write_buf.h"

#include "rotide.h"
#include "support/alloc.h"
#include "support/size_utils.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

int wbAppend(struct writeBuf *wb, const char *s, size_t len) {
	if (wb == NULL) {
		return 0;
	}
	if (len == 0) {
		return 1;
	}

	size_t new_len = 0;
	if (!editorSizeAdd(wb->len, len, &new_len) || new_len > ROTIDE_MAX_TEXT_BYTES) {
		return 0;
	}

	/* Grow the capacity geometrically. Reallocating to the exact length
	 * makes frame composition O(appends * bytes) under allocators that
	 * always copy on realloc (Fil-C's GC heap). Doubling keeps it amortized
	 * O(bytes) everywhere. */
	if (new_len > wb->cap) {
		size_t new_cap = wb->cap > 0 ? wb->cap : 256;
		while (new_cap < new_len) {
			if (new_cap > ROTIDE_MAX_TEXT_BYTES / 2) {
				new_cap = new_len;
				break;
			}
			new_cap *= 2;
		}
		char *new_buf = editorRealloc(wb->b, new_cap);
		if (new_buf == NULL) {
			return 0;
		}
		wb->b = new_buf;
		wb->cap = new_cap;
	}

	memcpy(&wb->b[wb->len], s, len);
	wb->len += len;
	return 1;
}

void wbFree(struct writeBuf *wb) {
	if (wb == NULL) {
		return;
	}
	free(wb->b);
	wb->b = NULL;
	wb->len = 0;
	wb->cap = 0;
}

static int wbWriteAllFd(int fd, const char *buf, size_t len) {
	if (len == 0) {
		return 1;
	}

	errno = 0;
	size_t total = 0;
	while (total < len) {
		ssize_t written = write(fd, buf + total, len - total);
		if (written == -1) {
			if (errno == EINTR) {
				continue;
			}
			return 0;
		}
		if (written == 0) {
			errno = 0;
			return 0;
		}
		total += (size_t)written;
	}
	return 1;
}

int wbWriteAllToStdout(const struct writeBuf *wb) {
	if (wb == NULL) {
		return 0;
	}
	return wbWriteAllFd(STDOUT_FILENO, wb->b, wb->len);
}
