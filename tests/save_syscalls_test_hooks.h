#ifndef SAVE_SYSCALLS_TEST_HOOKS_H
#define SAVE_SYSCALLS_TEST_HOOKS_H

void editorTestSaveSyscallsReset(void);
void editorTestSaveSyscallsInstallNoFail(void);
void editorTestSaveSyscallsFailFsyncOnCall(int call_idx);
void editorTestSaveSyscallsFailOpenDirOnCall(int call_idx);
void editorTestSaveSyscallsFailCloseOnCall(int call_idx);
void editorTestSaveSyscallsFailRenameOnCall(int call_idx);

#endif
