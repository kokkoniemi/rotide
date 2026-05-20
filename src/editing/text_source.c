#include "editing/text_source.h"

#include "editing/document_bridge.h"
#include "language/syntax.h"
#include "support/alloc.h"
#include "support/size_utils.h"
#include "text/document.h"

#include <errno.h>
#include <stdlib.h>

static int g_active_text_source_build_count = 0;
static int g_active_text_source_dup_count = 0;

static const char *editorDocumentTextSourceRead(const struct editorTextSource *source,
                                                size_t byte_index, uint32_t *bytes_read) {
	const struct editorDocument *document = source != NULL ? source->context : NULL;
	return editorDocumentRead(document, byte_index, bytes_read);
}

int editorBuildActiveTextSource(struct editorTextSource *source_out) {
	if (source_out == NULL) {
		return 0;
	}
	g_active_text_source_build_count++;
	if (!editorTabKindSupportsDocument(E.tab_kind) || !editorDocumentEnsureActiveCurrent() ||
	    E.document == NULL) {
		return 0;
	}
	source_out->read = editorDocumentTextSourceRead;
	source_out->context = E.document;
	source_out->length = editorDocumentLength(E.document);
	return 1;
}

char *editorDupActiveTextSource(size_t *len_out) {
	struct editorTextSource source = {0};
	g_active_text_source_dup_count++;
	if (len_out != NULL) {
		*len_out = 0;
	}
	if (!editorBuildActiveTextSource(&source)) {
		if (errno == 0) {
			errno = EIO;
		}
		return NULL;
	}

	if (source.length > ROTIDE_MAX_TEXT_BYTES) {
		errno = EOVERFLOW;
		return NULL;
	}

	size_t cap = 0;
	if (!editorSizeAdd(source.length, 1, &cap)) {
		errno = EOVERFLOW;
		return NULL;
	}

	char *dup = editorMalloc(cap);
	if (dup == NULL) {
		errno = ENOMEM;
		if (len_out != NULL) {
			*len_out = source.length;
		}
		return NULL;
	}
	if (source.length > 0 && !editorTextSourceCopyRange(&source, 0, source.length, dup)) {
		free(dup);
		errno = EIO;
		return NULL;
	}
	dup[source.length] = '\0';
	if (len_out != NULL) {
		*len_out = source.length;
	}
	errno = 0;
	return dup;
}

void editorActiveTextSourceBuildTestResetCount(void) {
	g_active_text_source_build_count = 0;
}

int editorActiveTextSourceBuildTestCount(void) {
	return g_active_text_source_build_count;
}

void editorActiveTextSourceDupTestResetCount(void) {
	g_active_text_source_dup_count = 0;
}

int editorActiveTextSourceDupTestCount(void) {
	return g_active_text_source_dup_count;
}
