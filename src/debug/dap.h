#ifndef LANGUAGE_DAP_H
#define LANGUAGE_DAP_H

#include "rotide.h"

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
