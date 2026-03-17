#include "save_syscalls_test_hooks.h"

#include "save_syscalls_control.h"

static struct {
	int rename_calls;
	int fsync_calls;
	int open_dir_calls;
	int close_calls;
	int fail_rename_on_call;
	int fail_fsync_on_call;
	int fail_open_dir_on_call;
	int fail_close_on_call;
} editor_save_syscall_test_state = {
	.rename_calls = 0,
	.fsync_calls = 0,
	.open_dir_calls = 0,
	.close_calls = 0,
	.fail_rename_on_call = -1,
	.fail_fsync_on_call = -1,
	.fail_open_dir_on_call = -1,
	.fail_close_on_call = -1
};

static int editorTestSaveSyscallsFailureProbe(enum editorSaveSyscallOp op) {
	switch (op) {
		case EDITOR_SAVE_SYSCALL_RENAME:
			editor_save_syscall_test_state.rename_calls++;
			return editor_save_syscall_test_state.fail_rename_on_call > 0 &&
					editor_save_syscall_test_state.rename_calls ==
					editor_save_syscall_test_state.fail_rename_on_call;
		case EDITOR_SAVE_SYSCALL_FSYNC:
			editor_save_syscall_test_state.fsync_calls++;
			return editor_save_syscall_test_state.fail_fsync_on_call > 0 &&
					editor_save_syscall_test_state.fsync_calls ==
					editor_save_syscall_test_state.fail_fsync_on_call;
		case EDITOR_SAVE_SYSCALL_OPEN_DIR:
			editor_save_syscall_test_state.open_dir_calls++;
			return editor_save_syscall_test_state.fail_open_dir_on_call > 0 &&
					editor_save_syscall_test_state.open_dir_calls ==
					editor_save_syscall_test_state.fail_open_dir_on_call;
		case EDITOR_SAVE_SYSCALL_CLOSE:
			editor_save_syscall_test_state.close_calls++;
			return editor_save_syscall_test_state.fail_close_on_call > 0 &&
					editor_save_syscall_test_state.close_calls ==
					editor_save_syscall_test_state.fail_close_on_call;
	}

	return 0;
}

static void editorTestSaveSyscallsClearCounters(void) {
	editor_save_syscall_test_state.rename_calls = 0;
	editor_save_syscall_test_state.fsync_calls = 0;
	editor_save_syscall_test_state.open_dir_calls = 0;
	editor_save_syscall_test_state.close_calls = 0;
}

void editorTestSaveSyscallsReset(void) {
	editorTestSaveSyscallsClearCounters();
	editor_save_syscall_test_state.fail_rename_on_call = -1;
	editor_save_syscall_test_state.fail_fsync_on_call = -1;
	editor_save_syscall_test_state.fail_open_dir_on_call = -1;
	editor_save_syscall_test_state.fail_close_on_call = -1;
	editorSaveSyscallsClearFailureProbe();
}

void editorTestSaveSyscallsInstallNoFail(void) {
	editorTestSaveSyscallsClearCounters();
	editor_save_syscall_test_state.fail_rename_on_call = -1;
	editor_save_syscall_test_state.fail_fsync_on_call = -1;
	editor_save_syscall_test_state.fail_open_dir_on_call = -1;
	editor_save_syscall_test_state.fail_close_on_call = -1;
	editorSaveSyscallsSetFailureProbe(editorTestSaveSyscallsFailureProbe);
}

void editorTestSaveSyscallsFailFsyncOnCall(int call_idx) {
	editorTestSaveSyscallsInstallNoFail();
	editor_save_syscall_test_state.fail_fsync_on_call = call_idx;
}

void editorTestSaveSyscallsFailOpenDirOnCall(int call_idx) {
	editorTestSaveSyscallsInstallNoFail();
	editor_save_syscall_test_state.fail_open_dir_on_call = call_idx;
}

void editorTestSaveSyscallsFailCloseOnCall(int call_idx) {
	editorTestSaveSyscallsInstallNoFail();
	editor_save_syscall_test_state.fail_close_on_call = call_idx;
}

void editorTestSaveSyscallsFailRenameOnCall(int call_idx) {
	editorTestSaveSyscallsInstallNoFail();
	editor_save_syscall_test_state.fail_rename_on_call = call_idx;
}
