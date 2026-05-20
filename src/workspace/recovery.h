#ifndef ROTIDE_WORKSPACE_RECOVERY_H
#define ROTIDE_WORKSPACE_RECOVERY_H

int editorRecoveryInitForCurrentDir(void);
void editorRecoveryShutdown(void);
const char *editorRecoveryPath(void);
int editorRecoveryHasSnapshot(void);
int editorRecoveryRestoreSnapshot(void);
int editorRecoveryPromptAndMaybeRestore(void);
void editorRecoveryMaybeAutosaveOnActivity(void);
void editorRecoveryCleanupOnCleanExit(void);
int editorStartupLoadRecoveryOrOpenArgs(int argc, char *argv[]);

#endif
