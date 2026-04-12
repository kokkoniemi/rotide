#ifndef EDITOR_FILE_IO_H
#define EDITOR_FILE_IO_H

char *editorPathJoin(const char *left, const char *right);
char *editorPathBasenameDup(const char *path);
char *editorPathDirnameDup(const char *path);
char *editorPathGetCwd(void);
int editorPathsReferToSameFile(const char *left, const char *right);
char *editorTempPathForTarget(const char *target);
int editorOpenParentDirForTarget(const char *target);

#endif
