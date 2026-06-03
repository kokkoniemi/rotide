#include "debug/dap_console.h"

#include "config/dap_config.h"
#include "debug/dap.h"
#include "editing/edit.h"
#include "rotide.h"
#include "terminal/terminal_pane.h"
#include "workspace/layout.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void editorDapConsoleCloseOwnedTerminalPane(void) {
	E.dap_terminal_leaf = NULL;
	struct editorPaneNode *leaf = E.dap_console_leaf;
	if (leaf == NULL) {
		return;
	}
	E.dap_console_leaf = NULL;
	E.dap_panel_tab = 0;
	E.dap_console_input_len = 0;
	E.dap_console_input[0] = '\0';
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
		/* Panel already open: focus it and show the Debug Console tab. */
		(void)editorLayoutSetFocusedLeaf(E.dap_console_leaf);
		E.dap_panel_tab = 1;
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
	E.dap_panel_tab = 1;
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

int editorDapPrepareTerminalConsole(struct editorDapLaunchConfig *config, char *tty_out,
                                    size_t tty_out_size) {
	if (tty_out != NULL && tty_out_size > 0) {
		tty_out[0] = '\0';
	}
	char console_value[ROTIDE_DAP_VALUE_MAX];
	int wants_terminal = editorDapLaunchGetStringField(config, "console", console_value,
	                                                   sizeof(console_value)) &&
	                     strcmp(console_value, "terminal") == 0;
	/* The `console` field is rotide-specific; never forward it to the adapter. */
	editorDapLaunchRemoveField(config, "console");

	/*
	 * Open the Debug Console panel as a bottom split. When console = "terminal"
	 * the panel also owns a childless-PTY terminal (the debuggee's tty) shown on
	 * a "Terminal" tab; the "Debug Console" tab shows the dap_output transcript.
	 * The PTY is minted without forking a placeholder — rotide runs a worker
	 * thread and a fork that touches an inherited lock before exec can deadlock.
	 */
	struct editorPaneNode *sibling = editorLayoutSplitFocused(EDITOR_SPLIT_HORIZONTAL, 0.5);
	if (sibling == NULL) {
		/* Without a panel the session can still run; only a tty is unavailable. */
		editorSetStatusMsg("Could not open Debug Console");
		return wants_terminal ? 0 : 1;
	}
	sibling->as.leaf.kind = EDITOR_PANE_KIND_DEBUG_CONSOLE;
	sibling->as.leaf.kind_state = NULL;
	sibling->as.leaf.kind_state_free = NULL;
	sibling->as.leaf.view.active_tab_idx = -1;

	if (wants_terminal) {
		struct editorRect rect = {0};
		int cols = 80;
		int rows = 24;
		if (editorLayoutFocusedLeafRect(&rect) && rect.w > 0 && rect.h > 1) {
			cols = rect.w;
			rows = rect.h - 1; /* the tab strip occupies one row */
		}
		char slave_path[PATH_MAX];
		struct editorTerminalPane *term = editorTerminalPaneCreateDetached(
		        cols, rows, slave_path, sizeof(slave_path));
		if (term == NULL || !editorDapLaunchSetStringField(config, "tty", slave_path)) {
			if (term != NULL) {
				editorTerminalPaneFree(term);
			}
			editorSetStatusMsg("Failed to set DAP console tty");
			(void)editorPaneTreeCloseLeaf(&E.layout_root, sibling);
			if (E.focused_leaf != NULL) {
				(void)editorPaneViewLoadIntoState(&E.focused_leaf->as.leaf.view);
			}
			return 0;
		}
		sibling->as.leaf.kind_state = term;
		sibling->as.leaf.kind_state_free = editorTerminalPaneFree;
		if (tty_out != NULL && tty_out_size > 0) {
			(void)snprintf(tty_out, tty_out_size, "%s", slave_path);
		}
	}

	E.dap_console_leaf = sibling;
	E.dap_console_scroll = 0;
	E.dap_panel_tab = wants_terminal ? 0 : 1;
	return 1;
}
