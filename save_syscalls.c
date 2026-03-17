#include "rotide.h"
#include "save_syscalls.h"

#include "save_syscalls_control.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

static editorSaveSyscallFailureProbe editor_save_syscall_failure_probe = NULL;

static int editorSaveSyscallsShouldFail(enum editorSaveSyscallOp op, int *failure_errno) {
	int probe_errno = 0;

	if (editor_save_syscall_failure_probe == NULL) {
		return 0;
	}
	if (!editor_save_syscall_failure_probe(op, &probe_errno)) {
		return 0;
	}
	if (failure_errno != NULL) {
		*failure_errno = probe_errno;
	}
	return 1;
}

void editorSaveSyscallsSetFailureProbe(editorSaveSyscallFailureProbe probe) {
	editor_save_syscall_failure_probe = probe;
}

void editorSaveSyscallsClearFailureProbe(void) {
	editor_save_syscall_failure_probe = NULL;
}

int editorSaveRename(const char *oldpath, const char *newpath) {
	int failure_errno = 0;
	if (editorSaveSyscallsShouldFail(EDITOR_SAVE_SYSCALL_RENAME, &failure_errno)) {
		errno = failure_errno != 0 ? failure_errno : EIO;
		return -1;
	}

	return rename(oldpath, newpath);
}

int editorSaveFsync(int fd) {
	int failure_errno = 0;
	if (editorSaveSyscallsShouldFail(EDITOR_SAVE_SYSCALL_FSYNC, &failure_errno)) {
		errno = failure_errno != 0 ? failure_errno : EIO;
		return -1;
	}

	return fsync(fd);
}

int editorSaveOpenDir(const char *path) {
	int failure_errno = 0;
	if (editorSaveSyscallsShouldFail(EDITOR_SAVE_SYSCALL_OPEN_DIR, &failure_errno)) {
		errno = failure_errno != 0 ? failure_errno : EIO;
		return -1;
	}

	return open(path, O_RDONLY | O_DIRECTORY);
}

int editorSaveClose(int fd) {
	int failure_errno = 0;
	if (editorSaveSyscallsShouldFail(EDITOR_SAVE_SYSCALL_CLOSE, &failure_errno)) {
		errno = failure_errno != 0 ? failure_errno : EIO;
		return -1;
	}

	return close(fd);
}

int editorSaveUnlink(const char *path) {
	int failure_errno = 0;
	if (editorSaveSyscallsShouldFail(EDITOR_SAVE_SYSCALL_UNLINK, &failure_errno)) {
		errno = failure_errno != 0 ? failure_errno : EIO;
		return -1;
	}

	return unlink(path);
}
