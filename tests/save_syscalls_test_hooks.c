#include "save_syscalls_test_hooks.h"

#include "support/save_syscalls.h"
#include <errno.h>
#include <stddef.h>

static struct {
	int rename_calls;
	int fsync_calls;
	int open_dir_calls;
	int close_calls;
	int unlink_calls;
	int fail_rename_on_call;
	int fail_fsync_on_call;
	int fail_open_dir_on_call;
	int fail_close_on_call;
	int fail_unlink_on_call;
	int fail_rename_errno;
	int fail_fsync_errno;
	int fail_open_dir_errno;
	int fail_close_errno;
	int fail_unlink_errno;
} editor_save_syscall_test_state = {
	.rename_calls = 0,
	.fsync_calls = 0,
	.open_dir_calls = 0,
	.close_calls = 0,
	.unlink_calls = 0,
	.fail_rename_on_call = -1,
	.fail_fsync_on_call = -1,
	.fail_open_dir_on_call = -1,
	.fail_close_on_call = -1,
	.fail_unlink_on_call = -1,
	.fail_rename_errno = EIO,
	.fail_fsync_errno = EIO,
	.fail_open_dir_errno = EIO,
	.fail_close_errno = EIO,
	.fail_unlink_errno = EIO
};

static int editorTestSaveSyscallsFailureProbe(enum editorSaveSyscallOp op,
		int *failure_errno) {
	switch (op) {
		case EDITOR_SAVE_SYSCALL_RENAME:
			editor_save_syscall_test_state.rename_calls++;
			if (editor_save_syscall_test_state.fail_rename_on_call > 0 &&
					editor_save_syscall_test_state.rename_calls ==
					editor_save_syscall_test_state.fail_rename_on_call) {
				if (failure_errno != NULL) {
					*failure_errno = editor_save_syscall_test_state.fail_rename_errno;
				}
				return 1;
			}
			return 0;
		case EDITOR_SAVE_SYSCALL_FSYNC:
			editor_save_syscall_test_state.fsync_calls++;
			if (editor_save_syscall_test_state.fail_fsync_on_call > 0 &&
					editor_save_syscall_test_state.fsync_calls ==
					editor_save_syscall_test_state.fail_fsync_on_call) {
				if (failure_errno != NULL) {
					*failure_errno = editor_save_syscall_test_state.fail_fsync_errno;
				}
				return 1;
			}
			return 0;
		case EDITOR_SAVE_SYSCALL_OPEN_DIR:
			editor_save_syscall_test_state.open_dir_calls++;
			if (editor_save_syscall_test_state.fail_open_dir_on_call > 0 &&
					editor_save_syscall_test_state.open_dir_calls ==
					editor_save_syscall_test_state.fail_open_dir_on_call) {
				if (failure_errno != NULL) {
					*failure_errno = editor_save_syscall_test_state.fail_open_dir_errno;
				}
				return 1;
			}
			return 0;
		case EDITOR_SAVE_SYSCALL_CLOSE:
			editor_save_syscall_test_state.close_calls++;
			if (editor_save_syscall_test_state.fail_close_on_call > 0 &&
					editor_save_syscall_test_state.close_calls ==
					editor_save_syscall_test_state.fail_close_on_call) {
				if (failure_errno != NULL) {
					*failure_errno = editor_save_syscall_test_state.fail_close_errno;
				}
				return 1;
			}
			return 0;
		case EDITOR_SAVE_SYSCALL_UNLINK:
			editor_save_syscall_test_state.unlink_calls++;
			if (editor_save_syscall_test_state.fail_unlink_on_call > 0 &&
					editor_save_syscall_test_state.unlink_calls ==
					editor_save_syscall_test_state.fail_unlink_on_call) {
				if (failure_errno != NULL) {
					*failure_errno = editor_save_syscall_test_state.fail_unlink_errno;
				}
				return 1;
			}
			return 0;
	}

	return 0;
}

static void editorTestSaveSyscallsResetErrnos(void) {
	editor_save_syscall_test_state.fail_rename_errno = EIO;
	editor_save_syscall_test_state.fail_fsync_errno = EIO;
	editor_save_syscall_test_state.fail_open_dir_errno = EIO;
	editor_save_syscall_test_state.fail_close_errno = EIO;
	editor_save_syscall_test_state.fail_unlink_errno = EIO;
}

static void editorTestSaveSyscallsClearCounters(void) {
	editor_save_syscall_test_state.rename_calls = 0;
	editor_save_syscall_test_state.fsync_calls = 0;
	editor_save_syscall_test_state.open_dir_calls = 0;
	editor_save_syscall_test_state.close_calls = 0;
	editor_save_syscall_test_state.unlink_calls = 0;
}

void editorTestSaveSyscallsReset(void) {
	editorTestSaveSyscallsClearCounters();
	editor_save_syscall_test_state.fail_rename_on_call = -1;
	editor_save_syscall_test_state.fail_fsync_on_call = -1;
	editor_save_syscall_test_state.fail_open_dir_on_call = -1;
	editor_save_syscall_test_state.fail_close_on_call = -1;
	editor_save_syscall_test_state.fail_unlink_on_call = -1;
	editorTestSaveSyscallsResetErrnos();
	editorSaveSyscallsClearFailureProbe();
}

void editorTestSaveSyscallsInstallNoFail(void) {
	editorTestSaveSyscallsClearCounters();
	editor_save_syscall_test_state.fail_rename_on_call = -1;
	editor_save_syscall_test_state.fail_fsync_on_call = -1;
	editor_save_syscall_test_state.fail_open_dir_on_call = -1;
	editor_save_syscall_test_state.fail_close_on_call = -1;
	editor_save_syscall_test_state.fail_unlink_on_call = -1;
	editorTestSaveSyscallsResetErrnos();
	editorSaveSyscallsSetFailureProbe(editorTestSaveSyscallsFailureProbe);
}

void editorTestSaveSyscallsFailFsyncOnCall(int call_idx) {
	editorTestSaveSyscallsInstallNoFail();
	editorTestSaveSyscallsFailFsyncOnCallWithErrno(call_idx, EIO);
}

void editorTestSaveSyscallsFailOpenDirOnCall(int call_idx) {
	editorTestSaveSyscallsInstallNoFail();
	editorTestSaveSyscallsFailOpenDirOnCallWithErrno(call_idx, EIO);
}

void editorTestSaveSyscallsFailCloseOnCall(int call_idx) {
	editorTestSaveSyscallsInstallNoFail();
	editorTestSaveSyscallsFailCloseOnCallWithErrno(call_idx, EIO);
}

void editorTestSaveSyscallsFailRenameOnCall(int call_idx) {
	editorTestSaveSyscallsInstallNoFail();
	editorTestSaveSyscallsFailRenameOnCallWithErrno(call_idx, EIO);
}

void editorTestSaveSyscallsFailUnlinkOnCall(int call_idx) {
	editorTestSaveSyscallsInstallNoFail();
	editorTestSaveSyscallsFailUnlinkOnCallWithErrno(call_idx, EIO);
}

void editorTestSaveSyscallsFailFsyncOnCallWithErrno(int call_idx, int errnum) {
	editor_save_syscall_test_state.fail_fsync_on_call = call_idx;
	editor_save_syscall_test_state.fail_fsync_errno = errnum;
}

void editorTestSaveSyscallsFailOpenDirOnCallWithErrno(int call_idx, int errnum) {
	editor_save_syscall_test_state.fail_open_dir_on_call = call_idx;
	editor_save_syscall_test_state.fail_open_dir_errno = errnum;
}

void editorTestSaveSyscallsFailCloseOnCallWithErrno(int call_idx, int errnum) {
	editor_save_syscall_test_state.fail_close_on_call = call_idx;
	editor_save_syscall_test_state.fail_close_errno = errnum;
}

void editorTestSaveSyscallsFailRenameOnCallWithErrno(int call_idx, int errnum) {
	editor_save_syscall_test_state.fail_rename_on_call = call_idx;
	editor_save_syscall_test_state.fail_rename_errno = errnum;
}

void editorTestSaveSyscallsFailUnlinkOnCallWithErrno(int call_idx, int errnum) {
	editor_save_syscall_test_state.fail_unlink_on_call = call_idx;
	editor_save_syscall_test_state.fail_unlink_errno = errnum;
}
