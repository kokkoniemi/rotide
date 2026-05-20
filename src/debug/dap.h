#ifndef LANGUAGE_DAP_H
#define LANGUAGE_DAP_H

#include <limits.h>

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

int editorDapStartSelectedLaunch(void);
int editorDapStartLaunch(int launch_idx);
int editorDapStop(void);
int editorDapContinue(void);
int editorDapPause(void);
int editorDapStepOver(void);
int editorDapStepInto(void);
int editorDapStepOut(void);
int editorDapToggleBreakpointAtCursor(void);
int editorDapHasBreakpoint(const char *path, int line);

char *editorDapBuildInitializeRequestJson(int seq, const char *adapter_id);
char *editorDapBuildSimpleCommandRequestJson(int seq, const char *command);
char *editorDapBuildLaunchRequestJson(int seq, const struct editorDapLaunchConfig *config,
                                      const char *workspace_root, const char *active_file);
int editorDapProcessIncomingMessage(const char *message);

/*
 * Inspects `config` for the rotide-specific `console` field. If the value
 * is "terminal", opens a terminal pane via the layout, resolves its slave
 * tty, sets `tty` in `config`, and records the pane in E.dap_terminal_leaf
 * so it can be closed when the DAP session ends. The `console` key is
 * always stripped from `config` so it doesn't reach the adapter. Returns
 * 1 on success (including when no console field is present), 0 if the
 * caller should abort the launch (terminal pane creation failed).
 *
 * Exposed primarily for tests and for any future "start launch" trigger
 * that wants the terminal-pane setup without spawning the adapter.
 */
int editorDapPrepareTerminalConsole(struct editorDapLaunchConfig *config);

#endif
