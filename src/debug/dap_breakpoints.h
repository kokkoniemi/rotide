#ifndef ROTIDE_DEBUG_DAP_BREAKPOINTS_H
#define ROTIDE_DEBUG_DAP_BREAKPOINTS_H

enum editorDapBreakpointToggleResult {
	EDITOR_DAP_BREAKPOINT_TOGGLE_INVALID = 0,
	EDITOR_DAP_BREAKPOINT_TOGGLE_SET,
	EDITOR_DAP_BREAKPOINT_TOGGLE_REMOVED,
	EDITOR_DAP_BREAKPOINT_TOGGLE_TOO_MANY
};

int editorDapBreakpointsPathWasSeenBefore(int idx);
char *editorDapBreakpointsBuildSetRequestJson(int seq, const char *path);
enum editorDapBreakpointToggleResult editorDapBreakpointsTogglePathLine(const char *path, int line);

#endif
