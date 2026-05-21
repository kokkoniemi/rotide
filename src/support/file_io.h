#ifndef ROTIDE_SUPPORT_FILE_IO_H
#define ROTIDE_SUPPORT_FILE_IO_H

#include <stddef.h>

char *editorPathJoin(const char *left, const char *right);
char *editorPathBasenameDup(const char *path);
char *editorPathDirnameDup(const char *path);
char *editorPathCwdDup(void);
char *editorPathAbsoluteDup(const char *path);
char *editorPathFindMarkerUpward(const char *start_dir, const char *const *markers,
                                 size_t marker_count);
int editorPathsReferToSameFile(const char *left, const char *right);
char *editorTempPathForTarget(const char *target);
int editorOpenParentDirForTarget(const char *target);

#endif
