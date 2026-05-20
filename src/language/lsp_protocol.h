#ifndef ROTIDE_LANGUAGE_LSP_PROTOCOL_H
#define ROTIDE_LANGUAGE_LSP_PROTOCOL_H

#include "language/lsp.h"
#include "language/lsp_json.h"
#include "language/lsp_transport.h"

#include <stddef.h>

struct editorLspPendingEdit {
	int start_line;
	int start_character;
	int end_line;
	int end_character;
	char *new_text;
};

int editorLspBuildFileUri(const char *path, char **uri_out);
char *editorLspDecodeFileUri(const char *uri);
char *editorLspBuildInitializeRequestJson(int request_id, const char *root_uri, int process_id);

int editorLspApplyPendingEdits(const struct editorLspPendingEdit *edits, int count);
int editorLspApplyPendingEditsWithClient(struct editorLspClient *client,
                                         const struct editorLspPendingEdit *edits, int count);

void editorLspSetDiagnosticsForPath(const char *path, const struct editorLspDiagnostic *diagnostics,
                                    int count);

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
