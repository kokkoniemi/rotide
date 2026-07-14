#ifndef ROTIDE_LANGUAGE_LSP_H
#define ROTIDE_LANGUAGE_LSP_H

#include "language/lsp_transport.h"
#include "language/syntax.h"
#include "rotide.h"

#include <stddef.h>

/* Public LSP value types are copied into tab-local state by request and
 * notification handlers. Positions use LSP line/character units at this API
 * boundary and are converted near editor buffer helpers.
 */
struct editorLspLocation {
	char *path;
	int line;
	int character;
};

struct editorLspSymbol {
	char *name;
	int kind;
	int line;
	int character;
	int depth;
	int parent_index;
	int is_last_sibling;
};

struct editorLspCompletionItem {
	char *label;
	char *filter_text;
	char *insert_text;
	int has_text_edit;
	int text_edit_start_line;
	int text_edit_start_character;
	int text_edit_end_line;
	int text_edit_end_character;
	char *text_edit_new_text;
};

struct editorLspTestStats {
	int start_count;
	int shutdown_count;
	int did_open_count;
	int did_change_count;
	int did_save_count;
	int did_close_count;
	int definition_count;
	int implementation_count;
	int references_count;
	int hover_count;
	int document_symbol_count;
	int code_action_count;
	int completion_count;
	int forward_search_count;
	int build_count;
};

struct editorLspTestLastChange {
	int had_range;
	int start_line;
	int start_character;
	int end_line;
	int end_character;
	int version;
	char text[64];
};

enum editorLspStartupFailureReason {
	EDITOR_LSP_STARTUP_FAILURE_NONE = 0,
	EDITOR_LSP_STARTUP_FAILURE_COMMAND_NOT_FOUND,
	EDITOR_LSP_STARTUP_FAILURE_OTHER
};

struct editorLspDiagnosticSummary {
	int count;
	int error_count;
	int warning_count;
};

void editorLspShutdown(void);
void editorLspPumpNotifications(void);

/* Document lifecycle notifications keep per-tab open flags and versions in
 * sync with the canonical editorDocument text.
 */
int editorLspEnsureDocumentOpen(const char *filename, enum editorSyntaxLanguage language,
                                int *doc_open_in_out, int *doc_version_in_out,
                                const char *full_text, size_t full_text_len);
int editorLspEnsureEslintDocumentOpen(const char *filename, enum editorSyntaxLanguage language,
                                      int *doc_open_in_out, int *doc_version_in_out,
                                      const char *full_text, size_t full_text_len);
int editorLspNotifyDidChange(const char *filename, enum editorSyntaxLanguage language,
                             int *doc_open_in_out, int *doc_version_in_out,
                             const struct editorSyntaxEdit *edit, const char *inserted_text,
                             size_t inserted_text_len, const char *full_text, size_t full_text_len);
int editorLspNotifyEslintDidChange(const char *filename, enum editorSyntaxLanguage language,
                                   int *doc_open_in_out, int *doc_version_in_out,
                                   const struct editorSyntaxEdit *edit, const char *inserted_text,
                                   size_t inserted_text_len, const char *full_text,
                                   size_t full_text_len);
int editorLspNotifyDidSave(const char *filename, enum editorSyntaxLanguage language,
                           int *doc_open_in_out, int *doc_version_in_out);
int editorLspNotifyEslintDidSave(const char *filename, enum editorSyntaxLanguage language,
                                 int *doc_open_in_out, int *doc_version_in_out);
void editorLspNotifyDidClose(const char *filename, enum editorSyntaxLanguage language,
                             int *doc_open_in_out, int *doc_version_in_out);
void editorLspNotifyEslintDidClose(const char *filename, enum editorSyntaxLanguage language,
                                   int *doc_open_in_out, int *doc_version_in_out);
void editorLspResetTrackedDocuments(void);

/* Request helpers synchronously or asynchronously query the active server and
 * return heap-owned result arrays for callers to store or free.
 */
int editorLspRequestDefinition(const char *filename, enum editorSyntaxLanguage language, int line,
                               int character, struct editorLspLocation **locations_out,
                               int *count_out, int *timed_out_out);
int editorLspRequestImplementation(const char *filename, enum editorSyntaxLanguage language,
                                   int line, int character,
                                   struct editorLspLocation **locations_out, int *count_out,
                                   int *timed_out_out);
int editorLspRequestReferences(const char *filename, enum editorSyntaxLanguage language, int line,
                               int character, struct editorLspLocation **locations_out,
                               int *count_out, int *timed_out_out);
int editorLspRequestHover(const char *filename, enum editorSyntaxLanguage language, int line,
                          int character, char **text_out, int *timed_out_out);
int editorLspRequestDocumentSymbols(const char *filename, enum editorSyntaxLanguage language,
                                    struct editorLspSymbol **symbols_out, int *count_out,
                                    int *timed_out_out);
void editorLspFreeSymbols(struct editorLspSymbol *symbols, int count);
int editorLspRequestForwardSearch(const char *filename, enum editorSyntaxLanguage language,
                                  int line, int character);
int editorLspRequestBuild(const char *filename, enum editorSyntaxLanguage language);

int editorLspRequestCompletionAsync(const char *filename, enum editorSyntaxLanguage language,
                                    int line, int character, int document_version,
                                    int prefix_start_cx, const char *prefix, int trigger_kind,
                                    int trigger_character);
void editorLspCancelCompletion(void);
int editorLspCompletionPendingActive(void);
int editorLspCompletionEnabledForFile(const char *filename, enum editorSyntaxLanguage language);
const char *editorLspCompletionTriggerCharsForFile(const char *filename,
                                                   enum editorSyntaxLanguage language);
void editorLspFreeCompletionItems(struct editorLspCompletionItem *items, int count);
const char *editorLspSymbolKindLabel(int kind);

void editorLspRefreshActiveDocumentSymbols(void);
void editorLspEnsureActiveDocumentTracked(void);
int editorLspRequestCodeActionFixes(const char *filename, enum editorSyntaxLanguage language);
void editorLspFreeLocations(struct editorLspLocation *locations, int count);
int editorLspProtocolCharacterToBufferColumn(int line, int protocol_character);
enum editorLspStartupFailureReason editorLspLastStartupFailureReason(void);
/* Test plumbing: reset the cached failure reason between mock test runs. */
void editorLspClearStartupFailureReason(void);
/*
 * Module-internal: ensure the client process for `filename`'s language is
 * running (real LSP) or has its mock counterpart marked alive (mock mode).
 * Declared here so siblings split out of lsp.c (lsp_features.c, ...) can
 * call them without copying lifecycle logic.
 */
int editorLspEnsureRunningForFile(const char *filename, enum editorSyntaxLanguage language);
int editorLspEnsureRunningEslintForFile(const char *filename, enum editorSyntaxLanguage language);
/*
 * Ensure-and-acquire: starts/finds the right (server_kind, workspace_root)
 * client via the registry and returns the live pointer, or NULL if the file
 * has no enabled LSP. Per-request and per-notification code should call one
 * of these instead of reading the registry-active pointer directly, so
 * client selection is explicit at each call site.
 */
struct editorLspClient *editorLspEnsureClientForFile(const char *filename,
                                                     enum editorSyntaxLanguage language);
struct editorLspClient *editorLspEnsureEslintClientForFile(const char *filename,
                                                           enum editorSyntaxLanguage language);
/*
 * Module-internal: server-kind detection and LSP language-id mapping for a
 * given file. Exposed so the document-tracking and request-building sibling
 * modules can build didOpen/didChange/didClose payloads without duplicating
 * the extension/language-id tables.
 */
enum editorLspServerKind editorLspServerKindForFile(const char *filename,
                                                    enum editorSyntaxLanguage language);
const char *editorLspLanguageIdForFile(const char *filename, enum editorSyntaxLanguage language);
int editorLspFileSupportsDefinition(const char *filename, enum editorSyntaxLanguage language);
int editorLspFileEnabled(const char *filename, enum editorSyntaxLanguage language);
int editorLspFileUsesEslint(const char *filename, enum editorSyntaxLanguage language);
int editorLspEslintEnabledForFile(const char *filename, enum editorSyntaxLanguage language);
const char *editorLspLanguageLabelForFile(const char *filename, enum editorSyntaxLanguage language);
const char *editorLspServerNameForFile(const char *filename, enum editorSyntaxLanguage language);
const char *editorLspCommandForFile(const char *filename, enum editorSyntaxLanguage language);
const char *editorLspCommandSettingNameForFile(const char *filename,
                                               enum editorSyntaxLanguage language);
int editorLspUsesSharedVscodeInstallPrompt(const char *filename,
                                           enum editorSyntaxLanguage language);
void editorLspClearDiagnosticsForFile(const char *filename);
void editorLspGetDiagnosticSummaryForFile(const char *filename,
                                          struct editorLspDiagnosticSummary *summary_out);

/* Test hooks */
void editorLspTestSetMockEnabled(int enabled);
void editorLspTestSetMockServerAlive(int alive);
void editorLspTestResetMock(void);
void editorLspTestGetStats(struct editorLspTestStats *out);
void editorLspTestGetLastChange(struct editorLspTestLastChange *out);
void editorLspTestGetLastDidOpenLanguageId(char *out, size_t out_size);
void editorLspTestSetMockDefinitionResponse(int result_code,
                                            const struct editorLspLocation *locations, int count);
void editorLspTestSetMockImplementationResponse(int result_code,
                                                const struct editorLspLocation *locations,
                                                int count);
void editorLspTestSetMockReferencesResponse(int result_code,
                                            const struct editorLspLocation *locations, int count);
void editorLspTestSetMockHoverResponse(int result_code, const char *text);
void editorLspTestSetMockDocumentSymbolResponse(int result_code,
                                                const struct editorLspSymbol *symbols, int count);
void editorLspTestSetMockForwardSearchResult(int result_code);
void editorLspTestSetMockBuildResult(int result_code);
void editorLspTestSetMockDiagnostics(const char *path,
                                     const struct editorLspDiagnostic *diagnostics, int count);
void editorLspTestSetMockCodeActionResult(int result_code, const struct editorLspDiagnostic *edits,
                                          int count);
void editorLspTestSetMockCompletionResponse(const struct editorLspCompletionItem *items, int count);
int editorLspTestParseDefinitionResponse(const char *response_json,
                                         struct editorLspLocation **locations_out, int *count_out);
int editorLspTestParseHoverResponse(const char *response_json, char **text_out);
int editorLspTestParseDocumentSymbolResponse(const char *response_json,
                                             struct editorLspSymbol **symbols_out, int *count_out);
int editorLspTestParseCompletionResponse(const char *response_json,
                                         struct editorLspCompletionItem **items_out,
                                         int *count_out);
char *editorLspTestBuildInitializeRequestJson(int request_id, const char *root_uri, int process_id,
                                              enum editorLspServerKind server_kind);
void editorLspTestDeliverPendingCompletion(void);

#endif
