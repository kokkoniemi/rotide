#include "debug/dap_session.h"

#include "language/lsp_transport.h"

#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

void editorDapSessionProcessReset(struct editorDapSessionProcess *process) {
	process->pid = 0;
	process->to_adapter_fd = -1;
	process->from_adapter_fd = -1;
}

int editorDapSessionProcessStart(struct editorDapSessionProcess *process, const char *command) {
	editorDapSessionProcessReset(process);
	return editorLspSpawnProcess(command, &process->pid, &process->to_adapter_fd,
	                             &process->from_adapter_fd);
}

void editorDapSessionProcessShutdown(struct editorDapSessionProcess *process) {
	if (process->to_adapter_fd != -1) {
		close(process->to_adapter_fd);
	}
	if (process->from_adapter_fd != -1) {
		close(process->from_adapter_fd);
	}
	if (process->pid > 0) {
		int status = 0;
		pid_t waited = waitpid(process->pid, &status, WNOHANG);
		if (waited == 0) {
			(void)kill(process->pid, SIGTERM);
			(void)waitpid(process->pid, &status, 0);
		}
	}
	editorDapSessionProcessReset(process);
}
