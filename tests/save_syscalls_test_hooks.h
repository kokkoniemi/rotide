#ifndef SAVE_SYSCALLS_TEST_HOOKS_H
#define SAVE_SYSCALLS_TEST_HOOKS_H

void editorTestSaveSyscallsReset(void);
void editorTestSaveSyscallsInstallNoFail(void);
void editorTestSaveSyscallsFailFsyncOnCall(int call_idx);
void editorTestSaveSyscallsFailOpenDirOnCall(int call_idx);
void editorTestSaveSyscallsFailCloseOnCall(int call_idx);
void editorTestSaveSyscallsFailRenameOnCall(int call_idx);
void editorTestSaveSyscallsFailUnlinkOnCall(int call_idx);
void editorTestSaveSyscallsFailFsyncOnCallWithErrno(int call_idx, int errnum);
void editorTestSaveSyscallsFailOpenDirOnCallWithErrno(int call_idx, int errnum);
void editorTestSaveSyscallsFailCloseOnCallWithErrno(int call_idx, int errnum);
void editorTestSaveSyscallsFailRenameOnCallWithErrno(int call_idx, int errnum);
void editorTestSaveSyscallsFailUnlinkOnCallWithErrno(int call_idx, int errnum);

#endif
