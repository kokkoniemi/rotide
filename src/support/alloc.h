#ifndef ALLOC_H
#define ALLOC_H

#include <stddef.h>

void *editorMalloc(size_t size);
void *editorRealloc(void *ptr, size_t size);

#endif
