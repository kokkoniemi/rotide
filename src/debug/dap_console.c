#include "debug/dap_console.h"

#include "config/dap_config.h"
#include "debug/dap.h"
#include "editing/edit.h"
#include "rotide.h"
#include "terminal/terminal_pane.h"
#include "workspace/layout.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

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
	struct editorPaneNode *terminal_leaf =
	        editorTerminalPaneOpenSplit("sleep infinity", EDITOR_SPLIT_HORIZONTAL);
	if (terminal_leaf == NULL) {
		editorSetStatusMsg("Could not open terminal pane for DAP console");
		editorDapLaunchRemoveField(config, "console");
		return 0;
	}
	struct editorTerminalPane *tp =
	        (struct editorTerminalPane *)terminal_leaf->as.leaf.kind_state;
	const char *slave_path = NULL;
	if (tp != NULL && tp->child.master_fd >= 0) {
		slave_path = ptsname(tp->child.master_fd);
	}
	if (slave_path == NULL || !editorDapLaunchSetStringField(config, "tty", slave_path)) {
		editorSetStatusMsg("Failed to resolve terminal pane tty");
		(void)editorPaneTreeCloseLeaf(&E.layout_root, terminal_leaf);
		if (E.focused_leaf != NULL) {
			(void)editorPaneViewLoadIntoState(&E.focused_leaf->as.leaf.view);
		}
		editorDapLaunchRemoveField(config, "console");
		return 0;
	}
	E.dap_terminal_leaf = terminal_leaf;
	/* For DAP terminal hosting keep only the PTY; stop placeholder child. */
	if (tp != NULL && tp->child.pid > 0) {
		pid_t pid = tp->child.pid;
		(void)kill(pid, SIGTERM);
		(void)waitpid(pid, NULL, 0);
		tp->child.pid = -1;
	}
	editorDapLaunchRemoveField(config, "console");
	return 1;
}
