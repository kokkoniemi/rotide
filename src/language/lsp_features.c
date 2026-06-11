#include "editing/text_source.h"
#include "language/lsp.h"
#include "language/lsp_framing.h"
#include "language/lsp_json.h"
#include "language/lsp_mock.h"
#include "language/lsp_protocol.h"
#include "language/lsp_responses.h"
#include "language/lsp_transport.h"
#include "language/syntax.h"
#include "rotide.h"
#include "support/json.h"

#include <stdlib.h>
#include <string.h>

static int lspFeaturesRequestLocationsByMethod(const char *method, int *mock_counter,
                                               const char *filename,
                                               enum editorSyntaxLanguage language, int line,
                                               int character,
                                               struct editorLspLocation **locations_out,
                                               int *count_out, int *timed_out_out) {
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
		if (mock_counter != NULL) {
			(*mock_counter)++;
		}
		int is_implementation = strcmp(method, "textDocument/implementation") == 0;
		int result_code = g_lsp_mock.definition_result_code;
		struct editorLspLocation *mock_locations = g_lsp_mock.definition_locations;
		int mock_location_count = g_lsp_mock.definition_location_count;
		if (is_implementation && g_lsp_mock.implementation_response_configured) {
			result_code = g_lsp_mock.implementation_result_code;
			mock_locations = g_lsp_mock.implementation_locations;
			mock_location_count = g_lsp_mock.implementation_location_count;
		}
		if (result_code == -2) {
			if (timed_out_out != NULL) {
				*timed_out_out = 1;
			}
			return -2;
		}
		if (result_code < 0) {
			return -1;
		}
		if (!editorLspCopyLocations(locations_out, count_out, mock_locations,
		                            mock_location_count)) {
			return -1;
		}
		return 1;
	}

	struct editorLspClient *client = editorLspEnsureClientForFile(filename, language);
	if (client == NULL) {
		return -1;
	}

	char *uri = NULL;
	if (!editorLspBuildFileUri(filename, &uri)) {
		return -1;
	}

	int protocol_character = editorLspProtocolCharacterFromBufferColumn(line, character);

	int request_id = client->next_request_id++;
	struct editorJsonString payload = {0};
	int built = editorLspStringAppendf(
	        &payload,
	        "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"%s\",\"params\":{"
	        "\"textDocument\":{\"uri\":",
	        request_id, method);
	if (built) {
		built = editorLspStringAppendJsonEscaped(&payload, uri, strlen(uri));
	}
	if (built) {
		built = editorLspStringAppendf(&payload,
		                               "},\"position\":{\"line\":%d,\"character\":%d}}}",
		                               line, protocol_character);
	}
	free(uri);
	if (!built) {
		free(payload.buf);
		return -1;
	}

	if (!editorLspSendRawJsonToFd(client->to_server_fd, payload.buf)) {
		free(payload.buf);
		editorLspClientCleanup(client, 0);
		return -1;
	}
	free(payload.buf);

	char *response = NULL;
	int timed_out = 0;
	if (!editorLspWaitForResponseId(client, request_id, ROTIDE_LSP_IO_TIMEOUT_MS, &response,
	                                &timed_out)) {
		editorLspClientCleanup(client, 0);
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

int editorLspRequestDefinition(const char *filename, enum editorSyntaxLanguage language, int line,
                               int character, struct editorLspLocation **locations_out,
                               int *count_out, int *timed_out_out) {
	return lspFeaturesRequestLocationsByMethod(
	        "textDocument/definition", &g_lsp_mock.stats.definition_count, filename, language,
	        line, character, locations_out, count_out, timed_out_out);
}

int editorLspRequestImplementation(const char *filename, enum editorSyntaxLanguage language,
                                   int line, int character,
                                   struct editorLspLocation **locations_out, int *count_out,
                                   int *timed_out_out) {
	return lspFeaturesRequestLocationsByMethod(
	        "textDocument/implementation", &g_lsp_mock.stats.implementation_count, filename,
	        language, line, character, locations_out, count_out, timed_out_out);
}

/* `textDocument/references` returns `Location[]` like definition, but its params
 * carry a `context.includeDeclaration`, so it builds its own request rather than
 * reusing the shared locations helper. */
int editorLspRequestReferences(const char *filename, enum editorSyntaxLanguage language, int line,
                               int character, struct editorLspLocation **locations_out,
                               int *count_out, int *timed_out_out) {
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
		g_lsp_mock.stats.references_count++;
		if (g_lsp_mock.references_result_code == -2) {
			if (timed_out_out != NULL) {
				*timed_out_out = 1;
			}
			return -2;
		}
		if (g_lsp_mock.references_result_code < 0) {
			return -1;
		}
		if (!editorLspCopyLocations(locations_out, count_out,
		                            g_lsp_mock.references_locations,
		                            g_lsp_mock.references_location_count)) {
			return -1;
		}
		return 1;
	}

	struct editorLspClient *client = editorLspEnsureClientForFile(filename, language);
	if (client == NULL) {
		return -1;
	}

	char *uri = NULL;
	if (!editorLspBuildFileUri(filename, &uri)) {
		return -1;
	}

	int protocol_character = editorLspProtocolCharacterFromBufferColumn(line, character);
	int request_id = client->next_request_id++;
	struct editorJsonString payload = {0};
	int built = editorLspStringAppendf(
	        &payload,
	        "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"textDocument/references\",\"params\":{"
	        "\"textDocument\":{\"uri\":",
	        request_id);
	if (built) {
		built = editorLspStringAppendJsonEscaped(&payload, uri, strlen(uri));
	}
	if (built) {
		built = editorLspStringAppendf(&payload,
		                               "},\"position\":{\"line\":%d,\"character\":%d},"
		                               "\"context\":{\"includeDeclaration\":true}}}",
		                               line, protocol_character);
	}
	free(uri);
	if (!built) {
		free(payload.buf);
		return -1;
	}

	if (!editorLspSendRawJsonToFd(client->to_server_fd, payload.buf)) {
		free(payload.buf);
		editorLspClientCleanup(client, 0);
		return -1;
	}
	free(payload.buf);

	char *response = NULL;
	int timed_out = 0;
	if (!editorLspWaitForResponseId(client, request_id, ROTIDE_LSP_IO_TIMEOUT_MS, &response,
	                                &timed_out)) {
		editorLspClientCleanup(client, 0);
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

int editorLspRequestDocumentSymbols(const char *filename, enum editorSyntaxLanguage language,
                                    struct editorLspSymbol **symbols_out, int *count_out,
                                    int *timed_out_out) {
	if (symbols_out == NULL || count_out == NULL) {
		return -1;
	}
	*symbols_out = NULL;
	*count_out = 0;
	if (timed_out_out != NULL) {
		*timed_out_out = 0;
	}

	if (!editorLspFileEnabled(filename, language)) {
		return 0;
	}
	if (filename == NULL || filename[0] == '\0' ||
	    !editorLspFileSupportsDefinition(filename, language)) {
		return -1;
	}

	if (g_lsp_mock.enabled) {
		if (!editorLspEnsureRunningForFile(filename, language)) {
			return -1;
		}
		g_lsp_mock.stats.document_symbol_count++;
		if (g_lsp_mock.document_symbol_result_code == -2) {
			if (timed_out_out != NULL) {
				*timed_out_out = 1;
			}
			return -2;
		}
		if (g_lsp_mock.document_symbol_result_code < 0) {
			return -1;
		}
		if (!editorLspCopySymbols(symbols_out, count_out, g_lsp_mock.document_symbols,
		                          g_lsp_mock.document_symbol_count)) {
			return -1;
		}
		return 1;
	}

	struct editorLspClient *client = editorLspEnsureClientForFile(filename, language);
	if (client == NULL) {
		return -1;
	}

	char *uri = NULL;
	if (!editorLspBuildFileUri(filename, &uri)) {
		return -1;
	}

	int request_id = client->next_request_id++;
	struct editorJsonString payload = {0};
	int built = editorLspStringAppendf(&payload,
	                                   "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":"
	                                   "\"textDocument/documentSymbol\",\"params\":{"
	                                   "\"textDocument\":{\"uri\":",
	                                   request_id);
	if (built) {
		built = editorLspStringAppendJsonEscaped(&payload, uri, strlen(uri));
	}
	if (built) {
		built = editorLspStringAppend(&payload, "}}}");
	}
	free(uri);
	if (!built) {
		free(payload.buf);
		return -1;
	}

	if (!editorLspSendRawJsonToFd(client->to_server_fd, payload.buf)) {
		free(payload.buf);
		editorLspClientCleanup(client, 0);
		return -1;
	}
	free(payload.buf);

	char *response = NULL;
	int timed_out = 0;
	if (!editorLspWaitForResponseId(client, request_id, ROTIDE_LSP_IO_TIMEOUT_MS, &response,
	                                &timed_out)) {
		editorLspClientCleanup(client, 0);
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

	struct editorLspSymbol *symbols = NULL;
	int count = 0;
	if (!editorLspParseDocumentSymbols(response, &symbols, &count)) {
		free(response);
		return -1;
	}
	free(response);

	*symbols_out = symbols;
	*count_out = count;
	return 1;
}

int editorLspCompletionEnabledForFile(const char *filename, enum editorSyntaxLanguage language) {
	if (!E.lsp_config.autocomplete_enabled) {
		return 0;
	}
	if (!editorLspFileEnabled(filename, language)) {
		return 0;
	}
	if (g_lsp_mock.enabled) {
		return 1;
	}
	struct editorLspClient *client = editorLspPrimaryClient();
	return client->completion_supported &&
	       editorLspWorkspaceRootsMatch(client->workspace_root_path,
	                                    client->workspace_root_path);
}

const char *editorLspCompletionTriggerCharsForFile(const char *filename,
                                                   enum editorSyntaxLanguage language) {
	if (!editorLspCompletionEnabledForFile(filename, language)) {
		return NULL;
	}
	if (g_lsp_mock.enabled) {
		return ".";
	}
	return editorLspPrimaryClient()->completion_trigger_chars;
}

void editorLspCancelCompletion(void) {
	editorLspCompletionPendingClear(&editorLspPrimaryClient()->completion_pending);
	g_lsp_mock.completion_pending_request_id = 0;
}

int editorLspCompletionPendingActive(void) {
	if (g_lsp_mock.enabled) {
		return g_lsp_mock.completion_pending_request_id != 0;
	}
	return editorLspPrimaryClient()->completion_pending.request_id != 0;
}

int editorLspRequestCompletionAsync(const char *filename, enum editorSyntaxLanguage language,
                                    int line, int character, int document_version,
                                    int prefix_start_cx, const char *prefix, int trigger_kind,
                                    int trigger_character) {
	if (filename == NULL || filename[0] == '\0' || line < 0 || character < 0) {
		return 0;
	}
	if (!editorLspCompletionEnabledForFile(filename, language)) {
		return 0;
	}

	editorLspCancelCompletion();

	if (g_lsp_mock.enabled) {
		if (!editorLspEnsureRunningForFile(filename, language)) {
			return 0;
		}
		g_lsp_mock.stats.completion_count++;
		int request_id = ++g_lsp_mock.completion_pending_request_id;
		if (request_id <= 0) {
			request_id = 1;
			g_lsp_mock.completion_pending_request_id = 1;
		}
		struct editorLspCompletionPending *pending =
		        &editorLspPrimaryClient()->completion_pending;
		pending->request_id = request_id;
		pending->document_version = document_version;
		pending->cy = line;
		pending->cx = character;
		pending->prefix_start_cx = prefix_start_cx;
		free(pending->prefix);
		pending->prefix = prefix != NULL ? strdup(prefix) : strdup("");
		free(pending->filename);
		pending->filename = strdup(filename);
		(void)trigger_kind;
		(void)trigger_character;
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

	int protocol_character = editorLspProtocolCharacterFromBufferColumn(line, character);
	int request_id = client->next_request_id++;
	struct editorJsonString payload = {0};
	int built = editorLspStringAppendf(
	        &payload,
	        "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"textDocument/completion\",\"params\":{"
	        "\"textDocument\":{\"uri\":",
	        request_id);
	if (built) {
		built = editorLspStringAppendJsonEscaped(&payload, uri, strlen(uri));
	}
	if (built) {
		built = editorLspStringAppendf(&payload,
		                               "},\"position\":{\"line\":%d,\"character\":%d}",
		                               line, protocol_character);
	}
	free(uri);
	if (built) {
		if (trigger_kind == 2 && trigger_character > 0 && trigger_character < 0x80) {
			char ch = (char)trigger_character;
			built = editorLspStringAppendf(
			        &payload, ",\"context\":{\"triggerKind\":2,\"triggerCharacter\":");
			if (built) {
				built = editorLspStringAppendJsonEscaped(&payload, &ch, 1);
			}
			if (built) {
				built = editorLspStringAppend(&payload, "}}}");
			}
		} else {
			built = editorLspStringAppendf(&payload,
			                               ",\"context\":{\"triggerKind\":%d}}}",
			                               trigger_kind > 0 ? trigger_kind : 1);
		}
	}
	if (!built) {
		free(payload.buf);
		return 0;
	}

	if (!editorLspSendRawJsonToFd(client->to_server_fd, payload.buf)) {
		free(payload.buf);
		editorLspClientCleanup(client, 0);
		return 0;
	}
	free(payload.buf);

	struct editorLspCompletionPending *pending = &client->completion_pending;
	pending->request_id = request_id;
	pending->document_version = document_version;
	pending->cy = line;
	pending->cx = character;
	pending->prefix_start_cx = prefix_start_cx;
	free(pending->prefix);
	pending->prefix = prefix != NULL ? strdup(prefix) : strdup("");
	free(pending->filename);
	pending->filename = strdup(filename);
	g_lsp_mock.stats.completion_count++;
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
		                                  g_lsp_mock.code_action_edit_count) >= 0
		               ? g_lsp_mock.code_action_edit_count
		               : -1;
	}

	size_t full_text_len = 0;
	char *full_text = NULL;
	if (!E.lsp_eslint_doc_open) {
		full_text = editorDupActiveTextSource(&full_text_len);
		if (full_text == NULL && full_text_len > 0) {
			return -1;
		}
	}
	int ready = editorLspEnsureEslintDocumentOpen(
	        filename, language, &E.lsp_eslint_doc_open, &E.lsp_eslint_doc_version,
	        full_text != NULL ? full_text : "", full_text_len);
	free(full_text);
	if (!ready) {
		return -1;
	}

	struct editorLspClient *client = editorLspEslintClient();
	if (client == NULL || client->to_server_fd < 0) {
		return -1;
	}

	char *uri = NULL;
	if (!editorLspBuildFileUri(filename, &uri)) {
		return -1;
	}

	int request_id = client->next_request_id++;
	struct editorJsonString payload = {0};
	int built = editorLspStringAppendf(
	        &payload,
	        "{\"jsonrpc\":\"2.0\",\"id\":%d,\"method\":\"textDocument/codeAction\",\"params\":{"
	        "\"textDocument\":{\"uri\":",
	        request_id);
	if (built) {
		built = editorLspStringAppendJsonEscaped(&payload, uri, strlen(uri));
	}
	if (built) {
		built = editorLspStringAppend(
		        &payload,
		        "},\"range\":{\"start\":{\"line\":0,\"character\":0},"
		        "\"end\":{\"line\":0,\"character\":0}},\"context\":{\"diagnostics\":[");
	}
	for (int i = 0; built && i < E.lsp_diagnostic_count; i++) {
		if (i > 0) {
			built = editorLspStringAppend(&payload, ",");
		}
		int start_character = editorLspClientProtocolCharacterFromBufferColumn(
		        client, E.lsp_diagnostics[i].start_line,
		        E.lsp_diagnostics[i].start_character);
		int end_character = editorLspClientProtocolCharacterFromBufferColumn(
		        client, E.lsp_diagnostics[i].end_line, E.lsp_diagnostics[i].end_character);
		built = editorLspStringAppendf(
		        &payload,
		        "{\"range\":{\"start\":{\"line\":%d,\"character\":%d},"
		        "\"end\":{\"line\":%d,\"character\":%d}},\"severity\":%d,\"message\":",
		        E.lsp_diagnostics[i].start_line, start_character,
		        E.lsp_diagnostics[i].end_line, end_character,
		        E.lsp_diagnostics[i].severity);
		if (built) {
			built = editorLspStringAppendJsonEscaped(
			        &payload,
			        E.lsp_diagnostics[i].message != NULL ? E.lsp_diagnostics[i].message
			                                             : "",
			        E.lsp_diagnostics[i].message != NULL
			                ? strlen(E.lsp_diagnostics[i].message)
			                : 0);
		}
		if (built) {
			built = editorLspStringAppend(&payload, "}");
		}
	}
	if (built) {
		built = editorLspStringAppend(&payload, "],\"only\":[\"source.fixAll.eslint\"]}}}");
	}
	free(uri);
	if (!built) {
		free(payload.buf);
		return -1;
	}

	if (!editorLspSendRawJsonToFd(client->to_server_fd, payload.buf)) {
		free(payload.buf);
		editorLspClientCleanup(client, 0);
		return -1;
	}
	free(payload.buf);

	char *response = NULL;
	int timed_out = 0;
	if (!editorLspWaitForResponseId(client, request_id, ROTIDE_LSP_IO_TIMEOUT_MS, &response,
	                                &timed_out)) {
		editorLspClientCleanup(client, 0);
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
		const char *edit_end =
		        edit_object != NULL ? editorLspFindJsonObjectEnd(edit_object) : NULL;
		if (edit_end == NULL) {
			break;
		}
		if (editorLspParseWorkspaceEditChanges(edit_object, filename, &edits, &count) &&
		    count > 0) {
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

	int applied = editorLspApplyPendingEditsWithClient(client, edits, count);
	editorLspFreePendingEdits(edits, count);
	return applied >= 0 ? applied : -1;
}

void editorLspRefreshActiveDocumentSymbols(void) {
	if (E.tab_kind != EDITOR_TAB_FILE || E.filename == NULL || E.filename[0] == '\0' ||
	    !editorLspFileEnabled(E.filename, E.syntax_language) ||
	    !editorLspFileSupportsDefinition(E.filename, E.syntax_language)) {
		if (E.lsp_symbols != NULL) {
			editorLspFreeSymbols(E.lsp_symbols, E.lsp_symbol_count);
		}
		E.lsp_symbols = NULL;
		E.lsp_symbol_count = 0;
		return;
	}

	struct editorLspSymbol *symbols = NULL;
	int count = 0;
	int timed_out = 0;
	int result = editorLspRequestDocumentSymbols(E.filename, E.syntax_language, &symbols,
	                                             &count, &timed_out);
	if (result <= 0) {
		editorLspFreeSymbols(symbols, count);
		return;
	}
	if (E.lsp_symbols != NULL) {
		editorLspFreeSymbols(E.lsp_symbols, E.lsp_symbol_count);
	}
	E.lsp_symbols = symbols;
	E.lsp_symbol_count = count;
}
