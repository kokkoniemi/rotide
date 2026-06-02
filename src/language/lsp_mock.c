#include "language/lsp_mock.h"

#include "language/autocomplete.h"
#include "language/lsp.h"
#include "language/lsp_protocol.h"
#include "language/lsp_registry.h"
#include "language/lsp_responses.h"
#include "language/lsp_transport.h"
#include "rotide.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct editorLspMockState g_lsp_mock = {0};

int editorLspMockEnabled(void) {
	return g_lsp_mock.enabled;
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
	editorLspFreeLocations(g_lsp_mock.definition_locations,
	                       g_lsp_mock.definition_location_count);
	editorLspFreeLocations(g_lsp_mock.implementation_locations,
	                       g_lsp_mock.implementation_location_count);
	editorLspFreeSymbols(g_lsp_mock.document_symbols, g_lsp_mock.document_symbol_count);
	editorLspFreeDiagnostics(g_lsp_mock.diagnostics, g_lsp_mock.diagnostic_count);
	editorLspFreePendingEdits(g_lsp_mock.code_action_edits, g_lsp_mock.code_action_edit_count);
	editorLspFreeCompletionItems(g_lsp_mock.completion_items, g_lsp_mock.completion_item_count);
	g_lsp_mock.definition_locations = NULL;
	g_lsp_mock.definition_location_count = 0;
	g_lsp_mock.definition_result_code = 1;
	g_lsp_mock.implementation_locations = NULL;
	g_lsp_mock.implementation_location_count = 0;
	g_lsp_mock.implementation_result_code = 1;
	g_lsp_mock.implementation_response_configured = 0;
	g_lsp_mock.document_symbols = NULL;
	g_lsp_mock.document_symbol_count = 0;
	g_lsp_mock.document_symbol_result_code = 1;
	g_lsp_mock.diagnostics = NULL;
	g_lsp_mock.diagnostic_count = 0;
	free(g_lsp_mock.diagnostic_path);
	g_lsp_mock.diagnostic_path = NULL;
	g_lsp_mock.code_action_result_code = 0;
	g_lsp_mock.code_action_edits = NULL;
	g_lsp_mock.code_action_edit_count = 0;
	g_lsp_mock.completion_items = NULL;
	g_lsp_mock.completion_item_count = 0;
	g_lsp_mock.completion_pending_request_id = 0;
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
	editorLspClearStartupFailureReason();
	editorLspRegistryReset();
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
	editorLspFreeLocations(g_lsp_mock.definition_locations,
	                       g_lsp_mock.definition_location_count);
	g_lsp_mock.definition_locations = NULL;
	g_lsp_mock.definition_location_count = 0;
	g_lsp_mock.definition_result_code = result_code;

	if (locations == NULL || count <= 0) {
		return;
	}
	(void)editorLspCopyLocations(&g_lsp_mock.definition_locations,
	                             &g_lsp_mock.definition_location_count, locations, count);
}

void editorLspTestSetMockImplementationResponse(int result_code,
                                                const struct editorLspLocation *locations,
                                                int count) {
	editorLspFreeLocations(g_lsp_mock.implementation_locations,
	                       g_lsp_mock.implementation_location_count);
	g_lsp_mock.implementation_locations = NULL;
	g_lsp_mock.implementation_location_count = 0;
	g_lsp_mock.implementation_result_code = result_code;
	g_lsp_mock.implementation_response_configured = 1;

	if (locations == NULL || count <= 0) {
		return;
	}
	(void)editorLspCopyLocations(&g_lsp_mock.implementation_locations,
	                             &g_lsp_mock.implementation_location_count, locations, count);
}

void editorLspTestSetMockDocumentSymbolResponse(int result_code,
                                                const struct editorLspSymbol *symbols, int count) {
	editorLspFreeSymbols(g_lsp_mock.document_symbols, g_lsp_mock.document_symbol_count);
	g_lsp_mock.document_symbols = NULL;
	g_lsp_mock.document_symbol_count = 0;
	g_lsp_mock.document_symbol_result_code = result_code;

	if (symbols == NULL || count <= 0) {
		return;
	}
	(void)editorLspCopySymbols(&g_lsp_mock.document_symbols, &g_lsp_mock.document_symbol_count,
	                           symbols, count);
}

void editorLspTestSetMockDiagnostics(const char *path,
                                     const struct editorLspDiagnostic *diagnostics, int count) {
	editorLspFreeDiagnostics(g_lsp_mock.diagnostics, g_lsp_mock.diagnostic_count);
	g_lsp_mock.diagnostics = NULL;
	g_lsp_mock.diagnostic_count = 0;
	free(g_lsp_mock.diagnostic_path);
	g_lsp_mock.diagnostic_path = path != NULL ? strdup(path) : NULL;
	(void)editorLspCopyDiagnostics(&g_lsp_mock.diagnostics, &g_lsp_mock.diagnostic_count,
	                               diagnostics, count);
}

void editorLspTestSetMockCodeActionResult(int result_code, const struct editorLspDiagnostic *edits,
                                          int count) {
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

int editorLspTestParseDocumentSymbolResponse(const char *response_json,
                                             struct editorLspSymbol **symbols_out, int *count_out) {
	if (response_json == NULL) {
		return 0;
	}
	return editorLspParseDocumentSymbols(response_json, symbols_out, count_out);
}

int editorLspTestParseCompletionResponse(const char *response_json,
                                         struct editorLspCompletionItem **items_out,
                                         int *count_out) {
	if (response_json == NULL) {
		return 0;
	}
	return editorLspParseCompletionResponse(response_json, items_out, count_out);
}

void editorLspTestSetMockCompletionResponse(const struct editorLspCompletionItem *items,
                                            int count) {
	editorLspFreeCompletionItems(g_lsp_mock.completion_items, g_lsp_mock.completion_item_count);
	g_lsp_mock.completion_items = NULL;
	g_lsp_mock.completion_item_count = 0;
	if (items == NULL || count <= 0) {
		return;
	}
	(void)editorLspCopyCompletionItems(&g_lsp_mock.completion_items,
	                                   &g_lsp_mock.completion_item_count, items, count);
}

void editorLspTestDeliverPendingCompletion(void) {
	if (!g_lsp_mock.enabled) {
		return;
	}
	struct editorLspCompletionPending *pending = &editorLspPrimaryClient()->completion_pending;
	if (pending->request_id == 0) {
		return;
	}
	struct editorLspCompletionItem *items = NULL;
	int count = 0;
	(void)editorLspCopyCompletionItems(&items, &count, g_lsp_mock.completion_items,
	                                   g_lsp_mock.completion_item_count);
	int request_id = pending->request_id;
	int document_version = pending->document_version;
	int request_cy = pending->cy;
	int request_cx = pending->cx;
	int prefix_start_cx = pending->prefix_start_cx;
	char *prefix = pending->prefix != NULL ? strdup(pending->prefix) : NULL;
	char *filename = pending->filename != NULL ? strdup(pending->filename) : NULL;
	editorLspCompletionPendingClear(pending);
	g_lsp_mock.completion_pending_request_id = 0;

	struct editorAutocompleteResponseSink response = {
	        .request_id = request_id,
	        .document_version = document_version,
	        .request_cy = request_cy,
	        .request_cx = request_cx,
	        .prefix_start_cx = prefix_start_cx,
	        .prefix = prefix,
	        .filename = filename,
	        .items = items,
	        .count = count,
	};
	editorAutocompleteHandleCompletionResponse(&response);
	free(prefix);
	free(filename);
}

char *editorLspTestBuildInitializeRequestJson(int request_id, const char *root_uri,
                                              int process_id) {
	return editorLspBuildInitializeRequestJson(request_id, root_uri, process_id);
}
