#include "debug/dap_client.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define ROTIDE_DAP_MAX_HEADER_BYTES 8192

static int editorDapWriteAll(int fd, const char *buf, size_t len) {
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

static int editorDapParseContentLength(const char *header, size_t *length_out) {
	const char *line = header;
	while (line != NULL && *line != '\0') {
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
				/* Reject overflow before it happens — without this a
				 * 20+ digit Content-Length silently wraps and produces a
				 * tiny payload_len from a hostile peer. */
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

char *editorDapClientReadFrame(int from_adapter_fd) {
	char header[ROTIDE_DAP_MAX_HEADER_BYTES + 1];
	size_t header_len = 0;
	while (header_len < ROTIDE_DAP_MAX_HEADER_BYTES) {
		char ch = '\0';
		ssize_t nread = read(from_adapter_fd, &ch, 1);
		if (nread == -1) {
			if (errno == EINTR) {
				continue;
			}
			return NULL;
		}
		if (nread == 0) {
			errno = EPIPE;
			return NULL;
		}
		header[header_len++] = ch;
		header[header_len] = '\0';
		if (header_len >= 4 && memcmp(header + header_len - 4, "\r\n\r\n", 4) == 0) {
			break;
		}
	}
	if (header_len >= ROTIDE_DAP_MAX_HEADER_BYTES) {
		errno = EMSGSIZE;
		return NULL;
	}
	size_t payload_len = 0;
	if (!editorDapParseContentLength(header, &payload_len)) {
		errno = EPROTO;
		return NULL;
	}
	if (payload_len > ROTIDE_DAP_MAX_PAYLOAD_BYTES) {
		errno = EMSGSIZE;
		return NULL;
	}
	char *payload = malloc(payload_len + 1);
	if (payload == NULL) {
		errno = ENOMEM;
		return NULL;
	}
	size_t total = 0;
	while (total < payload_len) {
		ssize_t nread = read(from_adapter_fd, payload + total, payload_len - total);
		if (nread == -1) {
			if (errno == EINTR) {
				continue;
			}
			free(payload);
			return NULL;
		}
		if (nread == 0) {
			free(payload);
			errno = EPIPE;
			return NULL;
		}
		total += (size_t)nread;
	}
	payload[payload_len] = '\0';
	return payload;
}

static int editorDapClientSendRawJson(int to_adapter_fd, const char *json) {
	if (json == NULL || to_adapter_fd == -1) {
		return 0;
	}
	size_t json_len = strlen(json);
	char header[64];
	int header_len = snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n", json_len);
	if (header_len <= 0 || (size_t)header_len >= sizeof(header)) {
		return 0;
	}
	return editorDapWriteAll(to_adapter_fd, header, (size_t)header_len) &&
			editorDapWriteAll(to_adapter_fd, json, json_len);
}

int editorDapClientSendRequest(int to_adapter_fd, char *json) {
	if (json == NULL) {
		return 0;
	}
	int ok = editorDapClientSendRawJson(to_adapter_fd, json);
	free(json);
	return ok;
}
