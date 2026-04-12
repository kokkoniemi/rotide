#ifndef SAVE_SYSCALLS_H
#define SAVE_SYSCALLS_H

enum editorSaveSyscallOp {
	EDITOR_SAVE_SYSCALL_RENAME = 1,
	EDITOR_SAVE_SYSCALL_FSYNC,
	EDITOR_SAVE_SYSCALL_OPEN_DIR,
	EDITOR_SAVE_SYSCALL_CLOSE,
	EDITOR_SAVE_SYSCALL_UNLINK
};

typedef int (*editorSaveSyscallFailureProbe)(enum editorSaveSyscallOp op,
		int *failure_errno);

void editorSaveSyscallsSetFailureProbe(editorSaveSyscallFailureProbe probe);
void editorSaveSyscallsClearFailureProbe(void);

int editorSaveRename(const char *oldpath, const char *newpath);
int editorSaveFsync(int fd);
int editorSaveOpenDir(const char *path);
int editorSaveClose(int fd);
int editorSaveUnlink(const char *path);

#endif
