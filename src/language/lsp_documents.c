#include "editing/text_source.h"
#include "language/lsp.h"
#include "language/lsp_framing.h"
#include "language/lsp_json.h"
#include "language/lsp_mock.h"
#include "language/lsp_protocol.h"
#include "language/lsp_transport.h"
#include "language/syntax.h"
#include "rotide.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int lspDocumentsIsTrackedLanguage(const char *filename, enum editorSyntaxLanguage language,
                                         int *doc_open_in_out, int *doc_version_in_out) {
	if (filename == NULL || filename[0] == '\0' || !editorLspFileEnabled(filename, language) ||
	    doc_open_in_out == NULL || doc_version_in_out == NULL ||
	    editorLspServerKindForFile(filename, language) == EDITOR_LSP_SERVER_NONE) {
		return 0;
	}
	return 1;
}

static int lspDocumentsIsTrackedEslintLanguage(const char *filename,
                                               enum editorSyntaxLanguage language,
                                               int *doc_open_in_out, int *doc_version_in_out) {
	if (filename == NULL || filename[0] == '\0' ||
	    !editorLspEslintEnabledForFile(filename, language) || doc_open_in_out == NULL ||
	    doc_version_in_out == NULL) {
		return 0;
	}
	return 1;
}

int editorLspEnsureDocumentOpen(const char *filename, enum editorSyntaxLanguage language,
                                int *doc_open_in_out, int *doc_version_in_out,
                                const char *full_text, size_t full_text_len) {
	if (!lspDocumentsIsTrackedLanguage(filename, language, doc_open_in_out,
	                                   doc_version_in_out)) {
		return 1;
	}
	if (*doc_open_in_out) {
		return 1;
	}
	struct editorLspClient *client = NULL;
	if (!g_lsp_mock.enabled) {
		client = editorLspEnsureClientForFile(filename, language);
		if (client == NULL) {
			return 0;
		}
	} else if (!editorLspEnsureRunningForFile(filename, language)) {
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
		g_lsp_mock.last_did_open_language_id[sizeof(g_lsp_mock.last_did_open_language_id) -
		                                     1] = '\0';
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
	int built = editorLspStringAppend(
	        &payload, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
	                  "\"textDocument\":{\"uri\":");
	if (built) {
		built = editorLspStringAppendJsonEscaped(&payload, uri, strlen(uri));
	}
	if (built) {
		built = editorLspStringAppendf(
		        &payload, ",\"languageId\":\"%s\",\"version\":%d,\"text\":", language_id,
		        version);
	}
	if (built) {
		built = editorLspStringAppendJsonEscaped(
		        &payload, full_text != NULL ? full_text : "", full_text_len);
	}
	if (built) {
		built = editorLspStringAppend(&payload, "}}}");
	}
	free(uri);
	if (!built) {
		free(payload.buf);
		return 0;
	}

	int sent = editorLspSendRawJsonToFd(client->to_server_fd, payload.buf);
	free(payload.buf);
	if (!sent) {
		editorLspClientCleanup(client, 0);
		return 0;
	}

	*doc_open_in_out = 1;
	*doc_version_in_out = version;
	return 1;
}

typedef unsigned int (*lspDocumentsProtocolColumnFn)(struct editorLspClient *client, int row,
                                                     int col);

static unsigned int lspDocumentsProtocolColumnGlobal(struct editorLspClient *client, int row,
                                                     int col) {
	(void)client;
	return (unsigned int)editorLspProtocolCharacterFromBufferColumn(row, col);
}

static unsigned int lspDocumentsProtocolColumnPerClient(struct editorLspClient *client, int row,
                                                        int col) {
	return (unsigned int)editorLspClientProtocolCharacterFromBufferColumn(client, row, col);
}

static void lspDocumentsUpdateMockChangeStats(const struct editorSyntaxEdit *edit,
                                              int next_version) {
	g_lsp_mock.stats.did_change_count++;
	g_lsp_mock.last_change.had_range = edit != NULL;
	g_lsp_mock.last_change.start_line = edit != NULL ? (int)edit->start_point.row : 0;
	g_lsp_mock.last_change.start_character = edit != NULL ? (int)edit->start_point.column : 0;
	g_lsp_mock.last_change.end_line = edit != NULL ? (int)edit->old_end_point.row : 0;
	g_lsp_mock.last_change.end_character = edit != NULL ? (int)edit->old_end_point.column : 0;
	g_lsp_mock.last_change.version = next_version;
}

/* Resolves the text for the mock didChange last_change.text field. Returns 0 on
 * text-source build failure. */
static int lspDocumentsCaptureMockChangeText(const struct editorSyntaxEdit *edit,
                                             const char *inserted_text, size_t inserted_text_len,
                                             const char *full_text, size_t full_text_len) {
	const char *mock_text = inserted_text;
	size_t mock_text_len = inserted_text_len;
	int owned = 0;
	if (edit == NULL) {
		mock_text = full_text;
		mock_text_len = full_text_len;
		if (mock_text == NULL) {
			struct editorTextSource source = {0};
			if (!editorBuildActiveTextSource(&source)) {
				return 0;
			}
			mock_text =
			        editorTextSourceDupRange(&source, 0, source.length, &mock_text_len);
			if (mock_text == NULL && mock_text_len > 0) {
				return 0;
			}
			owned = mock_text != NULL;
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
	if (owned) {
		free((char *)mock_text);
	}
	return 1;
}

/* Picks change_text / change_text_len based on whether to send a ranged change. May
 * downgrade send_range to 0 (whole-document) when UTF-16 conversion isn't safe for
 * the given edit. Returns 0 on resolution failure; on success caller owns *owned_out. */
static int lspDocumentsResolveChangeText(int position_encoding_utf16,
                                         const struct editorSyntaxEdit *edit,
                                         const char *inserted_text, size_t inserted_text_len,
                                         const char *full_text, size_t full_text_len,
                                         int *send_range_io, const char **change_text_out,
                                         size_t *change_text_len_out, char **owned_out) {
	*owned_out = NULL;
	if (*send_range_io && position_encoding_utf16 &&
	    (edit->start_point.row != edit->old_end_point.row ||
	     edit->start_point.column != edit->old_end_point.column)) {
		/* UTF-16 range conversion for deletes would require pre-edit text.
		 * Fall back to whole-document sync for those edits. */
		*send_range_io = 0;
	}
	if (*send_range_io) {
		if (inserted_text_len > 0 && inserted_text == NULL) {
			return 0;
		}
		*change_text_out = inserted_text != NULL ? inserted_text : "";
		*change_text_len_out = inserted_text_len;
		return 1;
	}
	*change_text_out = full_text;
	*change_text_len_out = full_text_len;
	if (*change_text_out != NULL) {
		return 1;
	}
	struct editorTextSource source = {0};
	if (!editorBuildActiveTextSource(&source)) {
		return 0;
	}
	*owned_out = editorTextSourceDupRange(&source, 0, source.length, change_text_len_out);
	if (*owned_out == NULL && *change_text_len_out > 0) {
		return 0;
	}
	*change_text_out = *owned_out != NULL ? *owned_out : "";
	return 1;
}

static int lspDocumentsAppendDidChangeRange(struct editorLspString *payload,
                                            struct editorLspClient *client,
                                            const struct editorSyntaxEdit *edit,
                                            lspDocumentsProtocolColumnFn protocol_column_fn) {
	unsigned int start_character = edit->start_point.column;
	unsigned int end_character = edit->old_end_point.column;
	if (client->position_encoding_utf16) {
		start_character = protocol_column_fn(client, (int)edit->start_point.row,
		                                     (int)edit->start_point.column);
		end_character = protocol_column_fn(client, (int)edit->old_end_point.row,
		                                   (int)edit->old_end_point.column);
	}
	return editorLspStringAppendf(payload,
	                              "\"range\":{\"start\":{\"line\":%u,\"character\":%u},"
	                              "\"end\":{\"line\":%u,\"character\":%u}},",
	                              edit->start_point.row, start_character,
	                              edit->old_end_point.row, end_character);
}

static int lspDocumentsBuildDidChangePayload(struct editorLspString *payload,
                                             struct editorLspClient *client, const char *uri,
                                             int next_version, int send_range,
                                             const struct editorSyntaxEdit *edit,
                                             const char *change_text, size_t change_text_len,
                                             lspDocumentsProtocolColumnFn protocol_column_fn) {
	int built = editorLspStringAppend(
	        payload, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didChange\",\"params\":{"
	                 "\"textDocument\":{\"uri\":");
	if (built) {
		built = editorLspStringAppendJsonEscaped(payload, uri, strlen(uri));
	}
	if (built) {
		built = editorLspStringAppendf(payload, ",\"version\":%d},\"contentChanges\":[{",
		                               next_version);
	}
	if (built && send_range) {
		built = lspDocumentsAppendDidChangeRange(payload, client, edit, protocol_column_fn);
	}
	if (built) {
		built = editorLspStringAppend(payload, "\"text\":");
	}
	if (built) {
		if (change_text_len > 0 && change_text == NULL) {
			built = 0;
		} else {
			built = editorLspStringAppendJsonEscaped(
			        payload, change_text != NULL ? change_text : "", change_text_len);
		}
	}
	if (built) {
		built = editorLspStringAppend(payload, "}]}}");
	}
	return built;
}

/* Sends the built payload and cleans up on send failure. Returns 1 on success.
 * Frees uri, owned_full_text, and payload->buf unconditionally. */
static int lspDocumentsSendDidChangePayload(struct editorLspClient *client, char *uri,
                                            char *owned_full_text, struct editorLspString *payload,
                                            int built) {
	free(uri);
	free(owned_full_text);
	if (!built) {
		free(payload->buf);
		return 0;
	}
	int sent = editorLspSendRawJsonToFd(client->to_server_fd, payload->buf);
	free(payload->buf);
	if (!sent) {
		editorLspClientCleanup(client, 0);
		return 0;
	}
	return 1;
}

static int lspDocumentsSendDidChangeForClient(struct editorLspClient *client, const char *filename,
                                              int next_version, const struct editorSyntaxEdit *edit,
                                              const char *inserted_text, size_t inserted_text_len,
                                              const char *full_text, size_t full_text_len,
                                              lspDocumentsProtocolColumnFn protocol_column_fn) {
	int send_range = edit != NULL;
	char *owned_full_text = NULL;
	const char *change_text = NULL;
	size_t change_text_len = 0;
	if (!lspDocumentsResolveChangeText(client->position_encoding_utf16, edit, inserted_text,
	                                   inserted_text_len, full_text, full_text_len, &send_range,
	                                   &change_text, &change_text_len, &owned_full_text)) {
		return 0;
	}

	char *uri = NULL;
	if (!editorLspBuildFileUri(filename, &uri)) {
		free(owned_full_text);
		return 0;
	}

	struct editorLspString payload = {0};
	int built = lspDocumentsBuildDidChangePayload(&payload, client, uri, next_version,
	                                              send_range, edit, change_text,
	                                              change_text_len, protocol_column_fn);
	return lspDocumentsSendDidChangePayload(client, uri, owned_full_text, &payload, built);
}

int editorLspNotifyDidChange(const char *filename, enum editorSyntaxLanguage language,
                             int *doc_open_in_out, int *doc_version_in_out,
                             const struct editorSyntaxEdit *edit, const char *inserted_text,
                             size_t inserted_text_len, const char *full_text,
                             size_t full_text_len) {
	if (!lspDocumentsIsTrackedLanguage(filename, language, doc_open_in_out,
	                                   doc_version_in_out)) {
		return 1;
	}

	if (!editorLspEnsureDocumentOpen(filename, language, doc_open_in_out, doc_version_in_out,
	                                 full_text, full_text_len)) {
		return 0;
	}

	int next_version = *doc_version_in_out + 1;
	if (g_lsp_mock.enabled) {
		lspDocumentsUpdateMockChangeStats(edit, next_version);
		if (!lspDocumentsCaptureMockChangeText(edit, inserted_text, inserted_text_len,
		                                       full_text, full_text_len)) {
			return 0;
		}
		*doc_version_in_out = next_version;
		return 1;
	}

	struct editorLspClient *client = editorLspEnsureClientForFile(filename, language);
	if (client == NULL) {
		return 0;
	}
	if (!lspDocumentsSendDidChangeForClient(client, filename, next_version, edit, inserted_text,
	                                        inserted_text_len, full_text, full_text_len,
	                                        lspDocumentsProtocolColumnGlobal)) {
		return 0;
	}
	*doc_version_in_out = next_version;
	return 1;
}

int editorLspNotifyDidSave(const char *filename, enum editorSyntaxLanguage language,
                           int *doc_open_in_out, int *doc_version_in_out) {
	if (!lspDocumentsIsTrackedLanguage(filename, language, doc_open_in_out,
	                                   doc_version_in_out)) {
		return 1;
	}
	if (!*doc_open_in_out) {
		return 1;
	}

	if (g_lsp_mock.enabled) {
		if (!editorLspEnsureRunningForFile(filename, language)) {
			return 0;
		}
		g_lsp_mock.stats.did_save_count++;
		return 1;
	}

	struct editorLspClient *client = editorLspEnsureClientForFile(filename, language);
	if (client == NULL) {
		return 0;
	}

	char *uri = NULL;
	if (!editorLspBuildFileUri(filename, &uri)) {
		return 0;
	}

	struct editorLspString payload = {0};
	int built = editorLspStringAppend(
	        &payload, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didSave\",\"params\":{"
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

	int sent = editorLspSendRawJsonToFd(client->to_server_fd, payload.buf);
	free(payload.buf);
	if (!sent) {
		editorLspClientCleanup(client, 0);
		return 0;
	}
	return 1;
}

void editorLspNotifyDidClose(const char *filename, enum editorSyntaxLanguage language,
                             int *doc_open_in_out, int *doc_version_in_out) {
	if (!lspDocumentsIsTrackedLanguage(filename, language, doc_open_in_out,
	                                   doc_version_in_out) ||
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

	struct editorLspClient *client = editorLspEnsureClientForFile(filename, language);
	if (client != NULL) {
		char *uri = NULL;
		if (editorLspBuildFileUri(filename, &uri)) {
			struct editorLspString payload = {0};
			int built = editorLspStringAppend(&payload,
			                                  "{\"jsonrpc\":\"2.0\",\"method\":"
			                                  "\"textDocument/didClose\",\"params\":{"
			                                  "\"textDocument\":{\"uri\":");
			if (built) {
				built = editorLspStringAppendJsonEscaped(&payload, uri,
				                                         strlen(uri));
			}
			if (built) {
				built = editorLspStringAppend(&payload, "}}}");
			}
			if (built && !editorLspSendRawJsonToFd(client->to_server_fd, payload.buf)) {
				editorLspClientCleanup(client, 0);
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
	if (!lspDocumentsIsTrackedEslintLanguage(filename, language, doc_open_in_out,
	                                         doc_version_in_out)) {
		return 1;
	}
	if (*doc_open_in_out) {
		return 1;
	}
	struct editorLspClient *client = NULL;
	if (!g_lsp_mock.enabled) {
		client = editorLspEnsureEslintClientForFile(filename, language);
		if (client == NULL) {
			return 0;
		}
	} else if (!editorLspEnsureRunningEslintForFile(filename, language)) {
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
		g_lsp_mock.last_did_open_language_id[sizeof(g_lsp_mock.last_did_open_language_id) -
		                                     1] = '\0';
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
	int built = editorLspStringAppend(
	        &payload, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{"
	                  "\"textDocument\":{\"uri\":");
	if (built) {
		built = editorLspStringAppendJsonEscaped(&payload, uri, strlen(uri));
	}
	if (built) {
		built = editorLspStringAppendf(
		        &payload, ",\"languageId\":\"%s\",\"version\":%d,\"text\":", language_id,
		        version);
	}
	if (built) {
		built = editorLspStringAppendJsonEscaped(
		        &payload, full_text != NULL ? full_text : "", full_text_len);
	}
	if (built) {
		built = editorLspStringAppend(&payload, "}}}");
	}
	free(uri);
	if (!built) {
		free(payload.buf);
		return 0;
	}

	int sent = editorLspSendRawJsonToFd(client->to_server_fd, payload.buf);
	free(payload.buf);
	if (!sent) {
		editorLspClientCleanup(client, 0);
		return 0;
	}

	*doc_open_in_out = 1;
	*doc_version_in_out = version;
	return 1;
}

int editorLspNotifyEslintDidChange(const char *filename, enum editorSyntaxLanguage language,
                                   int *doc_open_in_out, int *doc_version_in_out,
                                   const struct editorSyntaxEdit *edit, const char *inserted_text,
                                   size_t inserted_text_len, const char *full_text,
                                   size_t full_text_len) {
	if (!lspDocumentsIsTrackedEslintLanguage(filename, language, doc_open_in_out,
	                                         doc_version_in_out)) {
		return 1;
	}

	if (!editorLspEnsureEslintDocumentOpen(filename, language, doc_open_in_out,
	                                       doc_version_in_out, full_text, full_text_len)) {
		return 0;
	}

	int next_version = *doc_version_in_out + 1;
	if (g_lsp_mock.enabled) {
		lspDocumentsUpdateMockChangeStats(edit, next_version);
		*doc_version_in_out = next_version;
		return 1;
	}

	struct editorLspClient *client = editorLspEnsureEslintClientForFile(filename, language);
	if (client == NULL) {
		return 0;
	}
	if (!lspDocumentsSendDidChangeForClient(client, filename, next_version, edit, inserted_text,
	                                        inserted_text_len, full_text, full_text_len,
	                                        lspDocumentsProtocolColumnPerClient)) {
		return 0;
	}
	*doc_version_in_out = next_version;
	return 1;
}

int editorLspNotifyEslintDidSave(const char *filename, enum editorSyntaxLanguage language,
                                 int *doc_open_in_out, int *doc_version_in_out) {
	if (!lspDocumentsIsTrackedEslintLanguage(filename, language, doc_open_in_out,
	                                         doc_version_in_out)) {
		return 1;
	}
	if (!*doc_open_in_out) {
		return 1;
	}

	if (g_lsp_mock.enabled) {
		if (!editorLspEnsureRunningEslintForFile(filename, language)) {
			return 0;
		}
		g_lsp_mock.stats.did_save_count++;
		return 1;
	}

	struct editorLspClient *client = editorLspEnsureEslintClientForFile(filename, language);
	if (client == NULL) {
		return 0;
	}

	char *uri = NULL;
	if (!editorLspBuildFileUri(filename, &uri)) {
		return 0;
	}

	struct editorLspString payload = {0};
	int built = editorLspStringAppend(
	        &payload, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didSave\",\"params\":{"
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

	int sent = editorLspSendRawJsonToFd(client->to_server_fd, payload.buf);
	free(payload.buf);
	if (!sent) {
		editorLspClientCleanup(client, 0);
		return 0;
	}
	return 1;
}

void editorLspNotifyEslintDidClose(const char *filename, enum editorSyntaxLanguage language,
                                   int *doc_open_in_out, int *doc_version_in_out) {
	if (!lspDocumentsIsTrackedEslintLanguage(filename, language, doc_open_in_out,
	                                         doc_version_in_out) ||
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

	struct editorLspClient *client = editorLspEnsureEslintClientForFile(filename, language);
	if (client != NULL) {
		char *uri = NULL;
		if (editorLspBuildFileUri(filename, &uri)) {
			struct editorLspString payload = {0};
			int built = editorLspStringAppend(&payload,
			                                  "{\"jsonrpc\":\"2.0\",\"method\":"
			                                  "\"textDocument/didClose\",\"params\":{"
			                                  "\"textDocument\":{\"uri\":");
			if (built) {
				built = editorLspStringAppendJsonEscaped(&payload, uri,
				                                         strlen(uri));
			}
			if (built) {
				built = editorLspStringAppend(&payload, "}}}");
			}
			if (built && !editorLspSendRawJsonToFd(client->to_server_fd, payload.buf)) {
				editorLspClientCleanup(client, 0);
			}
			free(payload.buf);
			free(uri);
		}
	}

	*doc_open_in_out = 0;
	*doc_version_in_out = 0;
	editorLspClearDiagnosticsForFile(filename);
}
