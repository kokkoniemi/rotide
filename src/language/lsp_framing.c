#include "language/lsp_framing.h"

#include "support/size_utils.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

long long editorLspMonotonicMillis(void) {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		return 0;
	}
	return (long long)ts.tv_sec * 1000LL + (long long)(ts.tv_nsec / 1000000L);
}

int editorLspWriteAll(int fd, const char *buf, size_t len) {
	while (len > 0) {
		ssize_t written = write(fd, buf, len);
		if (written == -1) {
			if (errno == EINTR) {
				continue;
			}
			return 0;
		}
		if (written == 0) {
			errno = EPIPE;
			return 0;
		}
		buf += (size_t)written;
		len -= (size_t)written;
	}
	return 1;
}

int editorLspReadWithDeadline(int fd, char *buf, size_t len, long long deadline_ms) {
	size_t total = 0;
	while (total < len) {
		long long now = editorLspMonotonicMillis();
		int wait_ms = 0;
		if (deadline_ms > 0) {
			if (now >= deadline_ms) {
				errno = ETIMEDOUT;
				return 0;
			}
			long long remaining = deadline_ms - now;
			wait_ms = remaining > INT_MAX ? INT_MAX : (int)remaining;
		}

		struct pollfd pfd = {
		        .fd = fd,
		        .events = POLLIN,
		        .revents = 0,
		};

		int polled = poll(&pfd, 1, wait_ms);
		if (polled == -1) {
			if (errno == EINTR) {
				continue;
			}
			return 0;
		}
		if (polled == 0) {
			errno = ETIMEDOUT;
			return 0;
		}
		if ((pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
			errno = EPIPE;
			return 0;
		}

		ssize_t nread = read(fd, buf + total, len - total);
		if (nread == -1) {
			if (errno == EINTR) {
				continue;
			}
			return 0;
		}
		if (nread == 0) {
			errno = EPIPE;
			return 0;
		}
		total += (size_t)nread;
	}
	return 1;
}

int editorLspParseContentLength(const char *header, size_t *length_out) {
	if (header == NULL || length_out == NULL) {
		return 0;
	}

	const char *line = header;
	while (*line != '\0') {
		const char *line_end = strstr(line, "\r\n");
		if (line_end == NULL) {
			return 0;
		}
		if (line_end == line) {
			break;
		}
		if (strncasecmp(line, "Content-Length:", 15) == 0) {
			const char *value = line + 15;
			while (*value == ' ' || *value == '\t') {
				value++;
			}

			size_t parsed = 0;
			for (const char *p = value; p < line_end; p++) {
				if (!isdigit((unsigned char)*p)) {
					return 0;
				}
				size_t digit = (size_t)(*p - '0');
				/* Reject overflow before it happens — `parsed * 10 + digit`
				 * would otherwise wrap silently for huge inputs and produce
				 * a small payload_len. */
				if (parsed > (SIZE_MAX - digit) / 10) {
					return 0;
				}
				parsed = parsed * 10 + digit;
			}
			*length_out = parsed;
			return 1;
		}
		line = line_end + 2;
	}

	return 0;
}

char *editorLspReadFrame(int fd, int timeout_ms) {
	long long now = editorLspMonotonicMillis();
	long long deadline_ms = now + timeout_ms;
	char header[ROTIDE_LSP_MAX_HEADER_BYTES + 1];
	size_t header_len = 0;

	while (header_len < ROTIDE_LSP_MAX_HEADER_BYTES) {
		if (!editorLspReadWithDeadline(fd, &header[header_len], 1, deadline_ms)) {
			return NULL;
		}
		header_len++;
		header[header_len] = '\0';
		if (header_len >= 4 && header[header_len - 4] == '\r' &&
		    header[header_len - 3] == '\n' && header[header_len - 2] == '\r' &&
		    header[header_len - 1] == '\n') {
			break;
		}
	}

	if (header_len >= ROTIDE_LSP_MAX_HEADER_BYTES) {
		errno = EMSGSIZE;
		return NULL;
	}

	size_t payload_len = 0;
	if (!editorLspParseContentLength(header, &payload_len)) {
		errno = EPROTO;
		return NULL;
	}

	if (payload_len > ROTIDE_LSP_MAX_PAYLOAD_BYTES) {
		errno = EMSGSIZE;
		return NULL;
	}

	size_t alloc_len = 0;
	if (!editorSizeAdd(payload_len, 1, &alloc_len)) {
		errno = EOVERFLOW;
		return NULL;
	}
	char *payload = malloc(alloc_len);
	if (payload == NULL) {
		errno = ENOMEM;
		return NULL;
	}
	if (!editorLspReadWithDeadline(fd, payload, payload_len, deadline_ms)) {
		free(payload);
		return NULL;
	}
	payload[payload_len] = '\0';
	return payload;
}

int editorLspSendRawJsonToFd(int fd, const char *json) {
	if (json == NULL || fd == -1) {
		return 0;
	}

	size_t json_len = strlen(json);
	char header[64];
	int header_len = snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n", json_len);
	if (header_len <= 0 || (size_t)header_len >= sizeof(header)) {
		errno = EOVERFLOW;
		return 0;
	}

	if (!editorLspWriteAll(fd, header, (size_t)header_len) ||
	    !editorLspWriteAll(fd, json, json_len)) {
		return 0;
	}
	return 1;
}
