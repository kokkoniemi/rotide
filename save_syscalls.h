#ifndef SAVE_SYSCALLS_H
#define SAVE_SYSCALLS_H

int editorSaveRename(const char *oldpath, const char *newpath);
int editorSaveFsync(int fd);
int editorSaveOpenDir(const char *path);
int editorSaveClose(int fd);
int editorSaveUnlink(const char *path);

#endif
