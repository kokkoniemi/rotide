#ifndef ROTIDE_DEBUG_DAP_BREAKPOINTS_H
#define ROTIDE_DEBUG_DAP_BREAKPOINTS_H

#include "debug/dap.h"

enum editorDapBreakpointToggleResult {
	EDITOR_DAP_BREAKPOINT_TOGGLE_INVALID = 0,
	EDITOR_DAP_BREAKPOINT_TOGGLE_SET,
	EDITOR_DAP_BREAKPOINT_TOGGLE_REMOVED,
	EDITOR_DAP_BREAKPOINT_TOGGLE_TOO_MANY
};

int editorDapBreakpointsCount(void);
const struct editorDapBreakpoint *editorDapBreakpointsAt(int idx);
int editorDapBreakpointsPathWasSeenBefore(int idx);
char *editorDapBreakpointsBuildSetRequestJson(int seq, const char *path);
enum editorDapBreakpointToggleResult editorDapBreakpointsTogglePathLine(const char *path, int line);

#endif
