#include "language/lsp.h"

#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "support/file_io.h"
#include "support/size_utils.h"
#include "text/utf8.h"

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

enum editorLspServerKind {
	EDITOR_LSP_SERVER_NONE = 0,
	EDITOR_LSP_SERVER_GOPLS,
	EDITOR_LSP_SERVER_CLANGD,
	EDITOR_LSP_SERVER_HTML,
	EDITOR_LSP_SERVER_CSS,
	EDITOR_LSP_SERVER_JSON,
	EDITOR_LSP_SERVER_JAVASCRIPT,
	EDITOR_LSP_SERVER_ESLINT
};

struct editorLspClient {
	pid_t pid;
	int to_server_fd;
	int from_server_fd;
	int initialized;
	enum editorLspServerKind server_kind;
	enum editorLspServerKind disabled_for_position_encoding_server_kind;
	int next_request_id;
	int position_encoding_utf16;
	char *workspace_root_path;
};

struct editorLspString {
	char *buf;
	size_t len;
	size_t cap;
};

struct editorLspPendingEdit {
	int start_line;
	int start_character;
	int end_line;
	int end_character;
	char *new_text;
};

struct editorLspMockState {
	int enabled;
	int primary_server_alive;
	enum editorLspServerKind primary_server_kind;
	char *primary_workspace_root_path;
	int eslint_server_alive;
	char *eslint_workspace_root_path;
	char last_did_open_language_id[32];
	struct editorLspTestStats stats;
	struct editorLspTestLastChange last_change;
	int definition_result_code;
	struct editorLspLocation *definition_locations;
	int definition_location_count;
	struct editorLspDiagnostic *diagnostics;
	int diagnostic_count;
	char *diagnostic_path;
	int code_action_result_code;
	struct editorLspPendingEdit *code_action_edits;
	int code_action_edit_count;
};

static struct editorLspClient g_lsp_client = {
	.pid = 0,
	.to_server_fd = -1,
	.from_server_fd = -1,
	.initialized = 0,
	.server_kind = EDITOR_LSP_SERVER_NONE,
	.disabled_for_position_encoding_server_kind = EDITOR_LSP_SERVER_NONE,
	.next_request_id = 1,
	.position_encoding_utf16 = 0,
	.workspace_root_path = NULL,
};
static struct editorLspClient g_lsp_eslint_client = {
	.pid = 0,
	.to_server_fd = -1,
	.from_server_fd = -1,
	.initialized = 0,
	.server_kind = EDITOR_LSP_SERVER_NONE,
	.disabled_for_position_encoding_server_kind = EDITOR_LSP_SERVER_NONE,
	.next_request_id = 1,
	.position_encoding_utf16 = 0,
	.workspace_root_path = NULL,
};
static struct editorLspMockState g_lsp_mock = {0};
static enum editorLspStartupFailureReason g_lsp_last_startup_failure_reason =
		EDITOR_LSP_STARTUP_FAILURE_NONE;

static int editorLspUtf8ColumnToUtf16Units(const char *text, size_t text_len, int byte_column);
static int editorLspUtf16UnitsToUtf8Column(const char *text, size_t text_len, int utf16_units);
static int editorLspReadActiveLineText(int line, char **text_out, size_t *len_out);
static int editorLspProtocolCharacterFromBufferColumn(int line, int byte_column);
static int editorLspClientProtocolCharacterFromBufferColumn(struct editorLspClient *client, int line,
		int byte_column);
static int editorLspClientProtocolCharacterToBufferColumn(struct editorLspClient *client, int line,
		int protocol_character);
static void editorLspResetTrackedDocumentsForServerKind(enum editorLspServerKind server_kind);
static int editorLspFilenameHasHtmlExtension(const char *filename);
static int editorLspFilenameHasCssExtension(const char *filename);
static int editorLspFilenameHasJsonExtension(const char *filename);
static int editorLspFilenameHasJavascriptExtension(const char *filename);
static enum editorLspServerKind editorLspServerKindForFile(const char *filename,
		enum editorSyntaxLanguage language);
static const char *editorLspCommandForServerKind(enum editorLspServerKind server_kind);
static const char *editorLspServerNameForServerKind(enum editorLspServerKind server_kind);
static const char *editorLspCommandSettingNameForServerKind(
		enum editorLspServerKind server_kind);
static const char *editorLspLanguageLabelForServerKind(enum editorLspServerKind server_kind);
static int editorLspServerKindSupportsDefinition(enum editorLspServerKind server_kind);
static const char *editorLspLanguageIdForFile(const char *filename,
		enum editorSyntaxLanguage language);
static char *editorLspBuildWorkspaceRootPathForFile(const char *filename,
		enum editorLspServerKind server_kind);
static int editorLspWorkspaceRootsMatch(const char *left, const char *right);
static int editorLspProcessIncomingMessage(struct editorLspClient *client, const char *message);
static int editorLspTryDrainIncoming(struct editorLspClient *client, int timeout_ms);
static void editorLspFreeDiagnostics(struct editorLspDiagnostic *diagnostics, int count);
static int editorLspCopyDiagnostics(struct editorLspDiagnostic **out_diagnostics, int *out_count,
		const struct editorLspDiagnostic *diagnostics, int count);
static void editorLspSetDiagnosticsForPath(const char *path,
		const struct editorLspDiagnostic *diagnostics, int count);
static int editorLspApplyPendingEdits(const struct editorLspPendingEdit *edits, int count);
static void editorLspFreePendingEdits(struct editorLspPendingEdit *edits, int count);

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

static char *editorLspBuildInitializeRequestJson(int request_id, const char *root_uri,
		int process_id) {
	if (root_uri == NULL || root_uri[0] == '\0') {
		return NULL;
	}

	struct editorLspString init = {0};
	int built = editorLspStringAppendf(&init,
			"{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"initialize\",\"params\":{"
			"\"processId\":%d,\"rootUri\":",
			request_id, process_id);
	if (built) {
		built = editorLspStringAppendJsonEscaped(&init, root_uri, strlen(root_uri));
	}
	if (built) {
		built = editorLspStringAppend(&init,
				",\"capabilities\":{\"general\":{\"positionEncodings\":[\"utf-8\",\"utf-16\"]},"
				"\"workspace\":{\"applyEdit\":true},"
				"\"textDocument\":{\"codeAction\":{\"codeActionLiteralSupport\":{"
				"\"codeActionKind\":{\"valueSet\":[\"quickfix\",\"source.fixAll\","
				"\"source.fixAll.eslint\"]}}}}}}}");
	}
	if (!built) {
		free(init.buf);
		return NULL;
	}
	return init.buf;
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

static int editorLspSendRawJsonToFd(int fd, const char *json) {
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

static int editorLspSendRawJson(const char *json) {
	return editorLspSendRawJsonToFd(g_lsp_client.to_server_fd, json);
}

static int editorLspBuildFileUri(const char *path, char **uri_out) {
	if (path == NULL || uri_out == NULL) {
		return 0;
	}
	*uri_out = NULL;

	char *absolute_path = editorPathAbsoluteDup(path);
	if (absolute_path == NULL) {
		return 0;
	}

	struct editorLspString sb = {0};
	if (!editorLspStringAppend(&sb, "file://")) {
		free(absolute_path);
		free(sb.buf);
		return 0;
	}

	for (const unsigned char *p = (const unsigned char *)absolute_path; *p != '\0'; p++) {
		unsigned char ch = *p;
		int unreserved = isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~' || ch == '/';
		if (unreserved) {
			if (!editorLspStringAppendBytes(&sb, (const char *)&ch, 1)) {
				free(absolute_path);
				free(sb.buf);
				return 0;
			}
		} else {
			if (!editorLspStringAppendf(&sb, "%%%02X", (unsigned int)ch)) {
				free(absolute_path);
				free(sb.buf);
				return 0;
			}
		}
	}

	free(absolute_path);
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

static void editorLspResetTrackedDocumentsForServerKind(enum editorLspServerKind server_kind) {
	int is_eslint = server_kind == EDITOR_LSP_SERVER_ESLINT;
	if (is_eslint) {
		E.lsp_eslint_doc_open = 0;
		E.lsp_eslint_doc_version = 0;
	} else {
		E.lsp_doc_open = 0;
		E.lsp_doc_version = 0;
	}
	if (E.tabs == NULL) {
		return;
	}
	for (int i = 0; i < E.tab_count; i++) {
		if (is_eslint) {
			E.tabs[i].lsp_eslint_doc_open = 0;
			E.tabs[i].lsp_eslint_doc_version = 0;
		} else {
			E.tabs[i].lsp_doc_open = 0;
			E.tabs[i].lsp_doc_version = 0;
		}
	}
}

static int editorLspFilenameHasCppExtension(const char *filename) {
	if (filename == NULL || filename[0] == '\0') {
		return 0;
	}
	const char *dot = strrchr(filename, '.');
	if (dot == NULL) {
		return 0;
	}
	return strcmp(dot, ".cc") == 0 || strcmp(dot, ".cpp") == 0 || strcmp(dot, ".cxx") == 0 ||
			strcmp(dot, ".c++") == 0 || strcmp(dot, ".hh") == 0 || strcmp(dot, ".hpp") == 0 ||
			strcmp(dot, ".hxx") == 0;
}

static int editorLspFilenameHasHtmlExtension(const char *filename) {
	if (filename == NULL || filename[0] == '\0') {
		return 0;
	}
	const char *dot = strrchr(filename, '.');
	if (dot == NULL) {
		return 0;
	}
	return strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0 || strcmp(dot, ".xhtml") == 0;
}

static int editorLspFilenameHasCssExtension(const char *filename) {
	if (filename == NULL || filename[0] == '\0') {
		return 0;
	}
	const char *dot = strrchr(filename, '.');
	if (dot == NULL) {
		return 0;
	}
	return strcmp(dot, ".css") == 0 || strcmp(dot, ".scss") == 0;
}

static int editorLspFilenameHasJsonExtension(const char *filename) {
	if (filename == NULL || filename[0] == '\0') {
		return 0;
	}
	const char *dot = strrchr(filename, '.');
	if (dot == NULL) {
		return 0;
	}
	return strcmp(dot, ".json") == 0;
}

static int editorLspFilenameHasJavascriptExtension(const char *filename) {
	if (filename == NULL || filename[0] == '\0') {
		return 0;
	}
	const char *dot = strrchr(filename, '.');
	if (dot == NULL) {
		return 0;
	}
	return strcmp(dot, ".js") == 0 || strcmp(dot, ".mjs") == 0 || strcmp(dot, ".cjs") == 0 ||
			strcmp(dot, ".jsx") == 0;
}

static enum editorLspServerKind editorLspServerKindForFile(const char *filename,
		enum editorSyntaxLanguage language) {
	if (editorLspFilenameHasHtmlExtension(filename)) {
		return EDITOR_LSP_SERVER_HTML;
	}
	if (editorLspFilenameHasCssExtension(filename)) {
		return EDITOR_LSP_SERVER_CSS;
	}
	if (editorLspFilenameHasJsonExtension(filename)) {
		return EDITOR_LSP_SERVER_JSON;
	}
	if (editorLspFilenameHasJavascriptExtension(filename)) {
		return EDITOR_LSP_SERVER_JAVASCRIPT;
	}
	switch (language) {
		case EDITOR_SYNTAX_GO:
			return EDITOR_LSP_SERVER_GOPLS;
		case EDITOR_SYNTAX_C:
			return EDITOR_LSP_SERVER_CLANGD;
		case EDITOR_SYNTAX_HTML:
			return EDITOR_LSP_SERVER_HTML;
		case EDITOR_SYNTAX_CSS:
			return EDITOR_LSP_SERVER_CSS;
		case EDITOR_SYNTAX_JAVASCRIPT:
			return EDITOR_LSP_SERVER_JAVASCRIPT;
		default:
			return EDITOR_LSP_SERVER_NONE;
	}
}

static const char *editorLspCommandForServerKind(enum editorLspServerKind server_kind) {
	switch (server_kind) {
		case EDITOR_LSP_SERVER_GOPLS:
			return E.lsp_gopls_command;
		case EDITOR_LSP_SERVER_CLANGD:
			return E.lsp_clangd_command;
		case EDITOR_LSP_SERVER_HTML:
			return E.lsp_html_command;
		case EDITOR_LSP_SERVER_CSS:
			return E.lsp_css_command;
		case EDITOR_LSP_SERVER_JSON:
			return E.lsp_json_command;
		case EDITOR_LSP_SERVER_JAVASCRIPT:
			return E.lsp_javascript_command;
		case EDITOR_LSP_SERVER_ESLINT:
			return E.lsp_eslint_command;
		default:
			return NULL;
	}
}

static const char *editorLspServerNameForServerKind(enum editorLspServerKind server_kind) {
	switch (server_kind) {
		case EDITOR_LSP_SERVER_GOPLS:
			return "gopls";
		case EDITOR_LSP_SERVER_CLANGD:
			return "clangd";
		case EDITOR_LSP_SERVER_HTML:
			return "vscode-html-language-server";
		case EDITOR_LSP_SERVER_CSS:
			return "vscode-css-language-server";
		case EDITOR_LSP_SERVER_JSON:
			return "vscode-json-language-server";
		case EDITOR_LSP_SERVER_JAVASCRIPT:
			return "typescript-language-server";
		case EDITOR_LSP_SERVER_ESLINT:
			return "vscode-eslint-language-server";
		default:
			return "LSP";
	}
}

static const char *editorLspCommandSettingNameForServerKind(
		enum editorLspServerKind server_kind) {
	switch (server_kind) {
		case EDITOR_LSP_SERVER_GOPLS:
			return "gopls_command";
		case EDITOR_LSP_SERVER_CLANGD:
			return "clangd_command";
		case EDITOR_LSP_SERVER_HTML:
			return "html_command";
		case EDITOR_LSP_SERVER_CSS:
			return "css_command";
		case EDITOR_LSP_SERVER_JSON:
			return "json_command";
		case EDITOR_LSP_SERVER_JAVASCRIPT:
			return "javascript_command";
		case EDITOR_LSP_SERVER_ESLINT:
			return "eslint_command";
		default:
			return NULL;
	}
}

static const char *editorLspLanguageLabelForServerKind(enum editorLspServerKind server_kind) {
	switch (server_kind) {
		case EDITOR_LSP_SERVER_GOPLS:
			return "Go";
		case EDITOR_LSP_SERVER_CLANGD:
			return "C/C++";
		case EDITOR_LSP_SERVER_HTML:
			return "HTML";
		case EDITOR_LSP_SERVER_CSS:
			return "CSS/SCSS";
		case EDITOR_LSP_SERVER_JSON:
			return "JSON";
		case EDITOR_LSP_SERVER_JAVASCRIPT:
			return "JavaScript";
		case EDITOR_LSP_SERVER_ESLINT:
			return "JavaScript";
		default:
			return NULL;
	}
}

static int editorLspServerKindEnabled(enum editorLspServerKind server_kind) {
	switch (server_kind) {
		case EDITOR_LSP_SERVER_GOPLS:
			return E.lsp_gopls_enabled;
		case EDITOR_LSP_SERVER_CLANGD:
			return E.lsp_clangd_enabled;
		case EDITOR_LSP_SERVER_HTML:
			return E.lsp_html_enabled;
		case EDITOR_LSP_SERVER_CSS:
			return E.lsp_css_enabled;
		case EDITOR_LSP_SERVER_JSON:
			return E.lsp_json_enabled;
		case EDITOR_LSP_SERVER_JAVASCRIPT:
			return E.lsp_javascript_enabled;
		case EDITOR_LSP_SERVER_ESLINT:
			return E.lsp_eslint_enabled;
		default:
			return 0;
	}
}

int editorLspFileEnabled(const char *filename, enum editorSyntaxLanguage language) {
	return editorLspServerKindEnabled(editorLspServerKindForFile(filename, language));
}

int editorLspFileUsesEslint(const char *filename, enum editorSyntaxLanguage language) {
	return editorLspFilenameHasJavascriptExtension(filename) || language == EDITOR_SYNTAX_JAVASCRIPT;
}

int editorLspEslintEnabledForFile(const char *filename, enum editorSyntaxLanguage language) {
	return editorLspFileUsesEslint(filename, language) && E.lsp_eslint_enabled;
}

static int editorLspServerKindSupportsDefinition(enum editorLspServerKind server_kind) {
	return server_kind == EDITOR_LSP_SERVER_GOPLS || server_kind == EDITOR_LSP_SERVER_CLANGD ||
			server_kind == EDITOR_LSP_SERVER_HTML || server_kind == EDITOR_LSP_SERVER_CSS ||
			server_kind == EDITOR_LSP_SERVER_JSON ||
			server_kind == EDITOR_LSP_SERVER_JAVASCRIPT;
}

int editorLspFileSupportsDefinition(const char *filename, enum editorSyntaxLanguage language) {
	return editorLspServerKindSupportsDefinition(editorLspServerKindForFile(filename, language));
}

static const char *editorLspLanguageIdForFile(const char *filename,
		enum editorSyntaxLanguage language) {
	switch (language) {
		case EDITOR_SYNTAX_GO:
			return "go";
		case EDITOR_SYNTAX_C:
			return editorLspFilenameHasCppExtension(filename) ? "cpp" : "c";
		case EDITOR_SYNTAX_HTML:
			return "html";
		case EDITOR_SYNTAX_CSS:
			return editorLspFilenameHasCssExtension(filename) &&
						 strrchr(filename, '.') != NULL &&
						 strcmp(strrchr(filename, '.'), ".scss") == 0 ?
					"scss" :
					"css";
		case EDITOR_SYNTAX_JAVASCRIPT:
			return editorLspFilenameHasJavascriptExtension(filename) &&
						 strrchr(filename, '.') != NULL &&
						 strcmp(strrchr(filename, '.'), ".jsx") == 0 ?
					"javascriptreact" :
					"javascript";
		default:
			break;
	}
	if (editorLspFilenameHasHtmlExtension(filename)) {
		return "html";
	}
	if (editorLspFilenameHasCssExtension(filename)) {
		return strrchr(filename, '.') != NULL && strcmp(strrchr(filename, '.'), ".scss") == 0 ?
				"scss" :
				"css";
	}
	if (editorLspFilenameHasJsonExtension(filename)) {
		return "json";
	}
	if (editorLspFilenameHasJavascriptExtension(filename)) {
		return strrchr(filename, '.') != NULL && strcmp(strrchr(filename, '.'), ".jsx") == 0 ?
				"javascriptreact" :
				"javascript";
	}
	return NULL;
}

const char *editorLspLanguageLabelForFile(const char *filename,
		enum editorSyntaxLanguage language) {
	return editorLspLanguageLabelForServerKind(editorLspServerKindForFile(filename, language));
}

const char *editorLspServerNameForFile(const char *filename, enum editorSyntaxLanguage language) {
	return editorLspServerNameForServerKind(editorLspServerKindForFile(filename, language));
}

const char *editorLspCommandForFile(const char *filename, enum editorSyntaxLanguage language) {
	return editorLspCommandForServerKind(editorLspServerKindForFile(filename, language));
}

const char *editorLspCommandSettingNameForFile(const char *filename,
		enum editorSyntaxLanguage language) {
	return editorLspCommandSettingNameForServerKind(
			editorLspServerKindForFile(filename, language));
}

int editorLspUsesSharedVscodeInstallPrompt(const char *filename,
		enum editorSyntaxLanguage language) {
	enum editorLspServerKind server_kind = editorLspServerKindForFile(filename, language);
	return server_kind == EDITOR_LSP_SERVER_HTML || server_kind == EDITOR_LSP_SERVER_CSS ||
			server_kind == EDITOR_LSP_SERVER_JSON || server_kind == EDITOR_LSP_SERVER_ESLINT;
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

static const char *editorLspFindJsonObjectEnd(const char *object_start) {
	if (object_start == NULL || object_start[0] != '{') {
		return NULL;
	}

	int depth = 0;
	int in_string = 0;
	int escaped = 0;
	for (const char *scan = object_start; scan[0] != '\0'; scan++) {
		char ch = scan[0];
		if (in_string) {
			if (escaped) {
				escaped = 0;
			} else if (ch == '\\') {
				escaped = 1;
			} else if (ch == '"') {
				in_string = 0;
			}
			continue;
		}

		if (ch == '"') {
			in_string = 1;
			continue;
		}
		if (ch == '{') {
			depth++;
			continue;
		}
		if (ch == '}') {
			depth--;
			if (depth == 0) {
				return scan + 1;
			}
			if (depth < 0) {
				return NULL;
			}
		}
	}

	return NULL;
}

static const char *editorLspFindJsonArrayEnd(const char *array_start) {
	if (array_start == NULL || array_start[0] != '[') {
		return NULL;
	}

	int depth = 0;
	int in_string = 0;
	int escaped = 0;
	for (const char *scan = array_start; scan[0] != '\0'; scan++) {
		char ch = scan[0];
		if (in_string) {
			if (escaped) {
				escaped = 0;
			} else if (ch == '\\') {
				escaped = 1;
			} else if (ch == '"') {
				in_string = 0;
			}
			continue;
		}

		if (ch == '"') {
			in_string = 1;
			continue;
		}
		if (ch == '[') {
			depth++;
			continue;
		}
		if (ch == ']') {
			depth--;
			if (depth == 0) {
				return scan + 1;
			}
			if (depth < 0) {
				return NULL;
			}
		}
	}

	return NULL;
}

static int editorLspParsePositionFromKey(const char *range_json, const char *key_name,
		const char *limit, int *line_out, int *character_out) {
	char key_pattern[32];
	int written = snprintf(key_pattern, sizeof(key_pattern), "\"%s\"", key_name);
	if (written <= 0 || (size_t)written >= sizeof(key_pattern)) {
		return 0;
	}
	const char *start = editorLspStrstrBounded(range_json, key_pattern, limit);
	if (start == NULL) {
		return 0;
	}

	const char *start_colon = strchr(start, ':');
	if (start_colon == NULL || (limit != NULL && start_colon >= limit)) {
		return 0;
	}
	const char *start_object = strchr(start_colon + 1, '{');
	if (start_object == NULL || (limit != NULL && start_object >= limit)) {
		return 0;
	}
	const char *start_end = editorLspFindJsonObjectEnd(start_object);
	if (start_end == NULL || (limit != NULL && start_end > limit)) {
		return 0;
	}

	const char *line_key = editorLspStrstrBounded(start_object, "\"line\"", start_end);
	if (line_key == NULL) {
		return 0;
	}
	const char *line_colon = strchr(line_key, ':');
	if (line_colon == NULL || line_colon >= start_end) {
		return 0;
	}
	int line = 0;
	if (!editorLspParseJsonInt(line_colon + 1, &line, NULL) || line < 0) {
		return 0;
	}

	const char *char_key = editorLspStrstrBounded(start_object, "\"character\"", start_end);
	if (char_key == NULL) {
		return 0;
	}
	const char *char_colon = strchr(char_key, ':');
	if (char_colon == NULL || char_colon >= start_end) {
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

static int editorLspParsePositionFromStart(const char *range_json, const char *limit,
		int *line_out, int *character_out) {
	return editorLspParsePositionFromKey(range_json, "start", limit, line_out, character_out);
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

static int editorLspClientProtocolCharacterToBufferColumn(struct editorLspClient *client, int line,
		int protocol_character) {
	if (protocol_character < 0) {
		return 0;
	}
	if (client == NULL || !client->position_encoding_utf16) {
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
		const char *object_start = strchr(scan, '{');
		if (object_start == NULL) {
			break;
		}
		const char *object_end = editorLspFindJsonObjectEnd(object_start);
		if (object_end == NULL) {
			editorLspFreeLocations(locations, count);
			return 0;
		}
		scan = object_end;

		const char *uri_key_pos = editorLspStrstrBounded(object_start, key_pattern, object_end);
		if (uri_key_pos == NULL) {
			continue;
		}

		const char *uri_colon = strchr(uri_key_pos, ':');
		if (uri_colon == NULL || uri_colon >= object_end) {
			continue;
		}
		const char *uri_value = editorLspSkipWs(uri_colon + 1);
		if (uri_value == NULL || uri_value[0] != '"') {
			continue;
		}

		char *uri = NULL;
		const char *after_uri = NULL;
		if (!editorLspParseJsonString(uri_value, &uri, &after_uri)) {
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

		const char *range_pos = editorLspStrstrBounded(object_start, primary_pattern, object_end);
		if (range_pos == NULL && range_key_fallback != NULL) {
			char fallback_pattern[64];
			int fallback_written = snprintf(fallback_pattern, sizeof(fallback_pattern), "\"%s\"",
					range_key_fallback);
			if (fallback_written <= 0 || (size_t)fallback_written >= sizeof(fallback_pattern)) {
				free(uri);
				editorLspFreeLocations(locations, count);
				return 0;
			}
			range_pos = editorLspStrstrBounded(object_start, fallback_pattern, object_end);
		}

		int line = -1;
		int character = -1;
		if (range_pos != NULL) {
			(void)editorLspParsePositionFromStart(range_pos, object_end, &line, &character);
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

static void editorLspFreeDiagnostics(struct editorLspDiagnostic *diagnostics, int count) {
	if (diagnostics == NULL) {
		return;
	}
	for (int i = 0; i < count; i++) {
		free(diagnostics[i].message);
	}
	free(diagnostics);
}

static int editorLspCopyDiagnostics(struct editorLspDiagnostic **out_diagnostics, int *out_count,
		const struct editorLspDiagnostic *diagnostics, int count) {
	if (out_diagnostics == NULL || out_count == NULL) {
		return 0;
	}
	*out_diagnostics = NULL;
	*out_count = 0;
	if (diagnostics == NULL || count <= 0) {
		return 1;
	}

	size_t bytes = 0;
	if (!editorSizeMul(sizeof(*diagnostics), (size_t)count, &bytes)) {
		return 0;
	}
	struct editorLspDiagnostic *copy = calloc((size_t)count, sizeof(*copy));
	if (copy == NULL) {
		return 0;
	}

	for (int i = 0; i < count; i++) {
		copy[i].start_line = diagnostics[i].start_line;
		copy[i].start_character = diagnostics[i].start_character;
		copy[i].end_line = diagnostics[i].end_line;
		copy[i].end_character = diagnostics[i].end_character;
		copy[i].severity = diagnostics[i].severity;
		if (diagnostics[i].message != NULL) {
			copy[i].message = strdup(diagnostics[i].message);
			if (copy[i].message == NULL) {
				editorLspFreeDiagnostics(copy, count);
				return 0;
			}
		}
	}

	*out_diagnostics = copy;
	*out_count = count;
	return 1;
}

static int editorLspDiagnosticsErrorCount(const struct editorLspDiagnostic *diagnostics, int count) {
	int errors = 0;
	for (int i = 0; i < count; i++) {
		if (diagnostics[i].severity == 1) {
			errors++;
		}
	}
	return errors;
}

static int editorLspDiagnosticsWarningCount(const struct editorLspDiagnostic *diagnostics, int count) {
	int warnings = 0;
	for (int i = 0; i < count; i++) {
		if (diagnostics[i].severity == 2) {
			warnings++;
		}
	}
	return warnings;
}

static int editorLspPathMatches(const char *left, const char *right) {
	return left != NULL && right != NULL && editorPathsReferToSameFile(left, right);
}

static void editorLspUpdateDiagnosticFields(struct editorLspDiagnostic **diagnostics_in_out,
		int *count_in_out, int *error_count_out, int *warning_count_out,
		const struct editorLspDiagnostic *diagnostics, int count) {
	editorLspFreeDiagnostics(*diagnostics_in_out, *count_in_out);
	*diagnostics_in_out = NULL;
	*count_in_out = 0;
	*error_count_out = 0;
	*warning_count_out = 0;
	if (diagnostics == NULL || count <= 0) {
		return;
	}
	if (!editorLspCopyDiagnostics(diagnostics_in_out, count_in_out, diagnostics, count)) {
		return;
	}
	*error_count_out = editorLspDiagnosticsErrorCount(*diagnostics_in_out, *count_in_out);
	*warning_count_out = editorLspDiagnosticsWarningCount(*diagnostics_in_out, *count_in_out);
}

static void editorLspSetDiagnosticsForPath(const char *path,
		const struct editorLspDiagnostic *diagnostics, int count) {
	if (path == NULL || path[0] == '\0') {
		return;
	}

	int active_matches = editorLspPathMatches(path, E.filename);
	int old_count = active_matches ? E.lsp_diagnostic_count : 0;
	int old_errors = active_matches ? E.lsp_diagnostic_error_count : 0;
	int old_warnings = active_matches ? E.lsp_diagnostic_warning_count : 0;

	if (active_matches) {
		editorLspUpdateDiagnosticFields(&E.lsp_diagnostics, &E.lsp_diagnostic_count,
				&E.lsp_diagnostic_error_count, &E.lsp_diagnostic_warning_count,
				diagnostics, count);
	}
	for (int i = 0; i < E.tab_count; i++) {
		if (!editorLspPathMatches(path, E.tabs[i].filename)) {
			continue;
		}
		editorLspUpdateDiagnosticFields(&E.tabs[i].lsp_diagnostics,
				&E.tabs[i].lsp_diagnostic_count, &E.tabs[i].lsp_diagnostic_error_count,
				&E.tabs[i].lsp_diagnostic_warning_count, diagnostics, count);
	}

	if (active_matches &&
			(old_count != E.lsp_diagnostic_count ||
			 old_errors != E.lsp_diagnostic_error_count ||
			 old_warnings != E.lsp_diagnostic_warning_count)) {
		if (E.lsp_diagnostic_count == 0) {
			editorSetStatusMsg("ESLint: diagnostics cleared");
		} else {
			editorSetStatusMsg("ESLint: %d error%s, %d warning%s",
					E.lsp_diagnostic_error_count,
					E.lsp_diagnostic_error_count == 1 ? "" : "s",
					E.lsp_diagnostic_warning_count,
					E.lsp_diagnostic_warning_count == 1 ? "" : "s");
		}
	}
}

void editorLspClearDiagnosticsForFile(const char *filename) {
	editorLspSetDiagnosticsForPath(filename, NULL, 0);
}

void editorLspGetDiagnosticSummaryForFile(const char *filename,
		struct editorLspDiagnosticSummary *summary_out) {
	if (summary_out == NULL) {
		return;
	}
	memset(summary_out, 0, sizeof(*summary_out));
	if (editorLspPathMatches(filename, E.filename)) {
		summary_out->count = E.lsp_diagnostic_count;
		summary_out->error_count = E.lsp_diagnostic_error_count;
		summary_out->warning_count = E.lsp_diagnostic_warning_count;
		return;
	}
	for (int i = 0; i < E.tab_count; i++) {
		if (!editorLspPathMatches(filename, E.tabs[i].filename)) {
			continue;
		}
		summary_out->count = E.tabs[i].lsp_diagnostic_count;
		summary_out->error_count = E.tabs[i].lsp_diagnostic_error_count;
		summary_out->warning_count = E.tabs[i].lsp_diagnostic_warning_count;
		return;
	}
}

static int editorLspParseDiagnosticsMessage(const char *message, char **path_out,
		struct editorLspDiagnostic **diagnostics_out, int *count_out) {
	if (path_out == NULL || diagnostics_out == NULL || count_out == NULL) {
		return 0;
	}
	*path_out = NULL;
	*diagnostics_out = NULL;
	*count_out = 0;

	char *method = NULL;
	if (!editorLspFindStringField(message, "method", &method)) {
		return 0;
	}
	int is_publish = strcmp(method, "textDocument/publishDiagnostics") == 0;
	free(method);
	if (!is_publish) {
		return 0;
	}

	const char *params_key = strstr(message, "\"params\"");
	if (params_key == NULL) {
		return 0;
	}
	const char *params_colon = strchr(params_key, ':');
	if (params_colon == NULL) {
		return 0;
	}
	const char *params_object = strchr(params_colon + 1, '{');
	if (params_object == NULL) {
		return 0;
	}
	const char *params_end = editorLspFindJsonObjectEnd(params_object);
	if (params_end == NULL) {
		return 0;
	}

	char *uri = NULL;
	if (!editorLspFindStringField(params_object, "uri", &uri) || uri == NULL) {
		free(uri);
		return 0;
	}
	char *path = editorLspDecodeFileUri(uri);
	free(uri);
	if (path == NULL) {
		return 0;
	}

	const char *diag_key = editorLspStrstrBounded(params_object, "\"diagnostics\"", params_end);
	if (diag_key == NULL) {
		free(path);
		return 0;
	}
	const char *diag_colon = strchr(diag_key, ':');
	if (diag_colon == NULL || diag_colon >= params_end) {
		free(path);
		return 0;
	}
	const char *diag_array = strchr(diag_colon + 1, '[');
	if (diag_array == NULL || diag_array >= params_end) {
		free(path);
		return 0;
	}
	const char *diag_array_end = editorLspFindJsonArrayEnd(diag_array);
	if (diag_array_end == NULL || diag_array_end > params_end) {
		free(path);
		return 0;
	}

	struct editorLspDiagnostic *diagnostics = NULL;
	int count = 0;
	int cap = 0;
	const char *scan = diag_array + 1;
	while (scan < diag_array_end) {
		const char *object_start = strchr(scan, '{');
		if (object_start == NULL || object_start >= diag_array_end) {
			break;
		}
		const char *object_end = editorLspFindJsonObjectEnd(object_start);
		if (object_end == NULL || object_end > diag_array_end) {
			editorLspFreeDiagnostics(diagnostics, count);
			free(path);
			return 0;
		}
		scan = object_end;

		const char *range_key = editorLspStrstrBounded(object_start, "\"range\"", object_end);
		if (range_key == NULL) {
			continue;
		}
		const char *range_colon = strchr(range_key, ':');
		if (range_colon == NULL || range_colon >= object_end) {
			continue;
		}
		const char *range_object = strchr(range_colon + 1, '{');
		if (range_object == NULL || range_object >= object_end) {
			continue;
		}
		const char *range_end = editorLspFindJsonObjectEnd(range_object);
		if (range_end == NULL || range_end > object_end) {
			continue;
		}

		int start_line = 0;
		int start_character = 0;
		int end_line = 0;
		int end_character = 0;
		if (!editorLspParsePositionFromKey(range_object, "start", range_end, &start_line,
					&start_character) ||
				!editorLspParsePositionFromKey(range_object, "end", range_end, &end_line,
						&end_character)) {
			continue;
		}

		int severity = 1;
		const char *severity_key =
				editorLspStrstrBounded(object_start, "\"severity\"", object_end);
		if (severity_key != NULL) {
			const char *severity_colon = strchr(severity_key, ':');
			int parsed_severity = 0;
			if (severity_colon != NULL &&
					editorLspParseJsonInt(severity_colon + 1, &parsed_severity, NULL)) {
				severity = parsed_severity;
			}
		}

		char *msg = NULL;
		if (!editorLspFindStringField(object_start, "message", &msg) || msg == NULL) {
			msg = strdup("");
			if (msg == NULL) {
				editorLspFreeDiagnostics(diagnostics, count);
				free(path);
				return 0;
			}
		}

		if (count >= cap) {
			int new_cap = cap > 0 ? cap * 2 : 4;
			size_t bytes = 0;
			if (!editorSizeMul(sizeof(*diagnostics), (size_t)new_cap, &bytes)) {
				free(msg);
				editorLspFreeDiagnostics(diagnostics, count);
				free(path);
				return 0;
			}
			struct editorLspDiagnostic *grown = realloc(diagnostics, bytes);
			if (grown == NULL) {
				free(msg);
				editorLspFreeDiagnostics(diagnostics, count);
				free(path);
				return 0;
			}
			diagnostics = grown;
			cap = new_cap;
		}
		diagnostics[count].start_line = start_line;
		diagnostics[count].start_character = start_character;
		diagnostics[count].end_line = end_line;
		diagnostics[count].end_character = end_character;
		diagnostics[count].severity = severity;
		diagnostics[count].message = msg;
		count++;
	}

	*path_out = path;
	*diagnostics_out = diagnostics;
	*count_out = count;
	return 1;
}

static void editorLspFreePendingEdits(struct editorLspPendingEdit *edits, int count) {
	if (edits == NULL) {
		return;
	}
	for (int i = 0; i < count; i++) {
		free(edits[i].new_text);
	}
	free(edits);
}

static int editorLspPendingEditCompareDesc(const void *lhs, const void *rhs) {
	const struct editorLspPendingEdit *left = lhs;
	const struct editorLspPendingEdit *right = rhs;
	if (left->start_line != right->start_line) {
		return right->start_line - left->start_line;
	}
	return right->start_character - left->start_character;
}

static int editorLspApplyPendingEditsWithClient(struct editorLspClient *client,
		const struct editorLspPendingEdit *edits, int count) {
	if (edits == NULL || count <= 0) {
		return 0;
	}

	struct editorLspPendingEdit *sorted = calloc((size_t)count, sizeof(*sorted));
	if (sorted == NULL) {
		return -1;
	}
	for (int i = 0; i < count; i++) {
		sorted[i] = edits[i];
	}
	qsort(sorted, (size_t)count, sizeof(*sorted), editorLspPendingEditCompareDesc);

	for (int i = 0; i < count; i++) {
		int start_cx = editorLspClientProtocolCharacterToBufferColumn(client,
				sorted[i].start_line, sorted[i].start_character);
		int end_cx = editorLspClientProtocolCharacterToBufferColumn(client,
				sorted[i].end_line, sorted[i].end_character);
		size_t start_offset = 0;
		size_t end_offset = 0;
		if (!editorBufferPosToOffset(sorted[i].start_line, start_cx, &start_offset) ||
				!editorBufferPosToOffset(sorted[i].end_line, end_cx, &end_offset) ||
				end_offset < start_offset) {
			free(sorted);
			return -1;
		}

		size_t cursor_before = E.cursor_offset;
		size_t cursor_after = cursor_before;
		size_t new_len = sorted[i].new_text != NULL ? strlen(sorted[i].new_text) : 0;
		size_t old_len = end_offset - start_offset;
		if (cursor_before > start_offset) {
			if (cursor_before <= end_offset) {
				cursor_after = start_offset + new_len;
			} else {
				cursor_after = start_offset + new_len + (cursor_before - end_offset);
			}
		}

		struct editorDocumentEdit edit = {
			.kind = old_len > 0 ? EDITOR_EDIT_DELETE_TEXT : EDITOR_EDIT_INSERT_TEXT,
			.start_offset = start_offset,
			.old_len = old_len,
			.new_text = sorted[i].new_text != NULL ? sorted[i].new_text : "",
			.new_len = new_len,
			.before_cursor_offset = cursor_before,
			.after_cursor_offset = cursor_after,
			.before_dirty = E.dirty,
			.after_dirty = E.dirty + 1,
		};
		if (!editorApplyDocumentEdit(&edit)) {
			free(sorted);
			return -1;
		}
	}

	free(sorted);
	return count;
}

static int editorLspApplyPendingEdits(const struct editorLspPendingEdit *edits, int count) {
	return editorLspApplyPendingEditsWithClient(&g_lsp_client, edits, count);
}

static int editorLspParseWorkspaceEditChanges(const char *edit_json, const char *target_path,
		struct editorLspPendingEdit **edits_out, int *count_out) {
	if (edits_out == NULL || count_out == NULL) {
		return 0;
	}
	*edits_out = NULL;
	*count_out = 0;

	const char *changes_key = strstr(edit_json, "\"changes\"");
	if (changes_key == NULL) {
		return 1;
	}
	const char *changes_colon = strchr(changes_key, ':');
	if (changes_colon == NULL) {
		return 0;
	}
	const char *changes_object = strchr(changes_colon + 1, '{');
	if (changes_object == NULL) {
		return 0;
	}
	const char *changes_end = editorLspFindJsonObjectEnd(changes_object);
	if (changes_end == NULL) {
		return 0;
	}

	const char *scan = changes_object + 1;
	while (scan < changes_end) {
		const char *key = strchr(scan, '"');
		if (key == NULL || key >= changes_end) {
			break;
		}
		char *uri = NULL;
		const char *after_key = NULL;
		if (!editorLspParseJsonString(key, &uri, &after_key) || uri == NULL) {
			return 0;
		}
		const char *colon = strchr(after_key, ':');
		if (colon == NULL || colon >= changes_end) {
			free(uri);
			return 0;
		}
		const char *array_start = strchr(colon + 1, '[');
		if (array_start == NULL || array_start >= changes_end) {
			free(uri);
			return 0;
		}
		const char *array_end = editorLspFindJsonArrayEnd(array_start);
		if (array_end == NULL || array_end > changes_end) {
			free(uri);
			return 0;
		}

		char *path = editorLspDecodeFileUri(uri);
		free(uri);
		if (path != NULL && editorLspPathMatches(path, target_path)) {
			struct editorLspPendingEdit *edits = NULL;
			int count = 0;
			int cap = 0;
			const char *item_scan = array_start + 1;
			while (item_scan < array_end) {
				const char *object_start = strchr(item_scan, '{');
				if (object_start == NULL || object_start >= array_end) {
					break;
				}
				const char *object_end = editorLspFindJsonObjectEnd(object_start);
				if (object_end == NULL || object_end > array_end) {
					editorLspFreePendingEdits(edits, count);
					free(path);
					return 0;
				}
				item_scan = object_end;

				const char *range_key =
						editorLspStrstrBounded(object_start, "\"range\"", object_end);
				const char *range_colon = range_key != NULL ? strchr(range_key, ':') : NULL;
				const char *range_object =
						range_colon != NULL ? strchr(range_colon + 1, '{') : NULL;
				const char *range_end =
						range_object != NULL ? editorLspFindJsonObjectEnd(range_object) : NULL;
				char *new_text = NULL;
				if (!editorLspFindStringField(object_start, "newText", &new_text) ||
						new_text == NULL) {
					new_text = strdup("");
				}
				if (new_text == NULL) {
					editorLspFreePendingEdits(edits, count);
					free(path);
					return 0;
				}

				int start_line = 0;
				int start_character = 0;
				int end_line = 0;
				int end_character = 0;
				if (range_end != NULL &&
						editorLspParsePositionFromKey(range_object, "start", range_end,
								&start_line, &start_character) &&
						editorLspParsePositionFromKey(range_object, "end", range_end,
								&end_line, &end_character)) {
					if (count >= cap) {
						int new_cap = cap > 0 ? cap * 2 : 4;
						size_t bytes = 0;
						if (!editorSizeMul(sizeof(*edits), (size_t)new_cap, &bytes)) {
							free(new_text);
							editorLspFreePendingEdits(edits, count);
							free(path);
							return 0;
						}
						struct editorLspPendingEdit *grown = realloc(edits, bytes);
						if (grown == NULL) {
							free(new_text);
							editorLspFreePendingEdits(edits, count);
							free(path);
							return 0;
						}
						edits = grown;
						cap = new_cap;
					}
					edits[count].start_line = start_line;
					edits[count].start_character = start_character;
					edits[count].end_line = end_line;
					edits[count].end_character = end_character;
					edits[count].new_text = new_text;
					count++;
				} else {
					free(new_text);
				}
			}
			free(path);
			*edits_out = edits;
			*count_out = count;
			return 1;
		}
		free(path);
		scan = array_end;
	}

	return 1;
}

static int editorLspProcessAlive(struct editorLspClient *client) {
	if (client == NULL || client->pid <= 0) {
		return 0;
	}

	int status = 0;
	pid_t waited = waitpid(client->pid, &status, WNOHANG);
	if (waited == 0) {
		return 1;
	}
	if (waited == -1 && errno == EINTR) {
		return 1;
	}
	return 0;
}

static int editorLspRespondToRequest(struct editorLspClient *client, int request_id,
		const char *result_json) {
	struct editorLspString payload = {0};
	int built = editorLspStringAppendf(&payload,
			"{\"jsonrpc\":\"2.0\",\"id\":%d,\"result\":", request_id);
	if (built) {
		built = editorLspStringAppend(&payload, result_json != NULL ? result_json : "null");
	}
	if (built) {
		built = editorLspStringAppend(&payload, "}");
	}
	if (!built) {
		free(payload.buf);
		return 0;
	}
	int sent = editorLspSendRawJsonToFd(client != NULL ? client->to_server_fd : -1, payload.buf);
	free(payload.buf);
	return sent;
}

static int editorLspProcessIncomingMessage(struct editorLspClient *client, const char *message) {
	if (message == NULL || message[0] == '\0') {
		return 1;
	}

	char *path = NULL;
	struct editorLspDiagnostic *diagnostics = NULL;
	int diagnostic_count = 0;
	if (editorLspParseDiagnosticsMessage(message, &path, &diagnostics, &diagnostic_count)) {
		editorLspSetDiagnosticsForPath(path, diagnostics, diagnostic_count);
		editorLspFreeDiagnostics(diagnostics, diagnostic_count);
		free(path);
		return 1;
	}

	char *method = NULL;
	if (!editorLspFindStringField(message, "method", &method) || method == NULL) {
		free(method);
		return 1;
	}

	int request_id = 0;
	int has_request_id = editorLspExtractResponseId(message, &request_id);
	if (strcmp(method, "workspace/configuration") == 0) {
		free(method);
		if (has_request_id) {
			return editorLspRespondToRequest(client, request_id, "[{}]");
		}
		return 1;
	}
	if (strcmp(method, "client/registerCapability") == 0) {
		free(method);
		if (has_request_id) {
			return editorLspRespondToRequest(client, request_id, "null");
		}
		return 1;
	}
	if (strcmp(method, "workspace/applyEdit") == 0) {
		free(method);
		if (!has_request_id) {
			return 1;
		}
		struct editorLspPendingEdit *edits = NULL;
		int count = 0;
		int parsed = editorLspParseWorkspaceEditChanges(message, E.filename, &edits, &count);
		int applied =
				parsed && count > 0 && editorLspApplyPendingEditsWithClient(client, edits, count) >= 0;
		editorLspFreePendingEdits(edits, count);
		return editorLspRespondToRequest(client, request_id, applied ? "{\"applied\":true}" :
				"{\"applied\":false}");
	}

	free(method);
	if (has_request_id) {
		return editorLspRespondToRequest(client, request_id, "null");
	}
	return 1;
}

static int editorLspTryDrainIncoming(struct editorLspClient *client, int timeout_ms) {
	if (g_lsp_mock.enabled || client == NULL || client->from_server_fd == -1) {
		return 1;
	}

	int wait_ms = timeout_ms;
	for (;;) {
		struct pollfd pfd = {
			.fd = client->from_server_fd,
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
			return 1;
		}
		if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
			return 0;
		}

		char *message = editorLspReadFrame(client->from_server_fd, ROTIDE_LSP_IO_TIMEOUT_MS);
		if (message == NULL) {
			return 0;
		}
		int processed = editorLspProcessIncomingMessage(client, message);
		free(message);
		if (!processed) {
			return 0;
		}
		wait_ms = 0;
	}
}

static int editorLspTryGetProcessExitCode(struct editorLspClient *client, int *exit_code_out) {
	if (client == NULL || exit_code_out == NULL || client->pid <= 0) {
		return 0;
	}

	int status = 0;
	pid_t waited = waitpid(client->pid, &status, WNOHANG);
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

static int editorLspTryGetProcessExitCodeWithWait(struct editorLspClient *client, int timeout_ms,
		int *exit_code_out) {
	long long deadline_ms = editorLspMonotonicMillis() + (long long)timeout_ms;

	for (;;) {
		if (editorLspTryGetProcessExitCode(client, exit_code_out)) {
			return 1;
		}
		if (client == NULL || client->pid <= 0) {
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

static void editorLspSetStartupFailureStatus(struct editorLspClient *client, int timed_out) {
	g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_OTHER;
	const char *command = editorLspCommandForServerKind(client != NULL ? client->server_kind :
			EDITOR_LSP_SERVER_NONE);
	if (command == NULL || command[0] == '\0') {
		command = "language server";
	}
	if (timed_out) {
		editorSetStatusMsg("LSP startup failed: initialize timed out");
		return;
	}

	int saved_errno = errno;
	if (saved_errno == EPIPE) {
		int exit_code = 0;
		if (editorLspTryGetProcessExitCodeWithWait(client, 100, &exit_code) && exit_code == 127) {
			g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_COMMAND_NOT_FOUND;
			editorSetStatusMsg("LSP startup failed: command not found (%s)", command);
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

static int editorLspClientProtocolCharacterFromBufferColumn(struct editorLspClient *client, int line,
		int byte_column) {
	if (client == NULL || byte_column < 0) {
		byte_column = 0;
	}
	if (client == NULL || !client->position_encoding_utf16) {
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

static void editorLspClientResetState(struct editorLspClient *client) {
	if (client == NULL) {
		return;
	}
	client->pid = 0;
	client->to_server_fd = -1;
	client->from_server_fd = -1;
	client->initialized = 0;
	client->server_kind = EDITOR_LSP_SERVER_NONE;
	client->next_request_id = 1;
	client->position_encoding_utf16 = 0;
	client->workspace_root_path = NULL;
}

static void editorLspClientCleanup(struct editorLspClient *client, int graceful_shutdown) {
	if (client == NULL) {
		return;
	}
	if (client->pid <= 0 && client->to_server_fd == -1 && client->from_server_fd == -1) {
		editorLspClientResetState(client);
		return;
	}

	if (graceful_shutdown && client->initialized && client->to_server_fd != -1 &&
			client->from_server_fd != -1) {
		int shutdown_id = client->next_request_id++;
		struct editorLspString shutdown = {0};
		if (editorLspStringAppendf(&shutdown,
					"{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"shutdown\",\"params\":null}",
					shutdown_id)) {
			(void)editorLspSendRawJsonToFd(client->to_server_fd, shutdown.buf);
			char *response = editorLspReadFrame(client->from_server_fd, 500);
			free(response);
		}
		free(shutdown.buf);
		(void)editorLspSendRawJsonToFd(client->to_server_fd,
				"{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}");
	}

	if (client->to_server_fd != -1) {
		close(client->to_server_fd);
	}
	if (client->from_server_fd != -1) {
		close(client->from_server_fd);
	}

	if (client->pid > 0) {
		int status = 0;
		pid_t waited = waitpid(client->pid, &status, WNOHANG);
		if (waited == 0) {
			(void)kill(client->pid, SIGTERM);
			(void)waitpid(client->pid, &status, 0);
		}
	}

	free(client->workspace_root_path);
	editorLspClientResetState(client);
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

static int editorLspWorkspaceRootsMatch(const char *left, const char *right) {
	if (left == NULL || right == NULL) {
		return 0;
	}
	return editorPathsReferToSameFile(left, right);
}

static char *editorLspBuildWorkspaceRootPathForFile(const char *filename,
		enum editorLspServerKind server_kind) {
	static const char *const gopls_markers[] = {
		"go.work",
		"go.mod",
		".rotide.toml",
		".git"
	};
	static const char *const clangd_markers[] = {
		"compile_commands.json",
		"compile_flags.txt",
		".clangd",
		".rotide.toml",
		".git"
	};
	static const char *const html_markers[] = {
		"package.json",
		".rotide.toml",
		".git"
	};
	static const char *const javascript_markers[] = {
		"tsconfig.json",
		"jsconfig.json",
		"package.json",
		".rotide.toml",
		".git"
	};

	char *file_path = editorPathAbsoluteDup(filename);
	char *file_dir = NULL;
	char *workspace_root = NULL;

	if (file_path != NULL) {
		file_dir = editorPathDirnameDup(file_path);
		if (file_dir == NULL) {
			free(file_path);
			return NULL;
		}

		const char *const *markers = NULL;
		size_t marker_count = 0;
		if (server_kind == EDITOR_LSP_SERVER_GOPLS) {
			markers = gopls_markers;
			marker_count = sizeof(gopls_markers) / sizeof(gopls_markers[0]);
	} else if (server_kind == EDITOR_LSP_SERVER_CLANGD) {
		markers = clangd_markers;
		marker_count = sizeof(clangd_markers) / sizeof(clangd_markers[0]);
	} else if (server_kind == EDITOR_LSP_SERVER_HTML ||
			server_kind == EDITOR_LSP_SERVER_CSS ||
			server_kind == EDITOR_LSP_SERVER_JSON ||
			server_kind == EDITOR_LSP_SERVER_ESLINT) {
		markers = html_markers;
		marker_count = sizeof(html_markers) / sizeof(html_markers[0]);
	} else if (server_kind == EDITOR_LSP_SERVER_JAVASCRIPT) {
		markers = javascript_markers;
		marker_count = sizeof(javascript_markers) / sizeof(javascript_markers[0]);
	}

		if (markers != NULL) {
			workspace_root = editorPathFindMarkerUpward(file_dir, markers, marker_count);
		}
		if (workspace_root == NULL) {
			workspace_root = strdup(file_dir);
		}
		free(file_dir);
		free(file_path);
		return workspace_root;
	}

	if (E.drawer_root_path != NULL && E.drawer_root_path[0] != '\0') {
		return editorPathAbsoluteDup(E.drawer_root_path);
	}

	return editorPathGetCwd();
}

static int editorLspWaitForResponseId(struct editorLspClient *client, int request_id, int timeout_ms,
		char **response_out, int *timed_out_out) {
	if (response_out == NULL) {
		return 0;
	}
	*response_out = NULL;
	if (timed_out_out != NULL) {
		*timed_out_out = 0;
	}

	for (;;) {
		char *response =
				editorLspReadFrame(client != NULL ? client->from_server_fd : -1, timeout_ms);
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
		(void)editorLspProcessIncomingMessage(client, response);
		free(response);
	}
}

static int editorLspEnsureRunningReal(struct editorLspClient *client, const char *filename,
		enum editorLspServerKind server_kind) {
	g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_NONE;
	if (client == NULL) {
		return 0;
	}
	if (!editorLspServerKindEnabled(server_kind)) {
		return 0;
	}
	const char *command = editorLspCommandForServerKind(server_kind);
	if (command == NULL || command[0] == '\0') {
		const char *setting_name = editorLspCommandSettingNameForServerKind(server_kind);
		if (setting_name != NULL) {
			editorSetStatusMsg("LSP disabled: [lsp].%s is empty", setting_name);
		}
		return 0;
	}
	if (client->disabled_for_position_encoding_server_kind == server_kind) {
		return 0;
	}

	char *workspace_root_path = editorLspBuildWorkspaceRootPathForFile(filename, server_kind);
	if (workspace_root_path == NULL) {
		g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_OTHER;
		editorSetStatusMsg("LSP startup failed: workspace root unavailable");
		return 0;
	}

	if (client->initialized && client->server_kind == server_kind &&
			editorLspProcessAlive(client) &&
			editorLspWorkspaceRootsMatch(client->workspace_root_path, workspace_root_path)) {
		free(workspace_root_path);
		g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_NONE;
		return 1;
	}
	if (client->server_kind != EDITOR_LSP_SERVER_NONE &&
			(client->server_kind != server_kind || !editorLspProcessAlive(client) ||
			 !editorLspWorkspaceRootsMatch(client->workspace_root_path, workspace_root_path))) {
		editorLspResetTrackedDocumentsForServerKind(server_kind);
	}

	editorLspClientCleanup(client, 0);

	pid_t pid = 0;
	int to_server_fd = -1;
	int from_server_fd = -1;
	if (!editorLspSpawnProcess(command, &pid, &to_server_fd, &from_server_fd)) {
		free(workspace_root_path);
		g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_OTHER;
		editorSetStatusMsg("LSP startup failed: unable to launch %s", command);
		return 0;
	}

	editorLspClientResetState(client);
	client->pid = pid;
	client->to_server_fd = to_server_fd;
	client->from_server_fd = from_server_fd;
	client->server_kind = server_kind;
	client->next_request_id = 1;
	client->workspace_root_path = workspace_root_path;

	char *root_uri = NULL;
	if (client->workspace_root_path != NULL) {
		(void)editorLspBuildFileUri(client->workspace_root_path, &root_uri);
	}
	if (root_uri == NULL) {
		editorSetStatusMsg("LSP startup failed: workspace root unavailable");
		editorLspClientCleanup(client, 0);
		return 0;
	}

	int request_id = client->next_request_id++;
	char *init = editorLspBuildInitializeRequestJson(request_id, root_uri, (int)getpid());
	free(root_uri);
	if (init == NULL) {
		g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_OTHER;
		editorSetStatusMsg("LSP startup failed: out of memory");
		editorLspClientCleanup(client, 0);
		return 0;
	}

	if (!editorLspSendRawJsonToFd(client->to_server_fd, init)) {
		g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_OTHER;
		editorSetStatusMsg("LSP startup failed: initialize write failed");
		free(init);
		editorLspClientCleanup(client, 0);
		return 0;
	}
	free(init);

	char *response = NULL;
	int timed_out = 0;
	if (!editorLspWaitForResponseId(client, request_id, ROTIDE_LSP_IO_TIMEOUT_MS, &response,
				&timed_out)) {
		editorLspSetStartupFailureStatus(client, timed_out);
		editorLspClientCleanup(client, 0);
		return 0;
	}
	if (editorLspResponseHasError(response)) {
		free(response);
		g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_OTHER;
		editorSetStatusMsg("LSP startup failed: initialize returned error");
		editorLspClientCleanup(client, 0);
		return 0;
	}

	char *position_encoding = NULL;
	int have_encoding = editorLspFindStringField(response, "positionEncoding", &position_encoding);
	free(response);
	if (!have_encoding || position_encoding == NULL) {
		/* LSP default is UTF-16 when the field is omitted. */
		client->position_encoding_utf16 = 1;
	} else if (strcmp(position_encoding, "utf-8") == 0) {
		client->position_encoding_utf16 = 0;
	} else if (strcmp(position_encoding, "utf-16") == 0) {
		client->position_encoding_utf16 = 1;
	} else {
		free(position_encoding);
		client->disabled_for_position_encoding_server_kind = server_kind;
		editorLspClientCleanup(client, 0);
		editorSetStatusMsg("LSP disabled: unsupported position encoding");
		return 0;
	}
	free(position_encoding);

	if (!editorLspSendRawJsonToFd(client->to_server_fd,
				"{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}")) {
		g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_OTHER;
		editorSetStatusMsg("LSP startup failed: initialized notification failed");
		editorLspClientCleanup(client, 0);
		return 0;
	}

	client->initialized = 1;
	g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_NONE;
	return 1;
}

static int editorLspEnsureRunningForFile(const char *filename, enum editorSyntaxLanguage language) {
	enum editorLspServerKind server_kind = editorLspServerKindForFile(filename, language);
	if (server_kind == EDITOR_LSP_SERVER_NONE) {
		return 0;
	}
	if (g_lsp_mock.enabled) {
		char *workspace_root_path = editorLspBuildWorkspaceRootPathForFile(filename, server_kind);
		if (workspace_root_path == NULL) {
			return 0;
		}
		if (!editorLspServerKindEnabled(server_kind)) {
			free(workspace_root_path);
			return 0;
		}
		if (!g_lsp_mock.primary_server_alive || g_lsp_mock.primary_server_kind != server_kind ||
				!editorLspWorkspaceRootsMatch(g_lsp_mock.primary_workspace_root_path,
						workspace_root_path)) {
			if (g_lsp_mock.primary_server_kind != EDITOR_LSP_SERVER_NONE &&
					(!g_lsp_mock.primary_server_alive ||
					 g_lsp_mock.primary_server_kind != server_kind ||
					 !editorLspWorkspaceRootsMatch(g_lsp_mock.primary_workspace_root_path,
							 workspace_root_path))) {
				editorLspResetTrackedDocumentsForServerKind(server_kind);
			}
			g_lsp_mock.primary_server_alive = 1;
			g_lsp_mock.primary_server_kind = server_kind;
			free(g_lsp_mock.primary_workspace_root_path);
			g_lsp_mock.primary_workspace_root_path = workspace_root_path;
			g_lsp_mock.stats.start_count++;
		} else {
			free(workspace_root_path);
		}
		return 1;
	}
	return editorLspEnsureRunningReal(&g_lsp_client, filename, server_kind);
}

static int editorLspEnsureRunningEslintForFile(const char *filename,
		enum editorSyntaxLanguage language) {
	if (!editorLspEslintEnabledForFile(filename, language)) {
		return 0;
	}
	if (g_lsp_mock.enabled) {
		char *workspace_root_path =
				editorLspBuildWorkspaceRootPathForFile(filename, EDITOR_LSP_SERVER_ESLINT);
		if (workspace_root_path == NULL) {
			return 0;
		}
		if (!g_lsp_mock.eslint_server_alive ||
				!editorLspWorkspaceRootsMatch(g_lsp_mock.eslint_workspace_root_path,
						workspace_root_path)) {
			if (g_lsp_mock.eslint_server_alive &&
					!editorLspWorkspaceRootsMatch(g_lsp_mock.eslint_workspace_root_path,
							workspace_root_path)) {
				editorLspResetTrackedDocumentsForServerKind(EDITOR_LSP_SERVER_ESLINT);
			}
			g_lsp_mock.eslint_server_alive = 1;
			free(g_lsp_mock.eslint_workspace_root_path);
			g_lsp_mock.eslint_workspace_root_path = workspace_root_path;
			g_lsp_mock.stats.start_count++;
		} else {
			free(workspace_root_path);
		}
		return 1;
	}
	return editorLspEnsureRunningReal(&g_lsp_eslint_client, filename, EDITOR_LSP_SERVER_ESLINT);
}

void editorLspShutdown(void) {
	g_lsp_last_startup_failure_reason = EDITOR_LSP_STARTUP_FAILURE_NONE;
	if (g_lsp_mock.enabled) {
		if (g_lsp_mock.primary_server_alive) {
			g_lsp_mock.stats.shutdown_count++;
			g_lsp_mock.primary_server_alive = 0;
			g_lsp_mock.primary_server_kind = EDITOR_LSP_SERVER_NONE;
		}
		if (g_lsp_mock.eslint_server_alive) {
			g_lsp_mock.stats.shutdown_count++;
			g_lsp_mock.eslint_server_alive = 0;
		}
		free(g_lsp_mock.primary_workspace_root_path);
		g_lsp_mock.primary_workspace_root_path = NULL;
		free(g_lsp_mock.eslint_workspace_root_path);
		g_lsp_mock.eslint_workspace_root_path = NULL;
		return;
	}
	editorLspClientCleanup(&g_lsp_client, 1);
	editorLspClientCleanup(&g_lsp_eslint_client, 1);
}

void editorLspPumpNotifications(void) {
	if (g_lsp_mock.enabled) {
		if (g_lsp_mock.diagnostic_path != NULL) {
			editorLspSetDiagnosticsForPath(g_lsp_mock.diagnostic_path, g_lsp_mock.diagnostics,
					g_lsp_mock.diagnostic_count);
		}
		return;
	}
	(void)editorLspTryDrainIncoming(&g_lsp_client, 0);
	(void)editorLspTryDrainIncoming(&g_lsp_eslint_client, 0);
}

static int editorLspIsTrackedLanguage(const char *filename, enum editorSyntaxLanguage language,
		int *doc_open_in_out, int *doc_version_in_out) {
	if (filename == NULL || filename[0] == '\0' || !editorLspFileEnabled(filename, language) ||
			doc_open_in_out == NULL || doc_version_in_out == NULL ||
			editorLspServerKindForFile(filename, language) == EDITOR_LSP_SERVER_NONE) {
		return 0;
	}
	return 1;
}

static int editorLspIsTrackedEslintLanguage(const char *filename,
		enum editorSyntaxLanguage language, int *doc_open_in_out, int *doc_version_in_out) {
	if (filename == NULL || filename[0] == '\0' ||
			!editorLspEslintEnabledForFile(filename, language) ||
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
	if (!editorLspEnsureRunningForFile(filename, language)) {
		return 0;
	}

	const char *language_id = editorLspLanguageIdForFile(filename, language);
	if (language_id == NULL) {
		return 0;
	}

	int version = *doc_version_in_out > 0 ? *doc_version_in_out : 1;
	if (g_lsp_mock.enabled) {
		(void)snprintf(g_lsp_mock.last_did_open_language_id,
				sizeof(g_lsp_mock.last_did_open_language_id), "%s", language_id);
		g_lsp_mock.last_did_open_language_id[
				sizeof(g_lsp_mock.last_did_open_language_id) - 1] = '\0';
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
				",\"languageId\":\"%s\",\"version\":%d,\"text\":", language_id, version);
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
		editorLspClientCleanup(&g_lsp_client, 0);
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
		editorLspClientCleanup(&g_lsp_client, 0);
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
	if (!editorLspEnsureRunningForFile(filename, language)) {
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
		editorLspClientCleanup(&g_lsp_client, 0);
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
		editorLspClearDiagnosticsForFile(filename);
		return;
	}

	if (editorLspEnsureRunningForFile(filename, language)) {
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
				editorLspClientCleanup(&g_lsp_client, 0);
			}
			free(payload.buf);
			free(uri);
		}
	}

	*doc_open_in_out = 0;
	*doc_version_in_out = 0;
	editorLspClearDiagnosticsForFile(filename);
}

int editorLspEnsureEslintDocumentOpen(const char *filename, enum editorSyntaxLanguage language,
		int *doc_open_in_out, int *doc_version_in_out,
		const char *full_text, size_t full_text_len) {
	if (!editorLspIsTrackedEslintLanguage(filename, language, doc_open_in_out, doc_version_in_out)) {
		return 1;
	}
	if (*doc_open_in_out) {
		return 1;
	}
	if (!editorLspEnsureRunningEslintForFile(filename, language)) {
		return 0;
	}

	const char *language_id = editorLspLanguageIdForFile(filename, language);
	if (language_id == NULL) {
		return 0;
	}

	int version = *doc_version_in_out > 0 ? *doc_version_in_out : 1;
	if (g_lsp_mock.enabled) {
		(void)snprintf(g_lsp_mock.last_did_open_language_id,
				sizeof(g_lsp_mock.last_did_open_language_id), "%s", language_id);
		g_lsp_mock.last_did_open_language_id[
				sizeof(g_lsp_mock.last_did_open_language_id) - 1] = '\0';
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
				",\"languageId\":\"%s\",\"version\":%d,\"text\":", language_id, version);
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

	int sent = editorLspSendRawJsonToFd(g_lsp_eslint_client.to_server_fd, payload.buf);
	free(payload.buf);
	if (!sent) {
		editorLspClientCleanup(&g_lsp_eslint_client, 0);
		return 0;
	}

	*doc_open_in_out = 1;
	*doc_version_in_out = version;
	return 1;
}

int editorLspNotifyEslintDidChange(const char *filename, enum editorSyntaxLanguage language,
		int *doc_open_in_out, int *doc_version_in_out,
		const struct editorSyntaxEdit *edit,
		const char *inserted_text, size_t inserted_text_len,
		const char *full_text, size_t full_text_len) {
	if (!editorLspIsTrackedEslintLanguage(filename, language, doc_open_in_out, doc_version_in_out)) {
		return 1;
	}

	if (!editorLspEnsureEslintDocumentOpen(filename, language, doc_open_in_out, doc_version_in_out,
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
		*doc_version_in_out = next_version;
		return 1;
	}

	int send_range = edit != NULL;
	if (send_range && g_lsp_eslint_client.position_encoding_utf16 &&
			(edit->start_point.row != edit->old_end_point.row ||
			 edit->start_point.column != edit->old_end_point.column)) {
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
		if (g_lsp_eslint_client.position_encoding_utf16) {
			start_character = (unsigned int)editorLspClientProtocolCharacterFromBufferColumn(
					&g_lsp_eslint_client, (int)edit->start_point.row, (int)edit->start_point.column);
			end_character = (unsigned int)editorLspClientProtocolCharacterFromBufferColumn(
					&g_lsp_eslint_client, (int)edit->old_end_point.row,
					(int)edit->old_end_point.column);
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

	int sent = editorLspSendRawJsonToFd(g_lsp_eslint_client.to_server_fd, payload.buf);
	free(payload.buf);
	if (!sent) {
		editorLspClientCleanup(&g_lsp_eslint_client, 0);
		return 0;
	}

	*doc_version_in_out = next_version;
	return 1;
}

int editorLspNotifyEslintDidSave(const char *filename, enum editorSyntaxLanguage language,
		int *doc_open_in_out, int *doc_version_in_out) {
	if (!editorLspIsTrackedEslintLanguage(filename, language, doc_open_in_out, doc_version_in_out)) {
		return 1;
	}
	if (!*doc_open_in_out) {
		return 1;
	}
	if (!editorLspEnsureRunningEslintForFile(filename, language)) {
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

	int sent = editorLspSendRawJsonToFd(g_lsp_eslint_client.to_server_fd, payload.buf);
	free(payload.buf);
	if (!sent) {
		editorLspClientCleanup(&g_lsp_eslint_client, 0);
		return 0;
	}
	return 1;
}

void editorLspNotifyEslintDidClose(const char *filename, enum editorSyntaxLanguage language,
		int *doc_open_in_out, int *doc_version_in_out) {
	if (!editorLspIsTrackedEslintLanguage(filename, language, doc_open_in_out, doc_version_in_out) ||
			!*doc_open_in_out) {
		return;
	}

	if (g_lsp_mock.enabled) {
		g_lsp_mock.stats.did_close_count++;
		*doc_open_in_out = 0;
		*doc_version_in_out = 0;
		editorLspClearDiagnosticsForFile(filename);
		return;
	}

	if (editorLspEnsureRunningEslintForFile(filename, language)) {
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
			if (built &&
					!editorLspSendRawJsonToFd(g_lsp_eslint_client.to_server_fd, payload.buf)) {
				editorLspClientCleanup(&g_lsp_eslint_client, 0);
			}
			free(payload.buf);
			free(uri);
		}
	}

	*doc_open_in_out = 0;
	*doc_version_in_out = 0;
	editorLspClearDiagnosticsForFile(filename);
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

int editorLspRequestDefinition(const char *filename, enum editorSyntaxLanguage language, int line,
		int character, struct editorLspLocation **locations_out, int *count_out,
		int *timed_out_out) {
	if (locations_out == NULL || count_out == NULL) {
		return -1;
	}
	*locations_out = NULL;
	*count_out = 0;
	if (timed_out_out != NULL) {
		*timed_out_out = 0;
	}

	if (!editorLspFileEnabled(filename, language)) {
		return 0;
	}
	if (filename == NULL || filename[0] == '\0' || line < 0 || character < 0 ||
			!editorLspFileSupportsDefinition(filename, language)) {
		return -1;
	}

	if (g_lsp_mock.enabled) {
		if (!editorLspEnsureRunningForFile(filename, language)) {
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

	if (!editorLspEnsureRunningForFile(filename, language)) {
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
		editorLspClientCleanup(&g_lsp_client, 0);
		return -1;
	}
	free(payload.buf);

	char *response = NULL;
	int timed_out = 0;
	if (!editorLspWaitForResponseId(&g_lsp_client, request_id, ROTIDE_LSP_IO_TIMEOUT_MS, &response,
				&timed_out)) {
		editorLspClientCleanup(&g_lsp_client, 0);
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

int editorLspRequestCodeActionFixes(const char *filename, enum editorSyntaxLanguage language) {
	if (filename == NULL || filename[0] == '\0' ||
			!editorLspEslintEnabledForFile(filename, language)) {
		return 0;
	}

	if (g_lsp_mock.enabled) {
		if (!editorLspEnsureRunningEslintForFile(filename, language)) {
			return -1;
		}
		g_lsp_mock.stats.code_action_count++;
		if (g_lsp_mock.code_action_result_code <= 0) {
			return g_lsp_mock.code_action_result_code;
		}
		return editorLspApplyPendingEdits(g_lsp_mock.code_action_edits,
				g_lsp_mock.code_action_edit_count) >= 0 ?
				g_lsp_mock.code_action_edit_count :
				-1;
	}

	size_t full_text_len = 0;
	char *full_text = NULL;
	if (!E.lsp_eslint_doc_open) {
		full_text = editorDupActiveTextSource(&full_text_len);
		if (full_text == NULL && full_text_len > 0) {
			return -1;
		}
	}
	int ready = editorLspEnsureEslintDocumentOpen(filename, language, &E.lsp_eslint_doc_open,
			&E.lsp_eslint_doc_version, full_text != NULL ? full_text : "", full_text_len);
	free(full_text);
	if (!ready) {
		return -1;
	}

	char *uri = NULL;
	if (!editorLspBuildFileUri(filename, &uri)) {
		return -1;
	}

	int request_id = g_lsp_eslint_client.next_request_id++;
	struct editorLspString payload = {0};
	int built = editorLspStringAppendf(&payload,
			"{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"textDocument/codeAction\",\"params\":{"
			"\"textDocument\":{\"uri\":",
			request_id);
	if (built) {
		built = editorLspStringAppendJsonEscaped(&payload, uri, strlen(uri));
	}
	if (built) {
		built = editorLspStringAppend(&payload,
				"},\"range\":{\"start\":{\"line\":0,\"character\":0},"
				"\"end\":{\"line\":0,\"character\":0}},\"context\":{\"diagnostics\":[");
	}
	for (int i = 0; built && i < E.lsp_diagnostic_count; i++) {
		if (i > 0) {
			built = editorLspStringAppend(&payload, ",");
		}
		int start_character = editorLspClientProtocolCharacterFromBufferColumn(
				&g_lsp_eslint_client, E.lsp_diagnostics[i].start_line,
				E.lsp_diagnostics[i].start_character);
		int end_character = editorLspClientProtocolCharacterFromBufferColumn(
				&g_lsp_eslint_client, E.lsp_diagnostics[i].end_line,
				E.lsp_diagnostics[i].end_character);
		built = editorLspStringAppendf(&payload,
				"{\"range\":{\"start\":{\"line\":%d,\"character\":%d},"
				"\"end\":{\"line\":%d,\"character\":%d}},\"severity\":%d,\"message\":",
				E.lsp_diagnostics[i].start_line, start_character,
				E.lsp_diagnostics[i].end_line, end_character, E.lsp_diagnostics[i].severity);
		if (built) {
			built = editorLspStringAppendJsonEscaped(&payload,
					E.lsp_diagnostics[i].message != NULL ? E.lsp_diagnostics[i].message : "",
					E.lsp_diagnostics[i].message != NULL ? strlen(E.lsp_diagnostics[i].message) : 0);
		}
		if (built) {
			built = editorLspStringAppend(&payload, "}");
		}
	}
	if (built) {
		built = editorLspStringAppend(&payload,
				"],\"only\":[\"source.fixAll.eslint\"]}}}");
	}
	free(uri);
	if (!built) {
		free(payload.buf);
		return -1;
	}

	if (!editorLspSendRawJsonToFd(g_lsp_eslint_client.to_server_fd, payload.buf)) {
		free(payload.buf);
		editorLspClientCleanup(&g_lsp_eslint_client, 0);
		return -1;
	}
	free(payload.buf);

	char *response = NULL;
	int timed_out = 0;
	if (!editorLspWaitForResponseId(&g_lsp_eslint_client, request_id, ROTIDE_LSP_IO_TIMEOUT_MS,
				&response, &timed_out)) {
		editorLspClientCleanup(&g_lsp_eslint_client, 0);
		return timed_out ? -2 : -1;
	}
	if (editorLspResponseHasError(response)) {
		free(response);
		return -1;
	}

	const char *result_key = strstr(response, "\"result\"");
	const char *result_colon = result_key != NULL ? strchr(result_key, ':') : NULL;
	const char *result = result_colon != NULL ? editorLspSkipWs(result_colon + 1) : NULL;
	if (result == NULL || strncmp(result, "null", 4) == 0) {
		free(response);
		return 0;
	}

	const char *scan = result;
	struct editorLspPendingEdit *edits = NULL;
	int count = 0;
	while (scan != NULL) {
		const char *edit_key = strstr(scan, "\"edit\"");
		if (edit_key == NULL) {
			break;
		}
		const char *edit_colon = strchr(edit_key, ':');
		const char *edit_object = edit_colon != NULL ? strchr(edit_colon + 1, '{') : NULL;
		const char *edit_end = edit_object != NULL ? editorLspFindJsonObjectEnd(edit_object) : NULL;
		if (edit_end == NULL) {
			break;
		}
		if (editorLspParseWorkspaceEditChanges(edit_object, filename, &edits, &count) && count > 0) {
			break;
		}
		editorLspFreePendingEdits(edits, count);
		edits = NULL;
		count = 0;
		scan = edit_end;
	}
	free(response);
	if (count <= 0) {
		editorLspFreePendingEdits(edits, count);
		return 0;
	}

	int applied = editorLspApplyPendingEditsWithClient(&g_lsp_eslint_client, edits, count);
	editorLspFreePendingEdits(edits, count);
	return applied >= 0 ? applied : -1;
}

enum editorLspStartupFailureReason editorLspLastStartupFailureReason(void) {
	return g_lsp_last_startup_failure_reason;
}

void editorLspTestSetMockEnabled(int enabled) {
	g_lsp_mock.enabled = enabled ? 1 : 0;
	if (!g_lsp_mock.enabled) {
		g_lsp_mock.primary_server_alive = 0;
		g_lsp_mock.primary_server_kind = EDITOR_LSP_SERVER_NONE;
		g_lsp_mock.eslint_server_alive = 0;
		free(g_lsp_mock.primary_workspace_root_path);
		g_lsp_mock.primary_workspace_root_path = NULL;
		free(g_lsp_mock.eslint_workspace_root_path);
		g_lsp_mock.eslint_workspace_root_path = NULL;
		g_lsp_mock.last_did_open_language_id[0] = '\0';
	}
}

void editorLspTestSetMockServerAlive(int alive) {
	g_lsp_mock.primary_server_alive = alive ? 1 : 0;
}

void editorLspTestResetMock(void) {
	editorLspFreeLocations(g_lsp_mock.definition_locations, g_lsp_mock.definition_location_count);
	editorLspFreeDiagnostics(g_lsp_mock.diagnostics, g_lsp_mock.diagnostic_count);
	editorLspFreePendingEdits(g_lsp_mock.code_action_edits, g_lsp_mock.code_action_edit_count);
	g_lsp_mock.definition_locations = NULL;
	g_lsp_mock.definition_location_count = 0;
	g_lsp_mock.definition_result_code = 1;
	g_lsp_mock.diagnostics = NULL;
	g_lsp_mock.diagnostic_count = 0;
	free(g_lsp_mock.diagnostic_path);
	g_lsp_mock.diagnostic_path = NULL;
	g_lsp_mock.code_action_result_code = 0;
	g_lsp_mock.code_action_edits = NULL;
	g_lsp_mock.code_action_edit_count = 0;
	g_lsp_mock.primary_server_alive = 0;
	g_lsp_mock.primary_server_kind = EDITOR_LSP_SERVER_NONE;
	g_lsp_mock.eslint_server_alive = 0;
	free(g_lsp_mock.primary_workspace_root_path);
	g_lsp_mock.primary_workspace_root_path = NULL;
	free(g_lsp_mock.eslint_workspace_root_path);
	g_lsp_mock.eslint_workspace_root_path = NULL;
	memset(&g_lsp_mock.stats, 0, sizeof(g_lsp_mock.stats));
	memset(&g_lsp_mock.last_change, 0, sizeof(g_lsp_mock.last_change));
	g_lsp_mock.last_did_open_language_id[0] = '\0';
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

void editorLspTestGetLastDidOpenLanguageId(char *out, size_t out_size) {
	if (out == NULL || out_size == 0) {
		return;
	}
	(void)snprintf(out, out_size, "%s", g_lsp_mock.last_did_open_language_id);
	out[out_size - 1] = '\0';
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

void editorLspTestSetMockDiagnostics(const char *path, const struct editorLspDiagnostic *diagnostics,
		int count) {
	editorLspFreeDiagnostics(g_lsp_mock.diagnostics, g_lsp_mock.diagnostic_count);
	g_lsp_mock.diagnostics = NULL;
	g_lsp_mock.diagnostic_count = 0;
	free(g_lsp_mock.diagnostic_path);
	g_lsp_mock.diagnostic_path = path != NULL ? strdup(path) : NULL;
	(void)editorLspCopyDiagnostics(&g_lsp_mock.diagnostics, &g_lsp_mock.diagnostic_count,
			diagnostics, count);
}

void editorLspTestSetMockCodeActionResult(int result_code,
		const struct editorLspDiagnostic *edits, int count) {
	editorLspFreePendingEdits(g_lsp_mock.code_action_edits, g_lsp_mock.code_action_edit_count);
	g_lsp_mock.code_action_edits = NULL;
	g_lsp_mock.code_action_edit_count = 0;
	g_lsp_mock.code_action_result_code = result_code;
	if (edits == NULL || count <= 0) {
		return;
	}
	g_lsp_mock.code_action_edits = calloc((size_t)count, sizeof(*g_lsp_mock.code_action_edits));
	if (g_lsp_mock.code_action_edits == NULL) {
		g_lsp_mock.code_action_result_code = -1;
		return;
	}
	g_lsp_mock.code_action_edit_count = count;
	for (int i = 0; i < count; i++) {
		g_lsp_mock.code_action_edits[i].start_line = edits[i].start_line;
		g_lsp_mock.code_action_edits[i].start_character = edits[i].start_character;
		g_lsp_mock.code_action_edits[i].end_line = edits[i].end_line;
		g_lsp_mock.code_action_edits[i].end_character = edits[i].end_character;
		g_lsp_mock.code_action_edits[i].new_text =
				edits[i].message != NULL ? strdup(edits[i].message) : strdup("");
		if (g_lsp_mock.code_action_edits[i].new_text == NULL) {
			editorLspFreePendingEdits(g_lsp_mock.code_action_edits,
					g_lsp_mock.code_action_edit_count);
			g_lsp_mock.code_action_edits = NULL;
			g_lsp_mock.code_action_edit_count = 0;
			g_lsp_mock.code_action_result_code = -1;
			return;
		}
	}
}

int editorLspTestParseDefinitionResponse(const char *response_json,
		struct editorLspLocation **locations_out, int *count_out) {
	if (response_json == NULL) {
		return 0;
	}
	return editorLspParseDefinitionLocations(response_json, locations_out, count_out);
}

char *editorLspTestBuildInitializeRequestJson(int request_id, const char *root_uri,
		int process_id) {
	return editorLspBuildInitializeRequestJson(request_id, root_uri, process_id);
}
