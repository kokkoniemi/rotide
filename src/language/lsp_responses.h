#ifndef ROTIDE_LANGUAGE_LSP_RESPONSES_H
#define ROTIDE_LANGUAGE_LSP_RESPONSES_H

#include "language/lsp.h"
#include "language/lsp_protocol.h"

/*
 * Pure response and result parsers for LSP JSON-RPC responses.
 *
 * No process or transport ownership lives in this module. Each function
 * takes a JSON string and writes parsed results into caller-owned
 * out-parameters; freeing the parsed result is the caller's responsibility
 * via the matching Free/Copy helpers declared below.
 *
 * Lifecycle helpers for parsed result types also live here so the response
 * parsing module is self-contained: callers don't need to reach back into
 * lsp_protocol for cleanup of these parsed structures.
 */

int editorLspParseCompletionProviderInResponse(const char *response_json, int *supported_out,
                                               char **trigger_chars_out);
int editorLspParseCompletionResponse(const char *response_json,
                                     struct editorLspCompletionItem **items_out, int *count_out);
int editorLspCopyCompletionItems(struct editorLspCompletionItem **out_items, int *out_count,
                                 const struct editorLspCompletionItem *items, int count);

int editorLspParseDefinitionLocations(const char *response_json,
                                      struct editorLspLocation **locations_out, int *count_out);
int editorLspCopyLocations(struct editorLspLocation **out_locations, int *out_count,
                           const struct editorLspLocation *locations, int count);

int editorLspParseDocumentSymbols(const char *response_json, struct editorLspSymbol **symbols_out,
                                  int *count_out);
int editorLspCopySymbols(struct editorLspSymbol **out_symbols, int *out_count,
                         const struct editorLspSymbol *symbols, int count);

int editorLspParseWorkspaceEditChanges(const char *edit_json, const char *target_path,
                                       struct editorLspPendingEdit **edits_out, int *count_out);
void editorLspFreePendingEdits(struct editorLspPendingEdit *edits, int count);

void editorLspFreeDiagnostics(struct editorLspDiagnostic *diagnostics, int count);
int editorLspCopyDiagnostics(struct editorLspDiagnostic **out_diagnostics, int *out_count,
                             const struct editorLspDiagnostic *diagnostics, int count);

#endif
