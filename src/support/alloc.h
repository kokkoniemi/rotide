#ifndef ROTIDE_SUPPORT_ALLOC_H
#define ROTIDE_SUPPORT_ALLOC_H

#include <stddef.h>

typedef int (*editorAllocFailureProbe)(void);

void editorAllocSetFailureProbe(editorAllocFailureProbe probe);
void editorAllocClearFailureProbe(void);

void *editorMalloc(size_t size);
void *editorRealloc(void *ptr, size_t size);

#endif
