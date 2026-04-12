#ifndef LSP_H
#define LSP_H

#include "rotide.h"
#include "syntax.h"

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

void editorLspShutdown(void);

int editorLspEnsureDocumentOpen(const char *filename, enum editorSyntaxLanguage language,
		int *doc_open_in_out, int *doc_version_in_out,
		const char *full_text, size_t full_text_len);
int editorLspNotifyDidChange(const char *filename, enum editorSyntaxLanguage language,
		int *doc_open_in_out, int *doc_version_in_out,
		const struct editorSyntaxEdit *edit,
		const char *inserted_text, size_t inserted_text_len,
		const char *full_text, size_t full_text_len);
int editorLspNotifyDidSave(const char *filename, enum editorSyntaxLanguage language,
		int *doc_open_in_out, int *doc_version_in_out);
void editorLspNotifyDidClose(const char *filename, enum editorSyntaxLanguage language,
		int *doc_open_in_out, int *doc_version_in_out);

int editorLspRequestDefinition(const char *filename, int line, int character,
		struct editorLspLocation **locations_out, int *count_out, int *timed_out_out);
void editorLspFreeLocations(struct editorLspLocation *locations, int count);
int editorLspProtocolCharacterToBufferColumn(int line, int protocol_character);
enum editorLspStartupFailureReason editorLspLastStartupFailureReason(void);

/* Test hooks */
void editorLspTestSetMockEnabled(int enabled);
void editorLspTestSetMockServerAlive(int alive);
void editorLspTestResetMock(void);
void editorLspTestGetStats(struct editorLspTestStats *out);
void editorLspTestGetLastChange(struct editorLspTestLastChange *out);
void editorLspTestSetMockDefinitionResponse(int result_code,
		const struct editorLspLocation *locations, int count);

#endif
