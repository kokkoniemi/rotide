#include "lsp.h"

#include "buffer.h"
#include "size_utils.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define ROTIDE_LSP_IO_TIMEOUT_MS 2500
#define ROTIDE_LSP_MAX_HEADER_BYTES 8192

struct editorLspClient {
	pid_t pid;
	int to_server_fd;
	int from_server_fd;
	int initialized;
	int disabled_for_position_encoding;
	int next_request_id;
	int position_encoding_utf16;
};

struct editorLspString {
	char *buf;
	size_t len;
	size_t cap;
};

struct editorLspMockState {
	int enabled;
	int server_alive;
	struct editorLspTestStats stats;
	struct editorLspTestLastChange last_change;
	int definition_result_code;
	struct editorLspLocation *definition_locations;
	int definition_location_count;
};

static struct editorLspClient g_lsp_client = {
	.pid = 0,
	.to_server_fd = -1,
	.from_server_fd = -1,
	.initialized = 0,
	.disabled_for_position_encoding = 0,
	.next_request_id = 1,
	.position_encoding_utf16 = 0,
};
static struct editorLspMockState g_lsp_mock = {0};
static enum editorLspStartupFailureReason g_lsp_last_startup_failure_reason =
		EDITOR_LSP_STARTUP_FAILURE_NONE;

static int editorLspUtf8ColumnToUtf16Units(const char *text, size_t text_len, int byte_column);
static int editorLspUtf16UnitsToUtf8Column(const char *text, size_t text_len, int utf16_units);
static int editorLspReadActiveLineText(int line, char **text_out, size_t *len_out);
static int editorLspProtocolCharacterFromBufferColumn(int line, int byte_column);

static long long editorLspMonotonicMillis(void) {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		return 0;
	}
	return (long long)ts.tv_sec * 1000LL + (long long)(ts.tv_nsec / 1000000L);
}

static int editorLspStringEnsureCap(struct editorLspString *sb, size_t needed) {
	if (needed <= sb->cap) {
		return 1;
	}

	size_t new_cap = sb->cap > 0 ? sb->cap : 128;
	while (new_cap < needed) {
		if (new_cap > SIZE_MAX / 2) {
			return 0;
		}
		new_cap *= 2;
	}

	char *grown = realloc(sb->buf, new_cap);
	if (grown == NULL) {
		return 0;
	}
	sb->buf = grown;
	sb->cap = new_cap;
	return 1;
}

static int editorLspStringAppendBytes(struct editorLspString *sb, const char *bytes, size_t len) {
	if (len == 0) {
		return 1;
	}

	size_t needed = 0;
	if (!editorSizeAdd(sb->len, len, &needed) || !editorSizeAdd(needed, 1, &needed)) {
		return 0;
	}
	if (!editorLspStringEnsureCap(sb, needed)) {
		return 0;
	}

	memcpy(sb->buf + sb->len, bytes, len);
	sb->len += len;
	sb->buf[sb->len] = '\0';
	return 1;
}

static int editorLspStringAppend(struct editorLspString *sb, const char *text) {
	return editorLspStringAppendBytes(sb, text, strlen(text));
}

static int editorLspStringAppendf(struct editorLspString *sb, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	va_list ap_copy;
	va_copy(ap_copy, ap);
	int needed = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (needed < 0) {
		va_end(ap_copy);
		return 0;
	}

	size_t append_len = (size_t)needed;
	size_t total_needed = 0;
	if (!editorSizeAdd(sb->len, append_len, &total_needed) ||
			!editorSizeAdd(total_needed, 1, &total_needed)) {
		va_end(ap_copy);
		return 0;
	}
	if (!editorLspStringEnsureCap(sb, total_needed)) {
		va_end(ap_copy);
		return 0;
	}

	int written = vsnprintf(sb->buf + sb->len, sb->cap - sb->len, fmt, ap_copy);
	va_end(ap_copy);
	if (written < 0 || (size_t)written != append_len) {
		return 0;
	}
	sb->len += append_len;
	return 1;
}

static int editorLspStringAppendJsonEscaped(struct editorLspString *sb, const char *text,
		size_t len) {
	if (!editorLspStringAppendBytes(sb, "\"", 1)) {
		return 0;
	}

	for (size_t i = 0; i < len; i++) {
		unsigned char ch = (unsigned char)text[i];
		switch (ch) {
			case '"':
				if (!editorLspStringAppend(sb, "\\\"")) {
					return 0;
				}
				break;
			case '\\':
				if (!editorLspStringAppend(sb, "\\\\")) {
					return 0;
				}
				break;
			case '\b':
				if (!editorLspStringAppend(sb, "\\b")) {
					return 0;
				}
				break;
			case '\f':
				if (!editorLspStringAppend(sb, "\\f")) {
					return 0;
				}
				break;
			case '\n':
				if (!editorLspStringAppend(sb, "\\n")) {
					return 0;
				}
				break;
			case '\r':
				if (!editorLspStringAppend(sb, "\\r")) {
					return 0;
				}
				break;
			case '\t':
				if (!editorLspStringAppend(sb, "\\t")) {
					return 0;
				}
				break;
			default:
				if (ch < 0x20) {
					if (!editorLspStringAppendf(sb, "\\u%04x", (unsigned int)ch)) {
						return 0;
					}
				} else {
					if (!editorLspStringAppendBytes(sb, (const char *)&ch, 1)) {
						return 0;
					}
				}
				break;
		}
	}

	return editorLspStringAppendBytes(sb, "\"", 1);
}

static int editorLspWriteAll(int fd, const char *buf, size_t len) {
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

static int editorLspReadWithDeadline(int fd, char *buf, size_t len, long long deadline_ms) {
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

static int editorLspParseContentLength(const char *header, size_t *length_out) {
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

			unsigned long parsed = 0;
			for (const char *p = value; p < line_end; p++) {
				if (!isdigit((unsigned char)*p)) {
					return 0;
				}
				parsed = parsed * 10UL + (unsigned long)(*p - '0');
				if (parsed > SIZE_MAX) {
					return 0;
				}
			}
			*length_out = (size_t)parsed;
			return 1;
		}
		line = line_end + 2;
	}

	return 0;
}

static char *editorLspReadFrame(int fd, int timeout_ms) {
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
		if (header_len >= 4 &&
				header[header_len - 4] == '\r' &&
				header[header_len - 3] == '\n' &&
				header[header_len - 2] == '\r' &&
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

static int editorLspSendRawJson(const char *json) {
	if (json == NULL || g_lsp_client.to_server_fd == -1) {
		return 0;
	}

	size_t json_len = strlen(json);
	char header[64];
	int header_len = snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n", json_len);
	if (header_len <= 0 || (size_t)header_len >= sizeof(header)) {
		errno = EOVERFLOW;
		return 0;
	}

	if (!editorLspWriteAll(g_lsp_client.to_server_fd, header, (size_t)header_len) ||
			!editorLspWriteAll(g_lsp_client.to_server_fd, json, json_len)) {
		return 0;
	}
	return 1;
}

static int editorLspBuildFileUri(const char *path, char **uri_out) {
	if (path == NULL || uri_out == NULL) {
		return 0;
	}
	*uri_out = NULL;

	struct editorLspString sb = {0};
	if (!editorLspStringAppend(&sb, "file://")) {
		free(sb.buf);
		return 0;
	}

	for (const unsigned char *p = (const unsigned char *)path; *p != '\0'; p++) {
		unsigned char ch = *p;
		int unreserved = isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~' || ch == '/';
		if (unreserved) {
			if (!editorLspStringAppendBytes(&sb, (const char *)&ch, 1)) {
				free(sb.buf);
				return 0;
			}
		} else {
			if (!editorLspStringAppendf(&sb, "%%%02X", (unsigned int)ch)) {
				free(sb.buf);
				return 0;
			}
		}
	}

	*uri_out = sb.buf;
	return 1;
}

static int editorLspHexValue(char c) {
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

static char *editorLspDecodeFileUri(const char *uri) {
	if (uri == NULL || strncmp(uri, "file://", 7) != 0) {
		return NULL;
	}

	const char *rest = uri + 7;
	if (rest[0] != '/') {
		const char *slash = strchr(rest, '/');
		if (slash == NULL) {
			return NULL;
		}
		if ((size_t)(slash - rest) != strlen("localhost") ||
				strncasecmp(rest, "localhost", strlen("localhost")) != 0) {
			return NULL;
		}
		rest = slash;
	}

	size_t len = strlen(rest);
	char *path = malloc(len + 1);
	if (path == NULL) {
		return NULL;
	}

	size_t write_idx = 0;
	for (size_t i = 0; i < len; i++) {
		if (rest[i] == '%' && i + 2 < len) {
			int hi = editorLspHexValue(rest[i + 1]);
			int lo = editorLspHexValue(rest[i + 2]);
			if (hi >= 0 && lo >= 0) {
				path[write_idx++] = (char)((hi << 4) | lo);
				i += 2;
				continue;
			}
		}
		path[write_idx++] = rest[i];
	}
	path[write_idx] = '\0';
	return path;
}

static int editorLspParseJsonString(const char *json, char **value_out, const char **after_out) {
	if (json == NULL || value_out == NULL || json[0] != '"') {
		return 0;
	}

	struct editorLspString sb = {0};
	size_t i = 1;
	while (json[i] != '\0') {
		char ch = json[i];
		if (ch == '"') {
			i++;
			*value_out = sb.buf != NULL ? sb.buf : strdup("");
			if (*value_out == NULL) {
				free(sb.buf);
				return 0;
			}
			if (after_out != NULL) {
				*after_out = &json[i];
			}
			return 1;
		}
		if (ch == '\\') {
			i++;
			if (json[i] == '\0') {
				free(sb.buf);
				return 0;
			}
			char esc = json[i];
			switch (esc) {
				case '"':
				case '\\':
				case '/':
					if (!editorLspStringAppendBytes(&sb, &esc, 1)) {
						free(sb.buf);
						return 0;
					}
					break;
				case 'b':
					if (!editorLspStringAppendBytes(&sb, "\b", 1)) {
						free(sb.buf);
						return 0;
					}
					break;
				case 'f':
					if (!editorLspStringAppendBytes(&sb, "\f", 1)) {
						free(sb.buf);
						return 0;
					}
					break;
				case 'n':
					if (!editorLspStringAppendBytes(&sb, "\n", 1)) {
						free(sb.buf);
						return 0;
					}
					break;
				case 'r':
					if (!editorLspStringAppendBytes(&sb, "\r", 1)) {
						free(sb.buf);
						return 0;
					}
					break;
				case 't':
					if (!editorLspStringAppendBytes(&sb, "\t", 1)) {
						free(sb.buf);
						return 0;
					}
					break;
				case 'u': {
					if (json[i + 1] == '\0' || json[i + 2] == '\0' || json[i + 3] == '\0' ||
							json[i + 4] == '\0') {
						free(sb.buf);
						return 0;
					}
					int h1 = editorLspHexValue(json[i + 1]);
					int h2 = editorLspHexValue(json[i + 2]);
					int h3 = editorLspHexValue(json[i + 3]);
					int h4 = editorLspHexValue(json[i + 4]);
					if (h1 < 0 || h2 < 0 || h3 < 0 || h4 < 0) {
						free(sb.buf);
						return 0;
					}
					unsigned int code = (unsigned int)((h1 << 12) | (h2 << 8) | (h3 << 4) | h4);
					char out = code <= 0x7F ? (char)code : '?';
					if (!editorLspStringAppendBytes(&sb, &out, 1)) {
						free(sb.buf);
						return 0;
					}
					i += 4;
					break;
				}
				default:
					free(sb.buf);
					return 0;
			}
		} else {
			if (!editorLspStringAppendBytes(&sb, &ch, 1)) {
				free(sb.buf);
				return 0;
			}
		}
		i++;
	}

	free(sb.buf);
	return 0;
}

static const char *editorLspSkipWs(const char *p) {
	while (p != NULL && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) {
		p++;
	}
	return p;
}

static int editorLspParseJsonInt(const char *json, int *value_out, const char **after_out) {
	if (json == NULL || value_out == NULL) {
		return 0;
	}
	const char *p = editorLspSkipWs(json);
	if (p == NULL || (*p != '-' && !isdigit((unsigned char)*p))) {
		return 0;
	}

	int neg = 0;
	if (*p == '-') {
		neg = 1;
		p++;
	}
	if (!isdigit((unsigned char)*p)) {
		return 0;
	}

	long long value = 0;
	while (isdigit((unsigned char)*p)) {
		value = value * 10 + (*p - '0');
		if (value > INT_MAX) {
			return 0;
		}
		p++;
	}

	if (neg) {
		value = -value;
		if (value < INT_MIN) {
			return 0;
		}
	}

	*value_out = (int)value;
	if (after_out != NULL) {
		*after_out = p;
	}
	return 1;
}

static int editorLspExtractResponseId(const char *json, int *id_out) {
	if (json == NULL || id_out == NULL) {
		return 0;
	}
	const char *key = strstr(json, "\"id\"");
	if (key == NULL) {
		return 0;
	}
	const char *colon = strchr(key, ':');
	if (colon == NULL) {
		return 0;
	}
	return editorLspParseJsonInt(colon + 1, id_out, NULL);
}

static int editorLspResponseHasError(const char *json) {
	const char *key = strstr(json, "\"error\"");
	if (key == NULL) {
		return 0;
	}
	const char *colon = strchr(key, ':');
	if (colon == NULL) {
		return 0;
	}
	const char *value = editorLspSkipWs(colon + 1);
	if (value == NULL) {
		return 0;
	}
	return strncmp(value, "null", 4) != 0;
}

static int editorLspFindStringField(const char *json, const char *field_name, char **value_out) {
	if (json == NULL || field_name == NULL || value_out == NULL) {
		return 0;
	}
	*value_out = NULL;

	char key[128];
	int written = snprintf(key, sizeof(key), "\"%s\"", field_name);
	if (written <= 0 || (size_t)written >= sizeof(key)) {
		return 0;
	}

	const char *found = strstr(json, key);
	if (found == NULL) {
		return 0;
	}
	const char *colon = strchr(found, ':');
	if (colon == NULL) {
		return 0;
	}
	const char *value = editorLspSkipWs(colon + 1);
	if (value == NULL || value[0] != '"') {
		return 0;
	}
	return editorLspParseJsonString(value, value_out, NULL);
}

static const char *editorLspStrstrBounded(const char *haystack, const char *needle,
		const char *limit) {
	if (haystack == NULL || needle == NULL) {
		return NULL;
	}
	const char *found = strstr(haystack, needle);
	if (found == NULL) {
		return NULL;
	}
	if (limit != NULL && found >= limit) {
		return NULL;
	}
	return found;
}

static int editorLspParsePositionFromStart(const char *range_json, const char *limit,
		int *line_out, int *character_out) {
	const char *start = editorLspStrstrBounded(range_json, "\"start\"", limit);
	if (start == NULL) {
		return 0;
	}
	const char *line_key = editorLspStrstrBounded(start, "\"line\"", limit);
	if (line_key == NULL) {
		return 0;
	}
	const char *line_colon = strchr(line_key, ':');
	if (line_colon == NULL || (limit != NULL && line_colon >= limit)) {
		return 0;
	}
	int line = 0;
	if (!editorLspParseJsonInt(line_colon + 1, &line, NULL) || line < 0) {
		return 0;
	}

	const char *char_key = editorLspStrstrBounded(line_colon + 1, "\"character\"", limit);
	if (char_key == NULL) {
		return 0;
	}
	const char *char_colon = strchr(char_key, ':');
	if (char_colon == NULL || (limit != NULL && char_colon >= limit)) {
		return 0;
	}
	int character = 0;
	if (!editorLspParseJsonInt(char_colon + 1, &character, NULL) || character < 0) {
		return 0;
	}

	*line_out = line;
	*character_out = character;
	return 1;
}

void editorLspFreeLocations(struct editorLspLocation *locations, int count) {
	if (locations == NULL) {
		return;
	}
	for (int i = 0; i < count; i++) {
		free(locations[i].path);
	}
	free(locations);
}

int editorLspProtocolCharacterToBufferColumn(int line, int protocol_character) {
	if (protocol_character < 0) {
		return 0;
	}
	if (!g_lsp_client.position_encoding_utf16) {
		return protocol_character;
	}

	char *line_text = NULL;
	size_t line_len = 0;
	if (!editorLspReadActiveLineText(line, &line_text, &line_len)) {
		return protocol_character;
	}
	int byte_column = editorLspUtf16UnitsToUtf8Column(line_text, line_len, protocol_character);
	free(line_text);
	return byte_column;
}

static int editorLspAppendLocation(struct editorLspLocation **locations, int *count, int *cap,
		const char *path, int line, int character) {
	if (locations == NULL || count == NULL || cap == NULL || path == NULL) {
		return 0;
	}
	if (*count >= *cap) {
		int new_cap = *cap > 0 ? *cap * 2 : 4;
		if (new_cap < *count + 1) {
			new_cap = *count + 1;
		}
		size_t bytes = 0;
		if (!editorSizeMul(sizeof(**locations), (size_t)new_cap, &bytes)) {
			return 0;
		}
		struct editorLspLocation *grown = realloc(*locations, bytes);
		if (grown == NULL) {
			return 0;
		}
		*locations = grown;
		*cap = new_cap;
	}

	char *path_dup = strdup(path);
	if (path_dup == NULL) {
		return 0;
	}
	(*locations)[*count].path = path_dup;
	(*locations)[*count].line = line;
	(*locations)[*count].character = character;
	(*count)++;
	return 1;
}

static int editorLspParseLocationObjects(const char *result_json, const char *uri_key,
		const char *range_key_primary, const char *range_key_fallback,
		struct editorLspLocation **locations_out, int *count_out) {
	struct editorLspLocation *locations = NULL;
	int count = 0;
	int cap = 0;

	char key_pattern[64];
	int key_written = snprintf(key_pattern, sizeof(key_pattern), "\"%s\"", uri_key);
	if (key_written <= 0 || (size_t)key_written >= sizeof(key_pattern)) {
		return 0;
	}

	const char *scan = result_json;
	while (scan != NULL) {
		const char *uri_key_pos = strstr(scan, key_pattern);
		if (uri_key_pos == NULL) {
			break;
		}
		const char *next_uri = strstr(uri_key_pos + 1, key_pattern);

		const char *uri_colon = strchr(uri_key_pos, ':');
		if (uri_colon == NULL || (next_uri != NULL && uri_colon >= next_uri)) {
			scan = uri_key_pos + 1;
			continue;
		}
		const char *uri_value = editorLspSkipWs(uri_colon + 1);
		if (uri_value == NULL || uri_value[0] != '"') {
			scan = uri_key_pos + 1;
			continue;
		}

		char *uri = NULL;
		const char *after_uri = NULL;
		if (!editorLspParseJsonString(uri_value, &uri, &after_uri)) {
			scan = uri_key_pos + 1;
			continue;
		}
		(void)after_uri;

		char primary_pattern[64];
		int primary_written = snprintf(primary_pattern, sizeof(primary_pattern), "\"%s\"",
				range_key_primary);
		if (primary_written <= 0 || (size_t)primary_written >= sizeof(primary_pattern)) {
			free(uri);
			editorLspFreeLocations(locations, count);
			return 0;
		}

		const char *range_pos = editorLspStrstrBounded(uri_key_pos, primary_pattern, next_uri);
		if (range_pos == NULL && range_key_fallback != NULL) {
			char fallback_pattern[64];
			int fallback_written = snprintf(fallback_pattern, sizeof(fallback_pattern), "\"%s\"",
					range_key_fallback);
			if (fallback_written <= 0 || (size_t)fallback_written >= sizeof(fallback_pattern)) {
				free(uri);
				editorLspFreeLocations(locations, count);
				return 0;
			}
			range_pos = editorLspStrstrBounded(uri_key_pos, fallback_pattern, next_uri);
		}

		int line = -1;
		int character = -1;
		if (range_pos != NULL) {
			(void)editorLspParsePositionFromStart(range_pos, next_uri, &line, &character);
		}

		if (line >= 0 && character >= 0) {
			char *path = editorLspDecodeFileUri(uri);
			if (path != NULL) {
				if (!editorLspAppendLocation(&locations, &count, &cap, path, line, character)) {
					free(path);
					free(uri);
					editorLspFreeLocations(locations, count);
					return 0;
				}
				free(path);
			}
		}

		free(uri);
		scan = uri_key_pos + 1;
	}

	*locations_out = locations;
	*count_out = count;
	return 1;
}

static int editorLspParseDefinitionLocations(const char *response_json,
		struct editorLspLocation **locations_out, int *count_out) {
	*locations_out = NULL;
	*count_out = 0;

	const char *result_key = strstr(response_json, "\"result\"");
	if (result_key == NULL) {
		return 0;
	}
	const char *result_colon = strchr(result_key, ':');
	if (result_colon == NULL) {
		return 0;
	}
	const char *result = editorLspSkipWs(result_colon + 1);
	if (result == NULL) {
		return 0;
	}
	if (strncmp(result, "null", 4) == 0) {
		return 1;
	}

	if (strstr(result, "\"targetUri\"") != NULL) {
		return editorLspParseLocationObjects(result, "targetUri", "targetSelectionRange",
				"targetRange", locations_out, count_out);
	}
	return editorLspParseLocationObjects(result, "uri", "range", NULL, locations_out, count_out);
}

static int editorLspProcessAlive(void) {
	if (g_lsp_client.pid <= 0) {
		return 0;
	}

	int status = 0;
	pid_t waited = waitpid(g_lsp_client.pid, &status, WNOHANG);
	if (waited == 0) {
		return 1;
	}
	if (waited == -1 && errno == EINTR) {
		return 1;
	}
	return 0;
}

static int editorLspTryGetProcessExitCode(int *exit_code_out) {
	if (exit_code_out == NULL || g_lsp_client.pid <= 0) {
		return 0;
	}

	int status = 0;
	pid_t waited = waitpid(g_lsp_client.pid, &status, WNOHANG);
	if (waited <= 0) {
		return 0;
	}
	if (WIFEXITED(status)) {
		*exit_code_out = WEXITSTATUS(status);
		return 1;
	}
	if (WIFSIGNALED(status)) {
		*exit_code_out = 128 + WTERMSIG(status);
		return 1;
	}
	return 0;
}

static int editorLspTryGetProcessExitCodeWithWait(int timeout_ms, int *exit_code_out) {
	long long deadline_ms = editorLspMonotonicMillis() + (long long)timeout_ms;

	for (;;) {
		if (editorLspTryGetProcessExitCode(exit_code_out)) {
			return 1;
		}
		if (g_lsp_client.pid <= 0) {
			return 0;
		}
		if (editorLspMonotonicMillis() >= deadline_ms) {
			return 0;
		}
		struct timespec sleep_time = {
			.tv_sec = 0,
			.tv_nsec = 1000000L,
		};
		(void)nanosleep(&sleep_time, NULL);
	}
}

static void editorLspSetStartupFailureStatus(int timed_out) {
	g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_OTHER;
	if (timed_out) {
		editorSetStatusMsg("LSP startup failed: initialize timed out");
		return;
	}

	int saved_errno = errno;
	if (saved_errno == EPIPE) {
		int exit_code = 0;
		if (editorLspTryGetProcessExitCodeWithWait(100, &exit_code) && exit_code == 127) {
			g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_COMMAND_NOT_FOUND;
			editorSetStatusMsg("LSP startup failed: command not found (%s)",
					E.lsp_gopls_command);
			return;
		}
		editorSetStatusMsg("LSP startup failed: server exited during initialize");
		return;
	}
	if (saved_errno == EPROTO) {
		editorSetStatusMsg("LSP startup failed: invalid initialize response");
		return;
	}
	if (saved_errno == ENOMEM) {
		editorSetStatusMsg("LSP startup failed: out of memory");
		return;
	}

	editorSetStatusMsg("LSP startup failed: initialize I/O error");
}

static int editorLspUtf8ColumnToUtf16Units(const char *text, size_t text_len, int byte_column) {
	if (text == NULL || byte_column <= 0) {
		return 0;
	}

	int text_len_int = 0;
	if (!editorSizeToInt(text_len, &text_len_int)) {
		text_len_int = INT_MAX;
	}
	int clamped = byte_column;
	if (clamped < 0) {
		clamped = 0;
	}
	if (clamped > text_len_int) {
		clamped = text_len_int;
	}
	while (clamped > 0 && clamped < text_len_int &&
			editorIsUtf8ContinuationByte((unsigned char)text[clamped])) {
		clamped--;
	}

	int utf16_units = 0;
	for (int idx = 0; idx < clamped;) {
		unsigned int cp = 0;
		int seq_len = editorUtf8DecodeCodepoint(text + idx, text_len_int - idx, &cp);
		if (seq_len <= 0) {
			seq_len = 1;
			cp = (unsigned char)text[idx];
		}
		utf16_units += (cp > 0xFFFFU) ? 2 : 1;
		idx += seq_len;
	}

	return utf16_units;
}

static int editorLspUtf16UnitsToUtf8Column(const char *text, size_t text_len, int utf16_units) {
	if (text == NULL || utf16_units <= 0) {
		return 0;
	}

	int text_len_int = 0;
	if (!editorSizeToInt(text_len, &text_len_int)) {
		text_len_int = INT_MAX;
	}
	int units = 0;
	int idx = 0;
	while (idx < text_len_int) {
		unsigned int cp = 0;
		int seq_len = editorUtf8DecodeCodepoint(text + idx, text_len_int - idx, &cp);
		if (seq_len <= 0) {
			seq_len = 1;
			cp = (unsigned char)text[idx];
		}

		int cp_units = (cp > 0xFFFFU) ? 2 : 1;
		if (units + cp_units > utf16_units) {
			break;
		}
		units += cp_units;
		idx += seq_len;
	}

	return idx;
}

static int editorLspReadActiveLineText(int line, char **text_out, size_t *len_out) {
	if (text_out == NULL || len_out == NULL || line < 0) {
		return 0;
	}
	*text_out = NULL;
	*len_out = 0;

	size_t line_start = 0;
	size_t line_end = 0;
	if (!editorBufferLineByteRange(line, &line_start, &line_end)) {
		return 0;
	}

	struct editorTextSource source = {0};
	if (!editorBuildActiveTextSource(&source)) {
		return 0;
	}

	*text_out = editorTextSourceDupRange(&source, line_start, line_end, len_out);
	if (*text_out == NULL) {
		return line_end == line_start;
	}
	return 1;
}

static int editorLspProtocolCharacterFromBufferColumn(int line, int byte_column) {
	if (byte_column < 0) {
		byte_column = 0;
	}
	if (!g_lsp_client.position_encoding_utf16) {
		return byte_column;
	}
	char *line_text = NULL;
	size_t line_len = 0;
	if (!editorLspReadActiveLineText(line, &line_text, &line_len)) {
		return byte_column;
	}
	int protocol_character = editorLspUtf8ColumnToUtf16Units(line_text, line_len, byte_column);
	free(line_text);
	return protocol_character;
}

static void editorLspClientResetState(void) {
	g_lsp_client.pid = 0;
	g_lsp_client.to_server_fd = -1;
	g_lsp_client.from_server_fd = -1;
	g_lsp_client.initialized = 0;
	g_lsp_client.next_request_id = 1;
	g_lsp_client.position_encoding_utf16 = 0;
}

static void editorLspClientCleanup(int graceful_shutdown) {
	if (g_lsp_client.pid <= 0 && g_lsp_client.to_server_fd == -1 && g_lsp_client.from_server_fd == -1) {
		editorLspClientResetState();
		return;
	}

	if (graceful_shutdown && g_lsp_client.initialized && g_lsp_client.to_server_fd != -1 &&
			g_lsp_client.from_server_fd != -1) {
		int shutdown_id = g_lsp_client.next_request_id++;
		struct editorLspString shutdown = {0};
		if (editorLspStringAppendf(&shutdown,
					"{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"shutdown\",\"params\":null}",
					shutdown_id)) {
			(void)editorLspSendRawJson(shutdown.buf);
			char *response = editorLspReadFrame(g_lsp_client.from_server_fd, 500);
			free(response);
		}
		free(shutdown.buf);
		(void)editorLspSendRawJson("{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}");
	}

	if (g_lsp_client.to_server_fd != -1) {
		close(g_lsp_client.to_server_fd);
	}
	if (g_lsp_client.from_server_fd != -1) {
		close(g_lsp_client.from_server_fd);
	}

	if (g_lsp_client.pid > 0) {
		int status = 0;
		pid_t waited = waitpid(g_lsp_client.pid, &status, WNOHANG);
		if (waited == 0) {
			(void)kill(g_lsp_client.pid, SIGTERM);
			(void)waitpid(g_lsp_client.pid, &status, 0);
		}
	}

	editorLspClientResetState();
}

static int editorLspSpawnProcess(const char *command, pid_t *pid_out, int *to_server_fd_out,
		int *from_server_fd_out) {
	int stdin_pipe[2] = {-1, -1};
	int stdout_pipe[2] = {-1, -1};
	if (pipe(stdin_pipe) == -1) {
		return 0;
	}
	if (pipe(stdout_pipe) == -1) {
		close(stdin_pipe[0]);
		close(stdin_pipe[1]);
		return 0;
	}

	pid_t pid = fork();
	if (pid == -1) {
		close(stdin_pipe[0]);
		close(stdin_pipe[1]);
		close(stdout_pipe[0]);
		close(stdout_pipe[1]);
		return 0;
	}

	if (pid == 0) {
		(void)dup2(stdin_pipe[0], STDIN_FILENO);
		(void)dup2(stdout_pipe[1], STDOUT_FILENO);
		int devnull = open("/dev/null", O_WRONLY);
		if (devnull != -1) {
			(void)dup2(devnull, STDERR_FILENO);
			close(devnull);
		}

		close(stdin_pipe[0]);
		close(stdin_pipe[1]);
		close(stdout_pipe[0]);
		close(stdout_pipe[1]);

		execl("/bin/sh", "sh", "-c", command, (char *)NULL);
		_exit(127);
	}

	close(stdin_pipe[0]);
	close(stdout_pipe[1]);

	*pid_out = pid;
	*to_server_fd_out = stdin_pipe[1];
	*from_server_fd_out = stdout_pipe[0];
	return 1;
}

static char *editorLspBuildWorkspaceRootUri(void) {
	char *root_path = NULL;
	if (E.drawer_root_path != NULL && E.drawer_root_path[0] != '\0') {
		root_path = realpath(E.drawer_root_path, NULL);
	}
	if (root_path == NULL) {
		root_path = getcwd(NULL, 0);
	}
	if (root_path == NULL) {
		return NULL;
	}

	char *uri = NULL;
	if (!editorLspBuildFileUri(root_path, &uri)) {
		free(root_path);
		return NULL;
	}
	free(root_path);
	return uri;
}

static int editorLspWaitForResponseId(int request_id, int timeout_ms, char **response_out,
		int *timed_out_out) {
	if (response_out == NULL) {
		return 0;
	}
	*response_out = NULL;
	if (timed_out_out != NULL) {
		*timed_out_out = 0;
	}

	for (;;) {
		char *response = editorLspReadFrame(g_lsp_client.from_server_fd, timeout_ms);
		if (response == NULL) {
			if (timed_out_out != NULL && errno == ETIMEDOUT) {
				*timed_out_out = 1;
			}
			return 0;
		}

		int id = 0;
		if (editorLspExtractResponseId(response, &id) && id == request_id) {
			*response_out = response;
			return 1;
		}
		free(response);
	}
}

static int editorLspEnsureRunningReal(void) {
	g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_NONE;
	if (!E.lsp_enabled) {
		return 0;
	}
	if (E.lsp_gopls_command[0] == '\0') {
		editorSetStatusMsg("LSP disabled: [lsp].gopls_command is empty");
		return 0;
	}
	if (g_lsp_client.disabled_for_position_encoding) {
		return 0;
	}

	if (g_lsp_client.initialized && editorLspProcessAlive()) {
		g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_NONE;
		return 1;
	}

	editorLspClientCleanup(0);

	pid_t pid = 0;
	int to_server_fd = -1;
	int from_server_fd = -1;
	if (!editorLspSpawnProcess(E.lsp_gopls_command, &pid, &to_server_fd, &from_server_fd)) {
		g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_OTHER;
		editorSetStatusMsg("LSP startup failed: unable to launch %s", E.lsp_gopls_command);
		return 0;
	}

	editorLspClientResetState();
	g_lsp_client.pid = pid;
	g_lsp_client.to_server_fd = to_server_fd;
	g_lsp_client.from_server_fd = from_server_fd;
	g_lsp_client.next_request_id = 1;

	char *root_uri = editorLspBuildWorkspaceRootUri();
	if (root_uri == NULL) {
		editorSetStatusMsg("LSP startup failed: workspace root unavailable");
		editorLspClientCleanup(0);
		return 0;
	}

	int request_id = g_lsp_client.next_request_id++;
	struct editorLspString init = {0};
	int built = editorLspStringAppendf(&init,
			"{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"initialize\",\"params\":{"
			"\"processId\":%d,\"rootUri\":",
			request_id, (int)getpid());
	if (built) {
		built = editorLspStringAppendJsonEscaped(&init, root_uri, strlen(root_uri));
	}
	if (built) {
		built = editorLspStringAppend(&init,
				",\"capabilities\":{\"general\":{\"positionEncodings\":[\"utf-8\",\"utf-16\"]}}}}");
	}
	free(root_uri);
	if (!built) {
		g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_OTHER;
		editorSetStatusMsg("LSP startup failed: out of memory");
		free(init.buf);
		editorLspClientCleanup(0);
		return 0;
	}

	if (!editorLspSendRawJson(init.buf)) {
		g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_OTHER;
		editorSetStatusMsg("LSP startup failed: initialize write failed");
		free(init.buf);
		editorLspClientCleanup(0);
		return 0;
	}
	free(init.buf);

	char *response = NULL;
	int timed_out = 0;
	if (!editorLspWaitForResponseId(request_id, ROTIDE_LSP_IO_TIMEOUT_MS, &response, &timed_out)) {
		editorLspSetStartupFailureStatus(timed_out);
		editorLspClientCleanup(0);
		return 0;
	}
	if (editorLspResponseHasError(response)) {
		free(response);
		g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_OTHER;
		editorSetStatusMsg("LSP startup failed: initialize returned error");
		editorLspClientCleanup(0);
		return 0;
	}

	char *position_encoding = NULL;
	int have_encoding = editorLspFindStringField(response, "positionEncoding", &position_encoding);
	free(response);
	if (!have_encoding || position_encoding == NULL) {
		/* LSP default is UTF-16 when the field is omitted. */
		g_lsp_client.position_encoding_utf16 = 1;
	} else if (strcmp(position_encoding, "utf-8") == 0) {
		g_lsp_client.position_encoding_utf16 = 0;
	} else if (strcmp(position_encoding, "utf-16") == 0) {
		g_lsp_client.position_encoding_utf16 = 1;
	} else {
		free(position_encoding);
		g_lsp_client.disabled_for_position_encoding = 1;
		editorLspClientCleanup(0);
		editorSetStatusMsg("LSP disabled: unsupported position encoding");
		return 0;
	}
	free(position_encoding);

	if (!editorLspSendRawJson("{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}")) {
		g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_OTHER;
		editorSetStatusMsg("LSP startup failed: initialized notification failed");
		editorLspClientCleanup(0);
		return 0;
	}

	g_lsp_client.initialized = 1;
	g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_NONE;
	return 1;
}

static int editorLspEnsureRunning(void) {
	if (g_lsp_mock.enabled) {
		if (!E.lsp_enabled) {
			return 0;
		}
		if (!g_lsp_mock.server_alive) {
			g_lsp_mock.server_alive = 1;
			g_lsp_mock.stats.start_count++;
		}
		return 1;
	}
	return editorLspEnsureRunningReal();
}

void editorLspShutdown(void) {
	g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_NONE;
	if (g_lsp_mock.enabled) {
		if (g_lsp_mock.server_alive) {
			g_lsp_mock.stats.shutdown_count++;
			g_lsp_mock.server_alive = 0;
		}
		return;
	}
	editorLspClientCleanup(1);
}

static int editorLspIsTrackedLanguage(const char *filename, enum editorSyntaxLanguage language,
		int *doc_open_in_out, int *doc_version_in_out) {
	if (filename == NULL || filename[0] == '\0' || language != EDITOR_SYNTAX_GO || !E.lsp_enabled ||
			doc_open_in_out == NULL || doc_version_in_out == NULL) {
		return 0;
	}
	return 1;
}

int editorLspEnsureDocumentOpen(const char *filename, enum editorSyntaxLanguage language,
		int *doc_open_in_out, int *doc_version_in_out,
		const char *full_text, size_t full_text_len) {
	if (!editorLspIsTrackedLanguage(filename, language, doc_open_in_out, doc_version_in_out)) {
		return 1;
	}
	if (*doc_open_in_out) {
		return 1;
	}
	if (!editorLspEnsureRunning()) {
		return 0;
	}

	int version = *doc_version_in_out > 0 ? *doc_version_in_out : 1;
	if (g_lsp_mock.enabled) {
		g_lsp_mock.stats.did_open_count++;
		*doc_open_in_out = 1;
		*doc_version_in_out = version;
		return 1;
	}

	char *uri = NULL;
	if (!editorLspBuildFileUri(filename, &uri)) {
		return 0;
	}

	if (full_text_len > 0 && full_text == NULL) {
		free(uri);
		return 0;
	}

	struct editorLspString payload = {0};
	int built = editorLspStringAppend(&payload,
			"{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
			"\"textDocument\":{\"uri\":");
	if (built) {
		built = editorLspStringAppendJsonEscaped(&payload, uri, strlen(uri));
	}
	if (built) {
		built = editorLspStringAppendf(&payload,
				",\"languageId\":\"go\",\"version\":%d,\"text\":", version);
	}
	if (built) {
		built = editorLspStringAppendJsonEscaped(&payload,
				full_text != NULL ? full_text : "", full_text_len);
	}
	if (built) {
		built = editorLspStringAppend(&payload, "}}}");
	}
	free(uri);
	if (!built) {
		free(payload.buf);
		return 0;
	}

	int sent = editorLspSendRawJson(payload.buf);
	free(payload.buf);
	if (!sent) {
		editorLspClientCleanup(0);
		return 0;
	}

	*doc_open_in_out = 1;
	*doc_version_in_out = version;
	return 1;
}

int editorLspNotifyDidChange(const char *filename, enum editorSyntaxLanguage language,
		int *doc_open_in_out, int *doc_version_in_out,
		const struct editorSyntaxEdit *edit,
		const char *inserted_text, size_t inserted_text_len,
		const char *full_text, size_t full_text_len) {
	if (!editorLspIsTrackedLanguage(filename, language, doc_open_in_out, doc_version_in_out)) {
		return 1;
	}

	if (!editorLspEnsureDocumentOpen(filename, language, doc_open_in_out, doc_version_in_out,
				full_text, full_text_len)) {
		return 0;
	}

	int next_version = *doc_version_in_out + 1;
	if (g_lsp_mock.enabled) {
		g_lsp_mock.stats.did_change_count++;
		g_lsp_mock.last_change.had_range = edit != NULL;
		g_lsp_mock.last_change.start_line = edit != NULL ? (int)edit->start_point.row : 0;
		g_lsp_mock.last_change.start_character = edit != NULL ? (int)edit->start_point.column : 0;
		g_lsp_mock.last_change.end_line = edit != NULL ? (int)edit->old_end_point.row : 0;
		g_lsp_mock.last_change.end_character = edit != NULL ? (int)edit->old_end_point.column : 0;
		g_lsp_mock.last_change.version = next_version;
		const char *mock_text = inserted_text;
		size_t mock_text_len = inserted_text_len;
		if (edit == NULL) {
			mock_text = full_text;
			mock_text_len = full_text_len;
			if (mock_text == NULL) {
				struct editorTextSource source = {0};
				if (!editorBuildActiveTextSource(&source)) {
					return 0;
				}
				mock_text = editorTextSourceDupRange(&source, 0, source.length, &mock_text_len);
				if (mock_text == NULL && mock_text_len > 0) {
					return 0;
				}
			}
		}
		size_t copy_len = mock_text_len;
		if (copy_len >= sizeof(g_lsp_mock.last_change.text)) {
			copy_len = sizeof(g_lsp_mock.last_change.text) - 1;
		}
		if (copy_len > 0 && mock_text != NULL) {
			memcpy(g_lsp_mock.last_change.text, mock_text, copy_len);
		}
		g_lsp_mock.last_change.text[copy_len] = '\0';
		if (edit == NULL && mock_text != full_text) {
			free((char *)mock_text);
		}
		*doc_version_in_out = next_version;
		return 1;
	}

	int send_range = edit != NULL;
	if (send_range && g_lsp_client.position_encoding_utf16 &&
			(edit->start_point.row != edit->old_end_point.row ||
			 edit->start_point.column != edit->old_end_point.column)) {
		/*
		 * UTF-16 range conversion for deletes would require pre-edit text.
		 * Fall back to whole-document sync for those edits.
		 */
		send_range = 0;
	}

	char *owned_full_text = NULL;
	const char *change_text = NULL;
	size_t change_text_len = 0;
	if (send_range) {
		if (inserted_text_len > 0 && inserted_text == NULL) {
			return 0;
		}
		change_text = inserted_text != NULL ? inserted_text : "";
		change_text_len = inserted_text_len;
	} else {
		change_text = full_text;
		change_text_len = full_text_len;
		if (change_text == NULL) {
			struct editorTextSource source = {0};
			if (!editorBuildActiveTextSource(&source)) {
				return 0;
			}
			owned_full_text = editorTextSourceDupRange(&source, 0, source.length, &change_text_len);
			if (owned_full_text == NULL && change_text_len > 0) {
				return 0;
			}
			change_text = owned_full_text != NULL ? owned_full_text : "";
		}
	}

	char *uri = NULL;
	if (!editorLspBuildFileUri(filename, &uri)) {
		free(owned_full_text);
		return 0;
	}

	struct editorLspString payload = {0};
	int built = editorLspStringAppend(&payload,
			"{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
			"\"textDocument\":{\"uri\":");
	if (built) {
		built = editorLspStringAppendJsonEscaped(&payload, uri, strlen(uri));
	}
	if (built) {
		built = editorLspStringAppendf(&payload, ",\"version\":%d},\"contentChanges\":[{",
				next_version);
	}

	if (built && send_range) {
		unsigned int start_character = edit->start_point.column;
		unsigned int end_character = edit->old_end_point.column;
		if (g_lsp_client.position_encoding_utf16) {
			start_character = (unsigned int)editorLspProtocolCharacterFromBufferColumn(
					(int)edit->start_point.row, (int)edit->start_point.column);
			end_character = (unsigned int)editorLspProtocolCharacterFromBufferColumn(
					(int)edit->old_end_point.row, (int)edit->old_end_point.column);
		}

		built = editorLspStringAppendf(&payload,
				"\"range\":{\"start\":{\"line\":%u,\"character\":%u},"
				"\"end\":{\"line\":%u,\"character\":%u}},",
				edit->start_point.row, start_character,
				edit->old_end_point.row, end_character);
	}

	if (built) {
		built = editorLspStringAppend(&payload, "\"text\":");
	}
	if (built) {
		if (change_text_len > 0 && change_text == NULL) {
			built = 0;
		} else {
			built = editorLspStringAppendJsonEscaped(&payload,
					change_text != NULL ? change_text : "", change_text_len);
		}
	}
	if (built) {
		built = editorLspStringAppend(&payload, "}]}}");
	}
	free(uri);
	free(owned_full_text);
	if (!built) {
		free(payload.buf);
		return 0;
	}

	int sent = editorLspSendRawJson(payload.buf);
	free(payload.buf);
	if (!sent) {
		editorLspClientCleanup(0);
		return 0;
	}

	*doc_version_in_out = next_version;
	return 1;
}

int editorLspNotifyDidSave(const char *filename, enum editorSyntaxLanguage language,
		int *doc_open_in_out, int *doc_version_in_out) {
	if (!editorLspIsTrackedLanguage(filename, language, doc_open_in_out, doc_version_in_out)) {
		return 1;
	}
	if (!*doc_open_in_out) {
		return 1;
	}
	if (!editorLspEnsureRunning()) {
		return 0;
	}

	if (g_lsp_mock.enabled) {
		g_lsp_mock.stats.did_save_count++;
		return 1;
	}

	char *uri = NULL;
	if (!editorLspBuildFileUri(filename, &uri)) {
		return 0;
	}

	struct editorLspString payload = {0};
	int built = editorLspStringAppend(&payload,
			"{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didSave\",\"params\":{"
			"\"textDocument\":{\"uri\":");
	if (built) {
		built = editorLspStringAppendJsonEscaped(&payload, uri, strlen(uri));
	}
	if (built) {
		built = editorLspStringAppend(&payload, "}}}");
	}
	free(uri);
	if (!built) {
		free(payload.buf);
		return 0;
	}

	int sent = editorLspSendRawJson(payload.buf);
	free(payload.buf);
	if (!sent) {
		editorLspClientCleanup(0);
		return 0;
	}
	return 1;
}

void editorLspNotifyDidClose(const char *filename, enum editorSyntaxLanguage language,
		int *doc_open_in_out, int *doc_version_in_out) {
	if (!editorLspIsTrackedLanguage(filename, language, doc_open_in_out, doc_version_in_out) ||
			!*doc_open_in_out) {
		return;
	}

	if (g_lsp_mock.enabled) {
		g_lsp_mock.stats.did_close_count++;
		*doc_open_in_out = 0;
		*doc_version_in_out = 0;
		return;
	}

	if (editorLspEnsureRunning()) {
		char *uri = NULL;
		if (editorLspBuildFileUri(filename, &uri)) {
			struct editorLspString payload = {0};
			int built = editorLspStringAppend(&payload,
					"{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didClose\",\"params\":{"
					"\"textDocument\":{\"uri\":");
			if (built) {
				built = editorLspStringAppendJsonEscaped(&payload, uri, strlen(uri));
			}
			if (built) {
				built = editorLspStringAppend(&payload, "}}}");
			}
			if (built && !editorLspSendRawJson(payload.buf)) {
				editorLspClientCleanup(0);
			}
			free(payload.buf);
			free(uri);
		}
	}

	*doc_open_in_out = 0;
	*doc_version_in_out = 0;
}

static int editorLspCopyLocations(struct editorLspLocation **out_locations, int *out_count,
		const struct editorLspLocation *locations, int count) {
	*out_locations = NULL;
	*out_count = 0;
	if (count <= 0) {
		return 1;
	}

	size_t bytes = 0;
	if (!editorSizeMul(sizeof(struct editorLspLocation), (size_t)count, &bytes)) {
		return 0;
	}
	struct editorLspLocation *copy = calloc((size_t)count, sizeof(*copy));
	if (copy == NULL) {
		return 0;
	}

	for (int i = 0; i < count; i++) {
		copy[i].line = locations[i].line;
		copy[i].character = locations[i].character;
		if (locations[i].path != NULL) {
			copy[i].path = strdup(locations[i].path);
			if (copy[i].path == NULL) {
				editorLspFreeLocations(copy, count);
				return 0;
			}
		}
	}

	*out_locations = copy;
	*out_count = count;
	return 1;
}

int editorLspRequestDefinition(const char *filename, int line, int character,
		struct editorLspLocation **locations_out, int *count_out, int *timed_out_out) {
	if (locations_out == NULL || count_out == NULL) {
		return -1;
	}
	*locations_out = NULL;
	*count_out = 0;
	if (timed_out_out != NULL) {
		*timed_out_out = 0;
	}

	if (!E.lsp_enabled) {
		return 0;
	}
	if (filename == NULL || filename[0] == '\0' || line < 0 || character < 0) {
		return -1;
	}

	if (g_lsp_mock.enabled) {
		if (!editorLspEnsureRunning()) {
			return -1;
		}
		g_lsp_mock.stats.definition_count++;
		if (g_lsp_mock.definition_result_code == -2) {
			if (timed_out_out != NULL) {
				*timed_out_out = 1;
			}
			return -2;
		}
		if (g_lsp_mock.definition_result_code < 0) {
			return -1;
		}
		if (!editorLspCopyLocations(locations_out, count_out, g_lsp_mock.definition_locations,
					g_lsp_mock.definition_location_count)) {
			return -1;
		}
		return 1;
	}

	if (!editorLspEnsureRunning()) {
		return -1;
	}

	char *uri = NULL;
	if (!editorLspBuildFileUri(filename, &uri)) {
		return -1;
	}

	int protocol_character = editorLspProtocolCharacterFromBufferColumn(line, character);

	int request_id = g_lsp_client.next_request_id++;
	struct editorLspString payload = {0};
	int built = editorLspStringAppendf(&payload,
			"{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"textDocument/definition\",\"params\":{"
			"\"textDocument\":{\"uri\":",
			request_id);
	if (built) {
		built = editorLspStringAppendJsonEscaped(&payload, uri, strlen(uri));
	}
	if (built) {
		built = editorLspStringAppendf(&payload,
				"},\"position\":{\"line\":%d,\"character\":%d}}}", line, protocol_character);
	}
	free(uri);
	if (!built) {
		free(payload.buf);
		return -1;
	}

	if (!editorLspSendRawJson(payload.buf)) {
		free(payload.buf);
		editorLspClientCleanup(0);
		return -1;
	}
	free(payload.buf);

	char *response = NULL;
	int timed_out = 0;
	if (!editorLspWaitForResponseId(request_id, ROTIDE_LSP_IO_TIMEOUT_MS, &response, &timed_out)) {
		editorLspClientCleanup(0);
		if (timed_out) {
			if (timed_out_out != NULL) {
				*timed_out_out = 1;
			}
			return -2;
		}
		return -1;
	}

	if (editorLspResponseHasError(response)) {
		free(response);
		return -1;
	}

	struct editorLspLocation *locations = NULL;
	int count = 0;
	if (!editorLspParseDefinitionLocations(response, &locations, &count)) {
		free(response);
		return -1;
	}
	free(response);

	*locations_out = locations;
	*count_out = count;
	return 1;
}

enum editorLspStartupFailureReason editorLspLastStartupFailureReason(void) {
	return g_lsp_last_startup_failure_reason;
}

void editorLspTestSetMockEnabled(int enabled) {
	g_lsp_mock.enabled = enabled ? 1 : 0;
	if (!g_lsp_mock.enabled) {
		g_lsp_mock.server_alive = 0;
	}
}

void editorLspTestSetMockServerAlive(int alive) {
	g_lsp_mock.server_alive = alive ? 1 : 0;
}

void editorLspTestResetMock(void) {
	editorLspFreeLocations(g_lsp_mock.definition_locations, g_lsp_mock.definition_location_count);
	g_lsp_mock.definition_locations = NULL;
	g_lsp_mock.definition_location_count = 0;
	g_lsp_mock.definition_result_code = 1;
	g_lsp_mock.server_alive = 0;
	memset(&g_lsp_mock.stats, 0, sizeof(g_lsp_mock.stats));
	memset(&g_lsp_mock.last_change, 0, sizeof(g_lsp_mock.last_change));
	g_lsp_mock.enabled = 0;
	g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_NONE;
}

void editorLspTestGetStats(struct editorLspTestStats *out) {
	if (out == NULL) {
		return;
	}
	*out = g_lsp_mock.stats;
}

void editorLspTestGetLastChange(struct editorLspTestLastChange *out) {
	if (out == NULL) {
		return;
	}
	*out = g_lsp_mock.last_change;
}

void editorLspTestSetMockDefinitionResponse(int result_code,
		const struct editorLspLocation *locations, int count) {
	editorLspFreeLocations(g_lsp_mock.definition_locations, g_lsp_mock.definition_location_count);
	g_lsp_mock.definition_locations = NULL;
	g_lsp_mock.definition_location_count = 0;
	g_lsp_mock.definition_result_code = result_code;

	if (locations == NULL || count <= 0) {
		return;
	}
	(void)editorLspCopyLocations(&g_lsp_mock.definition_locations,
			&g_lsp_mock.definition_location_count, locations, count);
}
