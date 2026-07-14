#ifndef ROTIDE_LANGUAGE_LSP_MOCK_H
#define ROTIDE_LANGUAGE_LSP_MOCK_H

#include "language/lsp.h"
#include "language/lsp_protocol.h"
#include "language/lsp_transport.h"

/*
 * Test/mock backing state for the LSP layer.
 *
 * Outside src/language/, the mock is reached only via the editorLspTest*
 * setters/getters declared in lsp.h. Inside src/language/, production
 * code in lsp.c (and any sibling modules split out of it) reads the
 * fields of g_lsp_mock directly to short-circuit real protocol calls
 * when g_lsp_mock.enabled is non-zero.
 *
 * Keeping the struct in this header (rather than in lsp.h) ensures
 * non-language test/UI code cannot couple itself to mock internals.
 */
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
	int implementation_result_code;
	int implementation_response_configured;
	struct editorLspLocation *implementation_locations;
	int implementation_location_count;
	int references_result_code;
	int references_response_configured;
	struct editorLspLocation *references_locations;
	int references_location_count;
	int hover_result_code;
	int hover_response_configured;
	char *hover_text;
	int document_symbol_result_code;
	struct editorLspSymbol *document_symbols;
	int document_symbol_count;
	struct editorLspDiagnostic *diagnostics;
	int diagnostic_count;
	char *diagnostic_path;
	int code_action_result_code;
	struct editorLspPendingEdit *code_action_edits;
	int code_action_edit_count;
	struct editorLspCompletionItem *completion_items;
	int completion_item_count;
	int completion_pending_request_id;
	int forward_search_result_code;
	int build_result_code;
};

extern struct editorLspMockState g_lsp_mock;

#endif
