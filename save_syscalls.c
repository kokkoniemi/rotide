#include "rotide.h"
#include "save_syscalls.h"

#include "save_syscalls_control.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

static editorSaveSyscallFailureProbe editor_save_syscall_failure_probe = NULL;

static int editorSaveSyscallsShouldFail(enum editorSaveSyscallOp op) {
	return editor_save_syscall_failure_probe != NULL &&
			editor_save_syscall_failure_probe(op);
}

void editorSaveSyscallsSetFailureProbe(editorSaveSyscallFailureProbe probe) {
	editor_save_syscall_failure_probe = probe;
}

void editorSaveSyscallsClearFailureProbe(void) {
	editor_save_syscall_failure_probe = NULL;
}

int editorSaveRename(const char *oldpath, const char *newpath) {
	if (editorSaveSyscallsShouldFail(EDITOR_SAVE_SYSCALL_RENAME)) {
		errno = EIO;
		return -1;
	}

	return rename(oldpath, newpath);
}

int editorSaveFsync(int fd) {
	if (editorSaveSyscallsShouldFail(EDITOR_SAVE_SYSCALL_FSYNC)) {
		errno = EIO;
		return -1;
	}

	return fsync(fd);
}

int editorSaveOpenDir(const char *path) {
	if (editorSaveSyscallsShouldFail(EDITOR_SAVE_SYSCALL_OPEN_DIR)) {
		errno = EIO;
		return -1;
	}

	return open(path, O_RDONLY | O_DIRECTORY);
}

int editorSaveClose(int fd) {
	if (editorSaveSyscallsShouldFail(EDITOR_SAVE_SYSCALL_CLOSE)) {
		errno = EIO;
		return -1;
	}

	return close(fd);
}
