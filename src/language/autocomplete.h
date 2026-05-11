#ifndef LANGUAGE_AUTOCOMPLETE_H
#define LANGUAGE_AUTOCOMPLETE_H

#include "language/lsp.h"

void editorAutocompleteShutdown(void);
void editorAutocompleteOnCharInserted(int ch);
void editorAutocompleteOnCursorMoved(void);
void editorAutocompleteCancel(void);

int editorAutocompleteIsVisible(void);
int editorAutocompleteAcceptSelection(void);

void editorAutocompleteHandleCompletionResponse(int request_id, int document_version,
		int request_cy, int request_cx, int prefix_start_cx, const char *prefix,
		const char *filename, struct editorLspCompletionItem *items, int count);

#endif
