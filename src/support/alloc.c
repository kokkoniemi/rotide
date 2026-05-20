#include "alloc.h"

#include <stdlib.h>

static editorAllocFailureProbe g_alloc_failure_probe = NULL;

static int allocShouldFail(void) {
	return g_alloc_failure_probe != NULL && g_alloc_failure_probe();
}

void editorAllocSetFailureProbe(editorAllocFailureProbe probe) {
	g_alloc_failure_probe = probe;
}

void editorAllocClearFailureProbe(void) {
	g_alloc_failure_probe = NULL;
}

void *editorMalloc(size_t size) {
	if (allocShouldFail()) {
		return NULL;
	}

	return malloc(size);
}

void *editorRealloc(void *ptr, size_t size) {
	if (allocShouldFail()) {
		return NULL;
	}

	return realloc(ptr, size);
}
