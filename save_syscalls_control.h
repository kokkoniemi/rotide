#ifndef SAVE_SYSCALLS_CONTROL_H
#define SAVE_SYSCALLS_CONTROL_H

enum editorSaveSyscallOp {
	EDITOR_SAVE_SYSCALL_RENAME = 1,
	EDITOR_SAVE_SYSCALL_FSYNC,
	EDITOR_SAVE_SYSCALL_OPEN_DIR,
	EDITOR_SAVE_SYSCALL_CLOSE,
	EDITOR_SAVE_SYSCALL_UNLINK
};

typedef int (*editorSaveSyscallFailureProbe)(enum editorSaveSyscallOp op);

void editorSaveSyscallsSetFailureProbe(editorSaveSyscallFailureProbe probe);
void editorSaveSyscallsClearFailureProbe(void);

#endif
