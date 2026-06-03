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
