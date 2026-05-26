#include "editing/document_bridge.h"

#include "support/alloc.h"
#include "text/document.h"
#include "rotide.h"

#include <stdlib.h>

static int g_document_bridge_document_full_rebuild_count = 0;
static int g_document_bridge_document_incremental_update_count = 0;
static int g_document_bridge_text_tree_full_rebuild_count = 0;
static int g_document_bridge_text_tree_incremental_update_count = 0;

int editorTabKindSupportsDocument(enum editorTabKind tab_kind) {
	return tab_kind == EDITOR_TAB_FILE || tab_kind == EDITOR_TAB_TASK_LOG ||
	       tab_kind == EDITOR_TAB_UNSUPPORTED_FILE || tab_kind == EDITOR_TAB_GIT_DIFF;
}

void editorDocumentFreePtr(struct editorDocument **document_in_out) {
	if (document_in_out == NULL || *document_in_out == NULL) {
		return;
	}
	editorDocumentFree(*document_in_out);
	free(*document_in_out);
	*document_in_out = NULL;
}

static struct editorDocument *documentBridgeAllocDocument(void) {
	struct editorDocument *document = editorMalloc(sizeof(*document));
	if (document == NULL) {
		return NULL;
	}
	editorDocumentInit(document);
	return document;
}

static int documentBridgeResetStateFromText(struct editorDocument **document_in_out,
                                            enum editorTabKind tab_kind, const char *text,
                                            size_t len) {
	if (document_in_out == NULL) {
		return 0;
	}
	if (!editorTabKindSupportsDocument(tab_kind)) {
		editorDocumentFreePtr(document_in_out);
		return 1;
	}
	if (*document_in_out == NULL) {
		*document_in_out = documentBridgeAllocDocument();
		if (*document_in_out == NULL) {
			return 0;
		}
	}
	if (!editorDocumentResetFromString(*document_in_out, text, len)) {
		return 0;
	}
	editorDocumentStatsRecordFullRebuild();
	return 1;
}

static int documentBridgeEnsureForTab(enum editorTabKind tab_kind,
                                      struct editorDocument **document_in_out) {
	if (document_in_out == NULL) {
		return 0;
	}
	if (!editorTabKindSupportsDocument(tab_kind)) {
		editorDocumentFreePtr(document_in_out);
		return 1;
	}
	if (*document_in_out != NULL) {
		return 1;
	}
	return documentBridgeResetStateFromText(document_in_out, tab_kind, "", 0);
}

int editorDocumentResetActiveFromText(const char *text, size_t len) {
	return documentBridgeResetStateFromText(&E.document, E.tab_kind, text, len);
}

int editorDocumentEnsureActiveCurrent(void) {
	return documentBridgeEnsureForTab(E.tab_kind, &E.document) && E.document != NULL;
}

int editorBufferDocumentEnsureCurrent(struct editorBuffer *buffer) {
	if (buffer == NULL) {
		return 0;
	}
	return documentBridgeEnsureForTab(buffer->tab_kind, &buffer->document) &&
	       buffer->document != NULL;
}

int editorTabDocumentEnsureCurrent(struct editorTabState *tab) {
	return editorBufferDocumentEnsureCurrent(tab != NULL ? &tab->buffer : NULL);
}

void editorDocumentStatsReset(void) {
	g_document_bridge_document_full_rebuild_count = 0;
	g_document_bridge_document_incremental_update_count = 0;
}

void editorDocumentStatsRecordFullRebuild(void) {
	g_document_bridge_document_full_rebuild_count++;
}

void editorDocumentStatsRecordIncrementalUpdate(void) {
	g_document_bridge_document_incremental_update_count++;
}

int editorDocumentStatsFullRebuildCount(void) {
	return g_document_bridge_document_full_rebuild_count;
}

int editorDocumentStatsIncrementalUpdateCount(void) {
	return g_document_bridge_document_incremental_update_count;
}

void editorTextTreeStatsReset(void) {
	g_document_bridge_text_tree_full_rebuild_count = 0;
	g_document_bridge_text_tree_incremental_update_count = 0;
}

void editorTextTreeStatsRecordFullRebuild(void) {
	g_document_bridge_text_tree_full_rebuild_count++;
}

void editorTextTreeStatsRecordIncrementalUpdate(void) {
	g_document_bridge_text_tree_incremental_update_count++;
}

int editorTextTreeStatsFullRebuildCount(void) {
	return g_document_bridge_text_tree_full_rebuild_count;
}

int editorTextTreeStatsIncrementalUpdateCount(void) {
	return g_document_bridge_text_tree_incremental_update_count;
}
