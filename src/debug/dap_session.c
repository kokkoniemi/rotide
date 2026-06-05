#include "debug/dap_session.h"

#include "language/lsp_transport.h"

#include <signal.h>
#include <stdlib.h>
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

void editorDapSessionHandshakeReset(struct editorDapSessionHandshake *handshake) {
	free(handshake->pending_launch_json);
	handshake->pending_launch_json = NULL;
	handshake->initialized = 0;
	handshake->state = EDITOR_DAP_SESSION_IDLE;
}

void editorDapSessionHandshakeAwaitInitialize(struct editorDapSessionHandshake *handshake,
                                              char *launch_json) {
	editorDapSessionHandshakeReset(handshake);
	handshake->pending_launch_json = launch_json;
	handshake->state = EDITOR_DAP_SESSION_AWAIT_INITIALIZE_RESPONSE;
}

char *editorDapSessionHandshakeConsumeLaunch(struct editorDapSessionHandshake *handshake) {
	char *launch_json = handshake->pending_launch_json;
	handshake->pending_launch_json = NULL;
	if (launch_json != NULL) {
		handshake->state = EDITOR_DAP_SESSION_AWAIT_INITIALIZED_EVENT;
	}
	return launch_json;
}

int editorDapSessionHandshakeMarkInitialized(struct editorDapSessionHandshake *handshake) {
	if (handshake->initialized) {
		return 0;
	}
	handshake->initialized = 1;
	handshake->state = EDITOR_DAP_SESSION_RUNNING;
	return 1;
}
