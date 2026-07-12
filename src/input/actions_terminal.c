#include "input/actions_terminal.h"

#include "editing/edit.h"
#include "editing/history.h"
#include "rotide.h"
#include "terminal/terminal_pane.h"
#include "workspace/layout.h"
#include "workspace/tabs.h"

#include <stdlib.h>

static const char *actionsTerminalShell(void) {
	const char *shell = getenv("SHELL");
	if (shell == NULL || shell[0] == '\0') {
		shell = "/bin/sh";
	}
	return shell;
}

static int actionsTerminalOpenSplit(enum editorSplitOrientation split) {
	editorHistoryBreakGroup();
	struct editorPaneNode *terminal_leaf =
	        editorTerminalPaneOpenSplit(actionsTerminalShell(), split);
	if (terminal_leaf != NULL) {
		editorPaneAnnounceFocus();
		return 1;
	}
	editorSetStatusMsg("Failed to open terminal pane");
	return 1;
}

static int actionsTerminalNewTab(void) {
	editorHistoryBreakGroup();
	if (editorTabNewTerminalBesideActive(actionsTerminalShell()) < 0) {
		editorSetStatusMsg("Failed to open terminal tab");
	}
	return 1;
}

int editorHandleTerminalMappedAction(enum editorAction action) {
	switch (action) {
		case EDITOR_ACTION_TERMINAL_OPEN:
			return actionsTerminalOpenSplit(EDITOR_SPLIT_HORIZONTAL);
		case EDITOR_ACTION_TERMINAL_OPEN_VERTICAL:
			return actionsTerminalOpenSplit(EDITOR_SPLIT_VERTICAL);
		case EDITOR_ACTION_TERMINAL_NEW_TAB:
			return actionsTerminalNewTab();
		case EDITOR_ACTION_TERMINAL_MODE_NORMAL: {
			struct editorTerminalPane *normal_term =
			        editorTerminalPaneForPane(E.focused_leaf);
			if (normal_term != NULL) {
				normal_term->input_mode = EDITOR_TERMINAL_INPUT_NORMAL;
				editorTerminalPaneResetPendingInput(normal_term);
			}
			return 1;
		}
		case EDITOR_ACTION_TERMINAL_MODE_INSERT: {
			struct editorTerminalPane *insert_term =
			        editorTerminalPaneForPane(E.focused_leaf);
			if (insert_term != NULL) {
				insert_term->input_mode = EDITOR_TERMINAL_INPUT_INSERT;
				editorTerminalPaneResetPendingInput(insert_term);
			}
			return 1;
		}
		default:
			return 0;
	}
}
