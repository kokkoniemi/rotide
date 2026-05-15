#include "input/actions_file_tab.h"

#include "config/common.h"
#include "config/keymap.h"
#include "debug/dap.h"
#include "editing/edit.h"
#include "editing/history.h"
#include "language/lsp.h"
#include "language/syntax_worker.h"
#include "support/terminal.h"
#include "workspace/recovery.h"
#include "workspace/task.h"
#include "workspace/tabs.h"
#include "workspace/workspace_state.h"

#include <stdlib.h>

static int quit_confirmed = 0;
static int quit_task_confirmed = 0;

static void editorSetQuitConfirmStatus(void) {
	char quit_binding[24];
	if (editorKeymapFormatBinding(&E.keymap, EDITOR_ACTION_QUIT, quit_binding,
				sizeof(quit_binding))) {
		editorSetStatusMsg("File has unsaved changes. Press %s again to quit", quit_binding);
		return;
	}

	editorSetStatusMsg("File has unsaved changes. Press quit key again to quit");
}

static void editorSetQuitTaskConfirmStatus(void) {
	char quit_binding[24];
	if (editorKeymapFormatBinding(&E.keymap, EDITOR_ACTION_QUIT, quit_binding,
				sizeof(quit_binding))) {
		editorSetStatusMsg("Task is still running. Press %s again to terminate it and quit",
				quit_binding);
		return;
	}
	editorSetStatusMsg("Task is still running. Press quit key again to terminate it and quit");
}

static void editorSetCloseTabConfirmStatus(void) {
	char close_binding[24];
	if (editorKeymapFormatBinding(&E.keymap, EDITOR_ACTION_CLOSE_TAB, close_binding,
				sizeof(close_binding))) {
		editorSetStatusMsg("Tab has unsaved changes. Press %s again to close tab", close_binding);
		return;
	}

	editorSetStatusMsg("Tab has unsaved changes. Press close key again to close tab");
}

static void editorSetCloseTaskConfirmStatus(void) {
	char close_binding[24];
	if (editorKeymapFormatBinding(&E.keymap, EDITOR_ACTION_CLOSE_TAB, close_binding,
				sizeof(close_binding))) {
		editorSetStatusMsg("Task is still running. Press %s again to terminate it and close tab",
				close_binding);
		return;
	}
	editorSetStatusMsg("Task is still running. Press close key again to terminate it and close tab");
}

void editorActionQuit(void) {
	if (editorTaskIsRunning() && !quit_task_confirmed) {
		editorSetQuitTaskConfirmStatus();
		quit_task_confirmed = 1;
		return;
	}
	if (editorTaskIsRunning()) {
		(void)editorTaskTerminate();
		quit_task_confirmed = 0;
	}

	if (editorTabAnyDirty() && !quit_confirmed) {
		editorSetQuitConfirmStatus();
		quit_confirmed = 1;
		return;
	}

	(void)editorWorkspaceStateSave();
	editorDapShutdown();
	editorLspShutdown();
	editorSyntaxBackgroundStop();
	editorRecoveryCleanupOnCleanExit();
	editorRestoreTerminal();
	editorClearScreen();
	editorResetCursorPos();

	exit(EXIT_SUCCESS);
}

void editorOpenSettings(void) {
	enum editorConfigBootstrapStatus bootstrap = editorConfigEnsureGlobalConfig();
	char *path = editorConfigBuildGlobalConfigPath();
	if (path == NULL || bootstrap == EDITOR_CONFIG_BOOTSTRAP_FAILED) {
		free(path);
		editorSetStatusMsg("Could not open ~/.rotide/config.toml");
		return;
	}
	if (!editorTabOpenOrSwitchToFile(path)) {
		editorSetStatusMsg("Could not open %s", path);
	}
	free(path);
	E.pane_focus = EDITOR_PANE_TEXT;
}

void editorActionCloseTab(void) {
	if (editorActiveTaskTabIsRunning() && !E.close_confirmed) {
		editorSetCloseTaskConfirmStatus();
		E.close_confirmed = 1;
		return;
	}
	if (editorActiveTaskTabIsRunning()) {
		(void)editorTaskTerminate();
		E.close_confirmed = 0;
	}

	if (E.dirty && !E.close_confirmed) {
		editorSetCloseTabConfirmStatus();
		E.close_confirmed = 1;
		return;
	}

	if (editorTabCloseActive()) {
		E.close_confirmed = 0;
	}
}

int editorHandleFileTabMappedAction(enum editorAction action) {
	switch (action) {
	case EDITOR_ACTION_QUIT:
		editorHistoryBreakGroup();
		editorActionQuit();
		return 1;
	case EDITOR_ACTION_SAVE:
		editorHistoryBreakGroup();
		editorSave();
		return 1;
	case EDITOR_ACTION_NEW_TAB:
		editorHistoryBreakGroup();
		(void)editorTabNewEmpty();
		return 1;
	case EDITOR_ACTION_CLOSE_TAB:
		editorHistoryBreakGroup();
		editorActionCloseTab();
		return 1;
	case EDITOR_ACTION_NEXT_TAB:
		editorHistoryBreakGroup();
		(void)editorTabSwitchByDelta(1);
		return 1;
	case EDITOR_ACTION_PREV_TAB:
		editorHistoryBreakGroup();
		(void)editorTabSwitchByDelta(-1);
		return 1;
	case EDITOR_ACTION_OPEN_SETTINGS:
		editorHistoryBreakGroup();
		editorOpenSettings();
		return 1;
	default:
		return 0;
	}
}

void editorFileTabActionsAfterKeypress(int mapped_action, enum editorAction action) {
	if (!mapped_action || action != EDITOR_ACTION_CLOSE_TAB) {
		E.close_confirmed = 0;
	}
	if (!mapped_action || action != EDITOR_ACTION_QUIT) {
		quit_confirmed = 0;
		quit_task_confirmed = 0;
	}
}
