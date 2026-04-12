#include "alloc.h"

#include <stdlib.h>

static editorAllocFailureProbe editor_alloc_failure_probe = NULL;

static int editorAllocShouldFail(void) {
	return editor_alloc_failure_probe != NULL && editor_alloc_failure_probe();
}

void editorAllocSetFailureProbe(editorAllocFailureProbe probe) {
	editor_alloc_failure_probe = probe;
}

void editorAllocClearFailureProbe(void) {
	editor_alloc_failure_probe = NULL;
}

void *editorMalloc(size_t size) {
	if (editorAllocShouldFail()) {
		return NULL;
	}

	return malloc(size);
}

void *editorRealloc(void *ptr, size_t size) {
	if (editorAllocShouldFail()) {
		return NULL;
	}

	return realloc(ptr, size);
}
