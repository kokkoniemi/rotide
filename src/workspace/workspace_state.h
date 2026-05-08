#ifndef WORKSPACE_WORKSPACE_STATE_H
#define WORKSPACE_WORKSPACE_STATE_H

int editorWorkspaceStateInitForCurrentDir(void);
void editorWorkspaceStateShutdown(void);

int editorWorkspaceStateLoadAndApply(int total_cols);
int editorWorkspaceStateSave(void);

const char *editorWorkspaceStatePath(void);

#endif
