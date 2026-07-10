#include "input/actions_terminal.h"

#include "editing/edit.h"
#include "editing/history.h"
#include "rotide.h"
#include "terminal/terminal_pane.h"
#include "workspace/layout.h"

#include <stdlib.h>

static int actionsTerminalOpenSplit(enum editorSplitOrientation split) {
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

int editorHandleTerminalMappedAction(enum editorAction action) {
	switch (action) {
		case EDITOR_ACTION_TERMINAL_OPEN:
			return actionsTerminalOpenSplit(EDITOR_SPLIT_HORIZONTAL);
		case EDITOR_ACTION_TERMINAL_OPEN_VERTICAL:
			return actionsTerminalOpenSplit(EDITOR_SPLIT_VERTICAL);
		case EDITOR_ACTION_TERMINAL_PREFIX:
			E.terminal_prefix_armed = 1;
			editorSetStatusMsg("Terminal prefix armed: next key is rotide");
			return 1;
		case EDITOR_ACTION_TERMINAL_MODE_NORMAL: {
			/* Also the status-bar "Normal" button: a rotide-owned surface, so it
			 * works even when a fullscreen child holds the keyboard. */
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
