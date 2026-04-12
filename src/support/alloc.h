#ifndef ALLOC_H
#define ALLOC_H

#include <stddef.h>

typedef int (*editorAllocFailureProbe)(void);

void editorAllocSetFailureProbe(editorAllocFailureProbe probe);
void editorAllocClearFailureProbe(void);

void *editorMalloc(size_t size);
void *editorRealloc(void *ptr, size_t size);

#endif
