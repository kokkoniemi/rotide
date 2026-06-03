#include "debug/dap_console.h"

#include "config/dap_config.h"
#include "debug/dap.h"
#include "editing/edit.h"
#include "rotide.h"
#include "terminal/terminal_pane.h"
#include "workspace/layout.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

void editorDapConsoleCloseOwnedTerminalPane(void) {
	if (E.dap_terminal_leaf == NULL) {
		return;
	}
	struct editorPaneNode *leaf = E.dap_terminal_leaf;
	E.dap_terminal_leaf = NULL;
	if (E.layout_root == NULL || !editorPaneNodeContainsLeaf(E.layout_root, leaf)) {
		return; /* User already closed it. */
	}
	struct editorPaneNode *new_focus = editorPaneTreeCloseLeaf(&E.layout_root, leaf);
	if (new_focus != NULL) {
		E.focused_leaf = new_focus;
		(void)editorPaneViewLoadIntoState(&new_focus->as.leaf.view);
	}
}

int editorDapConsoleToggle(void) {
	if (E.layout_root == NULL) {
		return 0;
	}
	if (E.dap_console_leaf != NULL &&
	    editorPaneNodeContainsLeaf(E.layout_root, E.dap_console_leaf)) {
		struct editorPaneNode *leaf = E.dap_console_leaf;
		if (E.focused_leaf == leaf) {
			/* Toggle off: close the console pane. */
			E.dap_console_leaf = NULL;
			struct editorPaneNode *new_focus =
			        editorPaneTreeCloseLeaf(&E.layout_root, leaf);
			if (new_focus != NULL) {
				E.focused_leaf = new_focus;
				(void)editorPaneViewLoadIntoState(&new_focus->as.leaf.view);
			}
			return 1;
		}
		(void)editorLayoutSetFocusedLeaf(leaf);
		return 1;
	}
	struct editorPaneNode *sibling = editorLayoutSplitFocused(EDITOR_SPLIT_HORIZONTAL, 0.5);
	if (sibling == NULL) {
		editorSetStatusMsg("Could not open Debug Console");
		return 0;
	}
	/* editorLayoutSplitFocused already focused the new sibling; convert it to a
	 * console leaf with no tabs so focusing it never touches editor state. */
	sibling->as.leaf.kind = EDITOR_PANE_KIND_DEBUG_CONSOLE;
	sibling->as.leaf.kind_state = NULL;
	sibling->as.leaf.kind_state_free = NULL;
	sibling->as.leaf.view.active_tab_idx = -1;
	E.dap_console_leaf = sibling;
	E.dap_console_scroll = 0;
	editorPaneAnnounceFocus();
	return 1;
}

void editorDapConsoleScroll(int delta) {
	int lines = 0;
	if (E.dap_output_len > 0) {
		for (size_t i = 0; i < E.dap_output_len; i++) {
			if (E.dap_output[i] == '\n') {
				lines++;
			}
		}
		if (E.dap_output[E.dap_output_len - 1] != '\n') {
			lines++;
		}
	}
	int scroll = E.dap_console_scroll + delta;
	if (scroll < 0) {
		scroll = 0;
	}
	if (scroll > lines) {
		scroll = lines;
	}
	E.dap_console_scroll = scroll;
}

int editorDapPrepareTerminalConsole(struct editorDapLaunchConfig *config) {
	char console_value[ROTIDE_DAP_VALUE_MAX];
	if (!editorDapLaunchGetStringField(config, "console", console_value,
	                                   sizeof(console_value))) {
		return 1;
	}
	if (strcmp(console_value, "terminal") != 0) {
		/* Strip unsupported console values before forwarding to adapters. */
		editorDapLaunchRemoveField(config, "console");
		return 1;
	}
	/*
	 * Host the debuggee's tty in a terminal pane backed by a childless PTY. We
	 * deliberately do NOT fork a placeholder process to mint the PTY: rotide
	 * runs a background worker thread, and a fork+exec that touches an inherited
	 * lock before exec can deadlock the child, hanging the editor. The adapter
	 * opens the slave by path; rotide reads the master.
	 */
	char slave_path[PATH_MAX];
	struct editorPaneNode *terminal_leaf = editorTerminalPaneOpenSplitDetached(
	        EDITOR_SPLIT_HORIZONTAL, slave_path, sizeof(slave_path));
	if (terminal_leaf == NULL) {
		editorSetStatusMsg("Could not open terminal pane for DAP console");
		editorDapLaunchRemoveField(config, "console");
		return 0;
	}
	if (!editorDapLaunchSetStringField(config, "tty", slave_path)) {
		editorSetStatusMsg("Failed to set DAP console tty");
		(void)editorPaneTreeCloseLeaf(&E.layout_root, terminal_leaf);
		if (E.focused_leaf != NULL) {
			(void)editorPaneViewLoadIntoState(&E.focused_leaf->as.leaf.view);
		}
		editorDapLaunchRemoveField(config, "console");
		return 0;
	}
	E.dap_terminal_leaf = terminal_leaf;
	editorDapLaunchRemoveField(config, "console");
	return 1;
}
