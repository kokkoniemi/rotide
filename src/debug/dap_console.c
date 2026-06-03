#include "debug/dap_console.h"

#include "config/dap_config.h"
#include "debug/dap.h"
#include "editing/edit.h"
#include "rotide.h"
#include "terminal/terminal_pane.h"
#include "workspace/layout.h"
#include "workspace/tabs.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct editorDapConsolePane *dapConsolePaneNew(void) {
	return calloc(1, sizeof(struct editorDapConsolePane));
}

static void dapConsolePaneFree(void *payload) {
	free(payload);
}

struct editorDapConsolePane *editorDapConsoleForPane(const struct editorPaneNode *pane) {
	if (pane == NULL || pane->is_split) {
		return NULL;
	}
	if (editorPaneActiveKind(pane) != EDITOR_PANE_KIND_DEBUG_CONSOLE) {
		return NULL;
	}
	return (struct editorDapConsolePane *)editorTabPayloadAt(pane->as.leaf.view.active_tab_idx);
}

/* First member tab of `pane` whose kind matches, or -1. */
static int dapConsolePaneFindTabOfKind(const struct editorPaneNode *pane,
                                       enum editorPaneKind kind) {
	if (pane == NULL || pane->is_split) {
		return -1;
	}
	const struct editorPaneView *v = &pane->as.leaf.view;
	for (int i = 0; i < v->pane_tab_count; i++) {
		if (editorTabKindAt(v->pane_tabs[i]) == kind) {
			return v->pane_tabs[i];
		}
	}
	return -1;
}

/* First non-editor (console/terminal) member tab of `pane`, or -1. */
static int dapConsolePaneFindWidgetTab(const struct editorPaneNode *pane) {
	if (pane == NULL || pane->is_split) {
		return -1;
	}
	const struct editorPaneView *v = &pane->as.leaf.view;
	for (int i = 0; i < v->pane_tab_count; i++) {
		if (editorTabKindAt(v->pane_tabs[i]) != EDITOR_PANE_KIND_EDITOR) {
			return v->pane_tabs[i];
		}
	}
	return -1;
}

void editorDapConsoleCloseOwnedTerminalPane(void) {
	E.dap_terminal_leaf = NULL;
	struct editorPaneNode *leaf = E.dap_console_leaf;
	E.dap_console_leaf = NULL;
	if (leaf == NULL || E.layout_root == NULL) {
		return;
	}
	/* Close every console/terminal tab in the panel; the pane collapses once it
	 * holds no tabs. Containment is a pointer check, so it stays safe after the
	 * pane is freed by a collapsing close. */
	for (;;) {
		if (!editorPaneNodeContainsLeaf(E.layout_root, leaf)) {
			break;
		}
		int idx = dapConsolePaneFindWidgetTab(leaf);
		if (idx < 0 || !editorTabCloseAt(idx)) {
			break;
		}
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
		int console_idx = dapConsolePaneFindTabOfKind(E.dap_console_leaf,
		                                              EDITOR_PANE_KIND_DEBUG_CONSOLE);
		if (console_idx >= 0) {
			(void)editorTabSwitchToIndex(console_idx);
		}
		return 1;
	}
	struct editorPaneNode *sibling = editorLayoutSplitFocused(EDITOR_SPLIT_HORIZONTAL, 0.5);
	if (sibling == NULL) {
		editorSetStatusMsg("Could not open Debug Console");
		return 0;
	}
	struct editorDapConsolePane *console = dapConsolePaneNew();
	if (console == NULL || editorTabAdoptInPane(sibling, EDITOR_PANE_KIND_DEBUG_CONSOLE,
	                                            console, dapConsolePaneFree) < 0) {
		free(console);
		editorSetStatusMsg("Could not open Debug Console");
		return 0;
	}
	E.dap_console_leaf = sibling;
	editorPaneAnnounceFocus();
	return 1;
}

void editorDapConsoleScroll(struct editorDapConsolePane *console, int delta) {
	if (console == NULL) {
		return;
	}
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
	int scroll = console->scroll + delta;
	if (scroll < 0) {
		scroll = 0;
	}
	if (scroll > lines) {
		scroll = lines;
	}
	console->scroll = scroll;
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
	 * the panel also hosts a TERMINAL tab (a childless PTY for the debuggee's
	 * tty); the DEBUG_CONSOLE tab shows the dap_output transcript. The PTY is
	 * minted without forking a placeholder — rotide runs a worker thread and a
	 * fork that touches an inherited lock before exec can deadlock.
	 */
	struct editorPaneNode *sibling = editorLayoutSplitFocused(EDITOR_SPLIT_HORIZONTAL, 0.5);
	if (sibling == NULL) {
		/* Without a panel the session can still run; only a tty is unavailable. */
		editorSetStatusMsg("Could not open Debug Console");
		return wants_terminal ? 0 : 1;
	}
	struct editorDapConsolePane *console = dapConsolePaneNew();
	if (console == NULL || editorTabAdoptInPane(sibling, EDITOR_PANE_KIND_DEBUG_CONSOLE,
	                                            console, dapConsolePaneFree) < 0) {
		free(console);
		editorSetStatusMsg("Could not open Debug Console");
		return wants_terminal ? 0 : 1;
	}
	E.dap_console_leaf = sibling;

	if (wants_terminal) {
		struct editorRect rect = {0};
		int cols = 80;
		int rows = 24;
		if (editorLayoutFocusedLeafRect(&rect) && rect.w > 0 && rect.h > 0) {
			cols = rect.w;
			rows = rect.h;
		}
		char slave_path[PATH_MAX];
		struct editorTerminalPane *term = editorTerminalPaneCreateDetached(
		        cols, rows, slave_path, sizeof(slave_path));
		int term_idx = -1;
		if (term != NULL && editorDapLaunchSetStringField(config, "tty", slave_path)) {
			term_idx = editorTabCreateWidget(EDITOR_PANE_KIND_TERMINAL, term,
			                                 editorTerminalPaneFree);
		}
		if (term_idx < 0) {
			if (term != NULL) {
				editorTerminalPaneFree(term);
			}
			editorSetStatusMsg("Failed to set DAP console tty");
			editorDapConsoleCloseOwnedTerminalPane();
			return 0;
		}
		/* Terminal tab first (and active), Debug Console second. */
		(void)editorPaneViewInsertTabAt(&sibling->as.leaf.view, term_idx, 0);
		(void)editorTabSwitchToIndex(term_idx);
		if (tty_out != NULL && tty_out_size > 0) {
			(void)snprintf(tty_out, tty_out_size, "%s", slave_path);
		}
	}
	return 1;
}
