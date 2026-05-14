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

#endif
