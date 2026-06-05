#include "debug/dap_breakpoints.h"

#include "debug/dap.h"
#include "debug/dap_protocol.h"
#include "rotide.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

int editorDapHasBreakpoint(const char *path, int line) {
	if (path == NULL || path[0] == '\0') {
		return -1;
	}
	for (int i = 0; i < E.dap_breakpoint_count; i++) {
		if (E.dap_breakpoints[i].line == line &&
		    strcmp(E.dap_breakpoints[i].path, path) == 0) {
			return i;
		}
	}
	return -1;
}

int editorDapBreakpointsPathWasSeenBefore(int idx) {
	if (idx < 0 || idx >= E.dap_breakpoint_count) {
		return 0;
	}
	for (int i = 0; i < idx; i++) {
		if (strcmp(E.dap_breakpoints[i].path, E.dap_breakpoints[idx].path) == 0) {
			return 1;
		}
	}
	return 0;
}

char *editorDapBreakpointsBuildSetRequestJson(int seq, const char *path) {
	return editorDapBuildSetBreakpointsRequestJson(seq, path, E.dap_breakpoints,
	                                               E.dap_breakpoint_count);
}

enum editorDapBreakpointToggleResult editorDapBreakpointsTogglePathLine(const char *path,
                                                                        int line) {
	if (path == NULL || path[0] == '\0' || line < 0) {
		return EDITOR_DAP_BREAKPOINT_TOGGLE_INVALID;
	}
	int idx = editorDapHasBreakpoint(path, line);
	if (idx >= 0) {
		for (int i = idx; i + 1 < E.dap_breakpoint_count; i++) {
			E.dap_breakpoints[i] = E.dap_breakpoints[i + 1];
		}
		E.dap_breakpoint_count--;
		return EDITOR_DAP_BREAKPOINT_TOGGLE_REMOVED;
	}
	if (E.dap_breakpoint_count >= ROTIDE_DAP_MAX_BREAKPOINTS || strlen(path) >= PATH_MAX) {
		return EDITOR_DAP_BREAKPOINT_TOGGLE_TOO_MANY;
	}
	struct editorDapBreakpoint *bp = &E.dap_breakpoints[E.dap_breakpoint_count++];
	memset(bp, 0, sizeof(*bp));
	bp->kind = EDITOR_DAP_BREAKPOINT_LINE;
	(void)snprintf(bp->path, sizeof(bp->path), "%s", path);
	bp->line = line;
	return EDITOR_DAP_BREAKPOINT_TOGGLE_SET;
}
