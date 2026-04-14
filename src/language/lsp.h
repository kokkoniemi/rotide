#ifndef LSP_H
#define LSP_H

#include "rotide.h"
#include "language/syntax.h"

#include <stddef.h>

struct editorLspLocation {
	char *path;
	int line;
	int character;
};

struct editorLspTestStats {
	int start_count;
	int shutdown_count;
	int did_open_count;
	int did_change_count;
	int did_save_count;
	int did_close_count;
	int definition_count;
	int code_action_count;
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

int editorLspEnsureDocumentOpen(const char *filename, enum editorSyntaxLanguage language,
		int *doc_open_in_out, int *doc_version_in_out,
		const char *full_text, size_t full_text_len);
int editorLspEnsureEslintDocumentOpen(const char *filename, enum editorSyntaxLanguage language,
		int *doc_open_in_out, int *doc_version_in_out,
		const char *full_text, size_t full_text_len);
int editorLspNotifyDidChange(const char *filename, enum editorSyntaxLanguage language,
		int *doc_open_in_out, int *doc_version_in_out,
		const struct editorSyntaxEdit *edit,
		const char *inserted_text, size_t inserted_text_len,
		const char *full_text, size_t full_text_len);
int editorLspNotifyEslintDidChange(const char *filename, enum editorSyntaxLanguage language,
		int *doc_open_in_out, int *doc_version_in_out,
		const struct editorSyntaxEdit *edit,
		const char *inserted_text, size_t inserted_text_len,
		const char *full_text, size_t full_text_len);
int editorLspNotifyDidSave(const char *filename, enum editorSyntaxLanguage language,
		int *doc_open_in_out, int *doc_version_in_out);
int editorLspNotifyEslintDidSave(const char *filename, enum editorSyntaxLanguage language,
		int *doc_open_in_out, int *doc_version_in_out);
void editorLspNotifyDidClose(const char *filename, enum editorSyntaxLanguage language,
		int *doc_open_in_out, int *doc_version_in_out);
void editorLspNotifyEslintDidClose(const char *filename, enum editorSyntaxLanguage language,
		int *doc_open_in_out, int *doc_version_in_out);

int editorLspRequestDefinition(const char *filename, enum editorSyntaxLanguage language, int line,
		int character, struct editorLspLocation **locations_out, int *count_out,
		int *timed_out_out);
int editorLspRequestCodeActionFixes(const char *filename, enum editorSyntaxLanguage language);
void editorLspFreeLocations(struct editorLspLocation *locations, int count);
int editorLspProtocolCharacterToBufferColumn(int line, int protocol_character);
enum editorLspStartupFailureReason editorLspLastStartupFailureReason(void);
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
void editorLspTestSetMockDiagnostics(const char *path, const struct editorLspDiagnostic *diagnostics,
		int count);
void editorLspTestSetMockCodeActionResult(int result_code,
		const struct editorLspDiagnostic *edits, int count);
int editorLspTestParseDefinitionResponse(const char *response_json,
		struct editorLspLocation **locations_out, int *count_out);
char *editorLspTestBuildInitializeRequestJson(int request_id, const char *root_uri,
		int process_id);

#endif
