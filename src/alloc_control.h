#ifndef ALLOC_CONTROL_H
#define ALLOC_CONTROL_H

typedef int (*editorAllocFailureProbe)(void);

void editorAllocSetFailureProbe(editorAllocFailureProbe probe);
void editorAllocClearFailureProbe(void);

#endif
