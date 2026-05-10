#ifndef LSP_PROTOCOL_H
#define LSP_PROTOCOL_H

#include "language/lsp.h"
#include "language/lsp_transport.h"

#include <stddef.h>

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

int editorLspStringAppend(struct editorLspString *sb, const char *text);
int editorLspStringAppendJsonEscaped(struct editorLspString *sb, const char *text, size_t len);
int editorLspStringAppendf(struct editorLspString *sb, const char *fmt, ...)
		__attribute__((format(printf, 2, 3)));

int editorLspBuildFileUri(const char *path, char **uri_out);
char *editorLspBuildInitializeRequestJson(int request_id, const char *root_uri, int process_id);

const char *editorLspSkipWs(const char *p);
const char *editorLspFindJsonObjectEnd(const char *object_start);
int editorLspFindStringField(const char *json, const char *field_name, char **value_out);
int editorLspExtractResponseId(const char *json, int *id_out);
int editorLspResponseHasError(const char *json);

int editorLspParseDefinitionLocations(const char *response_json,
		struct editorLspLocation **locations_out, int *count_out);
int editorLspCopyLocations(struct editorLspLocation **out_locations, int *out_count,
		const struct editorLspLocation *locations, int count);

int editorLspParseWorkspaceEditChanges(const char *edit_json, const char *target_path,
		struct editorLspPendingEdit **edits_out, int *count_out);
void editorLspFreePendingEdits(struct editorLspPendingEdit *edits, int count);
int editorLspApplyPendingEdits(const struct editorLspPendingEdit *edits, int count);
int editorLspApplyPendingEditsWithClient(struct editorLspClient *client,
		const struct editorLspPendingEdit *edits, int count);

void editorLspFreeDiagnostics(struct editorLspDiagnostic *diagnostics, int count);
int editorLspCopyDiagnostics(struct editorLspDiagnostic **out_diagnostics, int *out_count,
		const struct editorLspDiagnostic *diagnostics, int count);
void editorLspSetDiagnosticsForPath(const char *path,
		const struct editorLspDiagnostic *diagnostics, int count);

int editorLspProcessIncomingMessage(struct editorLspClient *client, const char *message);

int editorLspUtf8ColumnToUtf16Units(const char *text, size_t text_len, int byte_column);
int editorLspUtf16UnitsToUtf8Column(const char *text, size_t text_len, int utf16_units);
int editorLspReadActiveLineText(int line, char **text_out, size_t *len_out);
int editorLspProtocolCharacterFromBufferColumn(int line, int byte_column);
int editorLspClientProtocolCharacterFromBufferColumn(struct editorLspClient *client, int line,
		int byte_column);
int editorLspClientProtocolCharacterToBufferColumn(struct editorLspClient *client, int line,
		int protocol_character);

#endif
