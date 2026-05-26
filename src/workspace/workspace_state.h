#ifndef ROTIDE_WORKSPACE_WORKSPACE_STATE_H
#define ROTIDE_WORKSPACE_WORKSPACE_STATE_H

int editorWorkspaceStateInitForCurrentDir(void);
void editorWorkspaceStateShutdown(void);

/* Loads the workspace state file for the current directory. If `reset_panes`
 * is non-zero, the saved layout/tab/pane assignments are skipped — useful when
 * the caller has already populated a fresh layout (e.g. opening files via
 * command-line args) and wants to keep that instead of the persisted layout.
 * Drawer settings and recent files are restored regardless. */
int editorWorkspaceStateLoadAndApply(int total_cols, int reset_panes);
int editorWorkspaceStateRestoreTabs(void);
int editorWorkspaceStateSave(void);
int editorWorkspaceStateRememberRecentFile(const char *path);
int editorWorkspaceStateRecentFileRank(const char *path);

const char *editorWorkspaceStatePath(void);

#endif
