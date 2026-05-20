#include "language/lsp_protocol.h"

#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "language/lsp_responses.h"
#include "support/file_io.h"
#include "support/size_utils.h"
#include "text/utf8.h"
#include "workspace/tabs.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

char *editorLspBuildInitializeRequestJson(int request_id, const char *root_uri, int process_id) {
	if (root_uri == NULL || root_uri[0] == '\0') {
		return NULL;
	}

	struct editorLspString init = {0};
	int built = editorLspStringAppendf(
	        &init,
	        "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"initialize\",\"params\":{"
	        "\"processId\":%d,\"rootUri\":",
	        request_id, process_id);
	if (built) {
		built = editorLspStringAppendJsonEscaped(&init, root_uri, strlen(root_uri));
	}
	if (built) {
		built = editorLspStringAppend(
		        &init,
		        ",\"capabilities\":{\"general\":{\"positionEncodings\":[\"utf-8\",\"utf-"
		        "16\"]},"
		        "\"workspace\":{\"applyEdit\":true},"
		        "\"textDocument\":{\"codeAction\":{\"codeActionLiteralSupport\":{"
		        "\"codeActionKind\":{\"valueSet\":[\"quickfix\",\"source.fixAll\","
		        "\"source.fixAll.eslint\"]}}},"
		        "\"completion\":{\"completionItem\":{\"snippetSupport\":false,"
		        "\"insertReplaceSupport\":false}},"
		        "\"documentSymbol\":{\"hierarchicalDocumentSymbolSupport\":true}}}}}");
	}
	if (!built) {
		free(init.buf);
		return NULL;
	}
	return init.buf;
}

int editorLspBuildFileUri(const char *path, char **uri_out) {
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
		int unreserved = isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~' ||
		                 ch == '/';
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

static int lspProtocolHexValue(char c) {
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

char *editorLspDecodeFileUri(const char *uri) {
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
			int hi = lspProtocolHexValue(rest[i + 1]);
			int lo = lspProtocolHexValue(rest[i + 2]);
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

int editorLspProtocolCharacterToBufferColumn(int line, int protocol_character) {
	return editorLspClientProtocolCharacterToBufferColumn(editorLspPrimaryClient(), line,
	                                                      protocol_character);
}

int editorLspClientProtocolCharacterToBufferColumn(struct editorLspClient *client, int line,
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

static int lspProtocolDiagnosticsErrorCount(const struct editorLspDiagnostic *diagnostics,
                                            int count) {
	int errors = 0;
	for (int i = 0; i < count; i++) {
		if (diagnostics[i].severity == 1) {
			errors++;
		}
	}
	return errors;
}

static int lspProtocolDiagnosticsWarningCount(const struct editorLspDiagnostic *diagnostics,
                                              int count) {
	int warnings = 0;
	for (int i = 0; i < count; i++) {
		if (diagnostics[i].severity == 2) {
			warnings++;
		}
	}
	return warnings;
}

static int lspProtocolPathMatches(const char *left, const char *right) {
	return left != NULL && right != NULL && editorPathsReferToSameFile(left, right);
}

static void lspProtocolUpdateDiagnosticFields(struct editorLspDiagnostic **diagnostics_in_out,
                                              int *count_in_out, int *error_count_out,
                                              int *warning_count_out,
                                              const struct editorLspDiagnostic *diagnostics,
                                              int count) {
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
	*error_count_out = lspProtocolDiagnosticsErrorCount(*diagnostics_in_out, *count_in_out);
	*warning_count_out = lspProtocolDiagnosticsWarningCount(*diagnostics_in_out, *count_in_out);
}

static const char *
lspProtocolDiagnosticSourceLabelForServerKind(enum editorLspServerKind server_kind) {
	switch (server_kind) {
		case EDITOR_LSP_SERVER_GOPLS:
			return "gopls";
		case EDITOR_LSP_SERVER_CLANGD:
			return "clangd";
		case EDITOR_LSP_SERVER_HTML:
			return "HTML LSP";
		case EDITOR_LSP_SERVER_CSS:
			return "CSS LSP";
		case EDITOR_LSP_SERVER_JSON:
			return "JSON LSP";
		case EDITOR_LSP_SERVER_JAVASCRIPT:
			return "TypeScript LSP";
		case EDITOR_LSP_SERVER_ESLINT:
			return "ESLint";
		default:
			return "LSP";
	}
}

static void
lspProtocolSetDiagnosticsForPathWithSource(const char *path,
                                           const struct editorLspDiagnostic *diagnostics, int count,
                                           const char *source_label) {
	if (path == NULL || path[0] == '\0') {
		return;
	}
	if (source_label == NULL || source_label[0] == '\0') {
		source_label = "LSP";
	}

	int active_matches = lspProtocolPathMatches(path, E.filename);
	int old_count = active_matches ? E.lsp_diagnostic_count : 0;
	int old_errors = active_matches ? E.lsp_diagnostic_error_count : 0;
	int old_warnings = active_matches ? E.lsp_diagnostic_warning_count : 0;

	if (active_matches) {
		lspProtocolUpdateDiagnosticFields(
		        &E.lsp_diagnostics, &E.lsp_diagnostic_count, &E.lsp_diagnostic_error_count,
		        &E.lsp_diagnostic_warning_count, diagnostics, count);
	}
	for (int i = 0; i < E.tab_count; i++) {
		struct editorBuffer *tab = editorTabBufferHandleAtMutable(i);
		if (tab == NULL || !lspProtocolPathMatches(path, tab->filename)) {
			continue;
		}
		lspProtocolUpdateDiagnosticFields(&tab->lsp_diagnostics, &tab->lsp_diagnostic_count,
		                                  &tab->lsp_diagnostic_error_count,
		                                  &tab->lsp_diagnostic_warning_count, diagnostics,
		                                  count);
	}

	if (active_matches &&
	    (old_count != E.lsp_diagnostic_count || old_errors != E.lsp_diagnostic_error_count ||
	     old_warnings != E.lsp_diagnostic_warning_count)) {
		if (E.lsp_diagnostic_count == 0) {
			editorSetStatusMsg("%s: diagnostics cleared", source_label);
		} else {
			editorSetStatusMsg("%s: %d error%s, %d warning%s", source_label,
			                   E.lsp_diagnostic_error_count,
			                   E.lsp_diagnostic_error_count == 1 ? "" : "s",
			                   E.lsp_diagnostic_warning_count,
			                   E.lsp_diagnostic_warning_count == 1 ? "" : "s");
		}
	}
}

void editorLspSetDiagnosticsForPath(const char *path, const struct editorLspDiagnostic *diagnostics,
                                    int count) {
	lspProtocolSetDiagnosticsForPathWithSource(path, diagnostics, count, "LSP");
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
	if (lspProtocolPathMatches(filename, E.filename)) {
		summary_out->count = E.lsp_diagnostic_count;
		summary_out->error_count = E.lsp_diagnostic_error_count;
		summary_out->warning_count = E.lsp_diagnostic_warning_count;
		return;
	}
	for (int i = 0; i < E.tab_count; i++) {
		const struct editorBuffer *tab = editorTabBufferHandleAt(i);
		if (tab == NULL || !lspProtocolPathMatches(filename, tab->filename)) {
			continue;
		}
		summary_out->count = tab->lsp_diagnostic_count;
		summary_out->error_count = tab->lsp_diagnostic_error_count;
		summary_out->warning_count = tab->lsp_diagnostic_warning_count;
		return;
	}
}

static int lspProtocolParseDiagnosticsMessage(const char *message, char **path_out,
                                              struct editorLspDiagnostic **diagnostics_out,
                                              int *count_out) {
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

		const char *range_key =
		        editorLspStrstrBounded(object_start, "\"range\"", object_end);
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

static int lspProtocolPendingEditCompareDesc(const void *lhs, const void *rhs) {
	const struct editorLspPendingEdit *left = lhs;
	const struct editorLspPendingEdit *right = rhs;
	if (left->start_line != right->start_line) {
		return right->start_line - left->start_line;
	}
	return right->start_character - left->start_character;
}

int editorLspApplyPendingEditsWithClient(struct editorLspClient *client,
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
	qsort(sorted, (size_t)count, sizeof(*sorted), lspProtocolPendingEditCompareDesc);

	for (int i = 0; i < count; i++) {
		int start_cx = editorLspClientProtocolCharacterToBufferColumn(
		        client, sorted[i].start_line, sorted[i].start_character);
		int end_cx = editorLspClientProtocolCharacterToBufferColumn(
		        client, sorted[i].end_line, sorted[i].end_character);
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
				cursor_after =
				        start_offset + new_len + (cursor_before - end_offset);
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

int editorLspApplyPendingEdits(const struct editorLspPendingEdit *edits, int count) {
	return editorLspApplyPendingEditsWithClient(editorLspPrimaryClient(), edits, count);
}

static int lspProtocolRespondToRequest(struct editorLspClient *client, int request_id,
                                       const char *result_json) {
	struct editorLspString payload = {0};
	int built = editorLspStringAppendf(
	        &payload, "{\"jsonrpc\":\"2.0\",\"id\":%d,\"result\":", request_id);
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
	int sent =
	        editorLspSendRawJsonToFd(client != NULL ? client->to_server_fd : -1, payload.buf);
	free(payload.buf);
	return sent;
}

int editorLspProcessIncomingMessage(struct editorLspClient *client, const char *message) {
	if (message == NULL || message[0] == '\0') {
		return 1;
	}

	char *path = NULL;
	struct editorLspDiagnostic *diagnostics = NULL;
	int diagnostic_count = 0;
	if (lspProtocolParseDiagnosticsMessage(message, &path, &diagnostics, &diagnostic_count)) {
		const char *source_label = lspProtocolDiagnosticSourceLabelForServerKind(
		        client != NULL ? client->server_kind : EDITOR_LSP_SERVER_NONE);
		lspProtocolSetDiagnosticsForPathWithSource(path, diagnostics, diagnostic_count,
		                                           source_label);
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
			return lspProtocolRespondToRequest(client, request_id, "[{}]");
		}
		return 1;
	}
	if (strcmp(method, "client/registerCapability") == 0) {
		free(method);
		if (has_request_id) {
			return lspProtocolRespondToRequest(client, request_id, "null");
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
		int parsed =
		        editorLspParseWorkspaceEditChanges(message, E.filename, &edits, &count);
		int applied = parsed && count > 0 &&
		              editorLspApplyPendingEditsWithClient(client, edits, count) >= 0;
		editorLspFreePendingEdits(edits, count);
		return lspProtocolRespondToRequest(
		        client, request_id, applied ? "{\"applied\":true}" : "{\"applied\":false}");
	}

	free(method);
	if (has_request_id) {
		return lspProtocolRespondToRequest(client, request_id, "null");
	}
	return 1;
}

int editorLspUtf8ColumnToUtf16Units(const char *text, size_t text_len, int byte_column) {
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

int editorLspUtf16UnitsToUtf8Column(const char *text, size_t text_len, int utf16_units) {
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

int editorLspReadActiveLineText(int line, char **text_out, size_t *len_out) {
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

int editorLspProtocolCharacterFromBufferColumn(int line, int byte_column) {
	return editorLspClientProtocolCharacterFromBufferColumn(editorLspPrimaryClient(), line,
	                                                        byte_column);
}

int editorLspClientProtocolCharacterFromBufferColumn(struct editorLspClient *client, int line,
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
