#include "input/actions_file_tab.h"

#include "config/common.h"
#include "debug/dap.h"
#include "editing/edit.h"
#include "editing/history.h"
#include "language/lsp.h"
#include "language/syntax_worker.h"
#include "rotide.h"
#include "support/terminal.h"
#include "workspace/recovery.h"
#include "workspace/tabs.h"
#include "workspace/task.h"
#include "workspace/workspace_state.h"

#include <stdlib.h>

static int g_actions_file_tab_quit_confirmed = 0;
static int g_actions_file_tab_quit_task_confirmed = 0;

static void actionsFileTabSetQuitConfirmStatus(void) {
	editorSetStatusMsg("File has unsaved changes. Use :q again to quit");
}

static void actionsFileTabSetQuitTaskConfirmStatus(void) {
	editorSetStatusMsg("Task is still running. Use :q again to terminate it and quit");
}

static void actionsFileTabSetCloseTabConfirmStatus(void) {
	editorSetStatusMsg("Tab has unsaved changes. Use :bd again to close tab");
}

static void actionsFileTabSetCloseTaskConfirmStatus(void) {
	editorSetStatusMsg("Task is still running. Use :bd again to terminate it and close tab");
}

static void actionsFileTabPerformQuit(void) {
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

void editorActionQuit(void) {
	if (editorTaskIsRunning() && !g_actions_file_tab_quit_task_confirmed) {
		actionsFileTabSetQuitTaskConfirmStatus();
		g_actions_file_tab_quit_task_confirmed = 1;
		return;
	}
	if (editorTaskIsRunning()) {
		(void)editorTaskTerminate();
		g_actions_file_tab_quit_task_confirmed = 0;
	}

	if (editorTabAnyDirty() && !g_actions_file_tab_quit_confirmed) {
		actionsFileTabSetQuitConfirmStatus();
		g_actions_file_tab_quit_confirmed = 1;
		return;
	}

	actionsFileTabPerformQuit();
}

/* Unconditional quit (Vim `:q!`): discard unsaved changes and terminate any
 * running task without the confirmation prompts editorActionQuit applies. */
void editorActionQuitForce(void) {
	if (editorTaskIsRunning()) {
		(void)editorTaskTerminate();
		g_actions_file_tab_quit_task_confirmed = 0;
	}
	actionsFileTabPerformQuit();
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
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
}

void editorActionCloseTab(void) {
	if (editorActiveTaskTabIsRunning() && !E.close_confirmed) {
		actionsFileTabSetCloseTaskConfirmStatus();
		E.close_confirmed = 1;
		return;
	}
	if (editorActiveTaskTabIsRunning()) {
		(void)editorTaskTerminate();
		E.close_confirmed = 0;
	}

	if (E.dirty && !E.close_confirmed) {
		actionsFileTabSetCloseTabConfirmStatus();
		E.close_confirmed = 1;
		return;
	}

	int was_commit_tab = E.tab_kind == EDITOR_TAB_GIT_COMMIT;
	if (editorTabCloseActive()) {
		E.close_confirmed = 0;
		if (was_commit_tab) {
			editorSetStatusMsg("Commit aborted");
		}
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
		g_actions_file_tab_quit_confirmed = 0;
		g_actions_file_tab_quit_task_confirmed = 0;
	}
}
