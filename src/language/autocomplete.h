#ifndef LANGUAGE_AUTOCOMPLETE_H
#define LANGUAGE_AUTOCOMPLETE_H

#include "language/lsp.h"

void editorAutocompleteShutdown(void);
void editorAutocompleteOnCharInserted(int ch);
void editorAutocompleteOnCursorMoved(void);
void editorAutocompleteCancel(void);

int editorAutocompleteIsVisible(void);
int editorAutocompleteAcceptSelection(void);

/*
 * Returns 1 when the popup is currently the autocomplete popup AND the supplied character
 * would extend the existing prefix (identifier byte or a server-declared trigger). Callers
 * can use this to bypass the popup's default dismiss-on-typing behavior so the popup can
 * narrow in place instead of disappearing and waiting for a fresh response.
 */
int editorAutocompleteWouldRefilter(int ch);

void editorAutocompleteHandleCompletionResponse(int request_id, int document_version,
                                                int request_cy, int request_cx, int prefix_start_cx,
                                                const char *prefix, const char *filename,
                                                struct editorLspCompletionItem *items, int count);

#endif
