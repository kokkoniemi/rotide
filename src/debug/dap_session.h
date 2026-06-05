#ifndef ROTIDE_DEBUG_DAP_SESSION_H
#define ROTIDE_DEBUG_DAP_SESSION_H

#include <sys/types.h>

struct editorDapSessionProcess {
	pid_t pid;
	int to_adapter_fd;
	int from_adapter_fd;
};

enum editorDapSessionState {
	EDITOR_DAP_SESSION_IDLE = 0,
	EDITOR_DAP_SESSION_AWAIT_INITIALIZE_RESPONSE,
	EDITOR_DAP_SESSION_AWAIT_INITIALIZED_EVENT,
	EDITOR_DAP_SESSION_RUNNING,
};

/*
 * DAP launch ordering is load-bearing: `launch` follows the `initialize`
 * response, while breakpoints and `configurationDone` follow the `initialized`
 * event. Some adapters start the debuggee on `launch`, so sending it early can
 * lose breakpoints and let the program run to exit.
 */
struct editorDapSessionHandshake {
	int initialized;
	enum editorDapSessionState state;
	char *pending_launch_json;
};

void editorDapSessionProcessReset(struct editorDapSessionProcess *process);
int editorDapSessionProcessStart(struct editorDapSessionProcess *process, const char *command);
void editorDapSessionProcessShutdown(struct editorDapSessionProcess *process);

void editorDapSessionHandshakeReset(struct editorDapSessionHandshake *handshake);
void editorDapSessionHandshakeAwaitInitialize(struct editorDapSessionHandshake *handshake,
                                              char *launch_json);
char *editorDapSessionHandshakeConsumeLaunch(struct editorDapSessionHandshake *handshake);
int editorDapSessionHandshakeMarkInitialized(struct editorDapSessionHandshake *handshake);

#endif
