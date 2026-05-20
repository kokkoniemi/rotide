#include "input/actions_terminal_debug.h"

#include "debug/dap.h"
#include "editing/edit.h"
#include "editing/history.h"
#include "terminal/terminal_pane.h"
#include "workspace/layout.h"

#include <stdlib.h>

static int actionsTerminalDebugOpenTerminalSplit(enum editorSplitOrientation split) {
	editorHistoryBreakGroup();
	const char *shell = getenv("SHELL");
	if (shell == NULL || shell[0] == '\0') {
		shell = "/bin/sh";
	}
	struct editorPaneNode *terminal_leaf = editorTerminalPaneOpenSplit(shell, split);
	if (terminal_leaf != NULL) {
		editorPaneAnnounceFocus();
		return 1;
	}
	editorSetStatusMsg("Failed to open terminal pane");
	return 1;
}

int editorHandleTerminalDebugMappedAction(enum editorAction action) {
	switch (action) {
		case EDITOR_ACTION_DAP_START:
			editorHistoryBreakGroup();
			(void)editorDapStartSelectedLaunch();
			return 1;
		case EDITOR_ACTION_DAP_STOP:
			editorHistoryBreakGroup();
			(void)editorDapStop();
			return 1;
		case EDITOR_ACTION_DAP_CONTINUE:
			editorHistoryBreakGroup();
			(void)editorDapContinue();
			return 1;
		case EDITOR_ACTION_DAP_PAUSE:
			editorHistoryBreakGroup();
			(void)editorDapPause();
			return 1;
		case EDITOR_ACTION_DAP_STEP_OVER:
			editorHistoryBreakGroup();
			(void)editorDapStepOver();
			return 1;
		case EDITOR_ACTION_DAP_STEP_INTO:
			editorHistoryBreakGroup();
			(void)editorDapStepInto();
			return 1;
		case EDITOR_ACTION_DAP_STEP_OUT:
			editorHistoryBreakGroup();
			(void)editorDapStepOut();
			return 1;
		case EDITOR_ACTION_DAP_TOGGLE_BREAKPOINT:
			editorHistoryBreakGroup();
			(void)editorDapToggleBreakpointAtCursor();
			return 1;
		case EDITOR_ACTION_TERMINAL_OPEN:
			return actionsTerminalDebugOpenTerminalSplit(EDITOR_SPLIT_HORIZONTAL);
		case EDITOR_ACTION_TERMINAL_OPEN_VERTICAL:
			return actionsTerminalDebugOpenTerminalSplit(EDITOR_SPLIT_VERTICAL);
		case EDITOR_ACTION_TERMINAL_PREFIX:
			E.terminal_prefix_armed = 1;
			editorSetStatusMsg("Terminal prefix armed: next key is rotide");
			return 1;
		default:
			return 0;
	}
}
