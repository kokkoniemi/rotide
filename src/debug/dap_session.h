#ifndef ROTIDE_DEBUG_DAP_SESSION_H
#define ROTIDE_DEBUG_DAP_SESSION_H

#include <sys/types.h>

struct editorDapSessionProcess {
	pid_t pid;
	int to_adapter_fd;
	int from_adapter_fd;
};

void editorDapSessionProcessReset(struct editorDapSessionProcess *process);
int editorDapSessionProcessStart(struct editorDapSessionProcess *process, const char *command);
void editorDapSessionProcessShutdown(struct editorDapSessionProcess *process);

#endif
