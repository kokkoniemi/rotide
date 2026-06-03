#ifndef ROTIDE_DEBUG_DAP_H
#define ROTIDE_DEBUG_DAP_H

#include <limits.h>
#include <stddef.h>

#define ROTIDE_DAP_MAX_ADAPTERS 16
#define ROTIDE_DAP_MAX_CONFIGS 16
#define ROTIDE_DAP_MAX_FIELDS 32
#define ROTIDE_DAP_MAX_ENV 32
#define ROTIDE_DAP_MAX_STRING_ARRAY_ITEMS 32
#define ROTIDE_DAP_ID_MAX 64
#define ROTIDE_DAP_NAME_MAX 128
#define ROTIDE_DAP_KEY_MAX 64
#define ROTIDE_DAP_VALUE_MAX 1024
#define ROTIDE_DAP_OUTPUT_MAX 4096
#define ROTIDE_DAP_MAX_BREAKPOINTS 128
#define ROTIDE_DAP_MAX_THREADS 32
#define ROTIDE_DAP_MAX_STACK_FRAMES 128
#define ROTIDE_DAP_MAX_SCOPES 64
#define ROTIDE_DAP_MAX_VARIABLES 256

enum editorDapLaunchValueKind {
	EDITOR_DAP_LAUNCH_VALUE_STRING = 0,
	EDITOR_DAP_LAUNCH_VALUE_BOOL,
	EDITOR_DAP_LAUNCH_VALUE_INT,
	EDITOR_DAP_LAUNCH_VALUE_STRING_ARRAY
};

struct editorDapAdapterConfig {
	char id[ROTIDE_DAP_ID_MAX];
	char command[PATH_MAX];
};

typedef char editorDapStringArrayItem[ROTIDE_DAP_VALUE_MAX];

struct editorDapLaunchField {
	char key[ROTIDE_DAP_KEY_MAX];
	enum editorDapLaunchValueKind kind;
	char string_value[ROTIDE_DAP_VALUE_MAX];
	int int_value;
	int bool_value;
	editorDapStringArrayItem *array_values;
	int array_count;
};

struct editorDapEnvVar {
	char key[ROTIDE_DAP_KEY_MAX];
	char value[ROTIDE_DAP_VALUE_MAX];
};

struct editorDapLaunchConfig {
	char id[ROTIDE_DAP_ID_MAX];
	char name[ROTIDE_DAP_NAME_MAX];
	char adapter[ROTIDE_DAP_ID_MAX];
	char request[32];
	struct editorDapLaunchField fields[ROTIDE_DAP_MAX_FIELDS];
	int field_count;
	struct editorDapEnvVar env[ROTIDE_DAP_MAX_ENV];
	int env_count;
};

struct editorDapBreakpoint {
	char path[PATH_MAX];
	int line;
};

struct editorDapThread {
	int id;
	char name[ROTIDE_DAP_NAME_MAX];
};

struct editorDapStackFrame {
	int id;
	char name[ROTIDE_DAP_NAME_MAX];
	char path[PATH_MAX];
	int line;
	int column;
};

struct editorDapScope {
	int variables_reference;
	char name[ROTIDE_DAP_NAME_MAX];
};

struct editorDapVariable {
	int variables_reference;
	char name[ROTIDE_DAP_NAME_MAX];
	char value[ROTIDE_DAP_VALUE_MAX];
};

void editorDapLaunchFieldClear(struct editorDapLaunchField *field);
void editorDapLaunchConfigClear(struct editorDapLaunchConfig *config);
void editorDapLaunchConfigsClear(struct editorDapLaunchConfig *configs, int count);

void editorDapShutdown(void);
void editorDapPumpNotifications(void);
/* Read end of the adapter pipe, or -1 when no session is running. The input
 * loop polls this so adapter traffic (e.g. the async `initialized` event) wakes
 * the editor instead of waiting for an unrelated event. */
int editorDapAdapterReadFd(void);

int editorDapStartSelectedLaunch(void);
int editorDapStartLaunch(int launch_idx);
int editorDapStop(void);
int editorDapRestart(void);
/* Sends a REPL `evaluate` request for `expr`, scoped to the top stack frame when
 * stopped. The expression echo and result are appended to the DAP output stream.
 * Returns 1 if the request was sent. */
int editorDapEvaluate(const char *expr);
int editorDapContinue(void);
int editorDapPause(void);
int editorDapStepOver(void);
int editorDapStepInto(void);
int editorDapStepOut(void);
int editorDapToggleBreakpointAtCursor(void);
int editorDapToggleBreakpointAtLine(int line);
int editorDapHasBreakpoint(const char *path, int line);
/* Returns 1 if the debuggee is stopped at `line` (0-based) of `path` — i.e. the
 * top stack frame resolves to the same file and line. */
int editorDapIsStoppedLine(const char *path, int line);

char *editorDapBuildInitializeRequestJson(int seq, const char *adapter_id);
char *editorDapBuildSimpleCommandRequestJson(int seq, const char *command);
/* Builds the adapter spawn command, appending `--tty=<tty_path>` for gdb-family
 * adapters when a debuggee tty is in use (program output then bypasses the DAP
 * stream). Other adapters / no tty get the base command unchanged. */
void editorDapBuildAdapterCommand(const char *base, const char *tty_path, char *out,
                                  size_t out_size);
char *editorDapBuildEvaluateRequestJson(int seq, const char *expr, int frame_id,
                                        const char *context);
char *editorDapBuildLaunchRequestJson(int seq, const struct editorDapLaunchConfig *config,
                                      const char *workspace_root, const char *active_file);
int editorDapProcessIncomingMessage(const char *message);

/* editorDapPrepareTerminalConsole is declared in debug/dap_console.h. */

/*
 * Test-only handshake introspection/setup. Values returned by
 * editorDapSessionStateForTest mirror the internal session states:
 *   0 = idle, 1 = awaiting initialize response,
 *   2 = awaiting initialized event, 3 = running.
 * editorDapBeginSessionForTest seeds an in-progress session that has "sent"
 * `initialize` (seq 1) and queued `launch_json` (ownership transferred) to be
 * flushed when the initialize response is processed; `to_adapter_fd` receives
 * any outgoing frames. editorDapEndSessionForTest tears the session down
 * (frees the queued launch); the caller still owns and must close its fds.
 */
int editorDapSessionStateForTest(void);
void editorDapBeginSessionForTest(int to_adapter_fd, char *launch_json);
void editorDapEndSessionForTest(void);

#endif
