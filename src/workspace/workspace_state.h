#ifndef ROTIDE_WORKSPACE_WORKSPACE_STATE_H
#define ROTIDE_WORKSPACE_WORKSPACE_STATE_H

int editorWorkspaceStateInitForCurrentDir(void);
void editorWorkspaceStateShutdown(void);

int editorWorkspaceStateLoadAndApply(int total_cols);
int editorWorkspaceStateRestoreTabs(void);
int editorWorkspaceStateSave(void);
int editorWorkspaceStateRememberRecentFile(const char *path);
int editorWorkspaceStateRecentFileRank(const char *path);

const char *editorWorkspaceStatePath(void);

#endif
