#ifndef EDITOR_GIT_H
#define EDITOR_GIT_H

#include "rotide.h"

int editorGitInit(void);
void editorGitRefresh(void);
void editorGitFree(void);
const char *editorGitBranch(void);
enum editorGitStatus editorGitFileStatus(const char *abs_path);
enum editorGitStatus editorGitDirStatus(const char *abs_path);

#endif
