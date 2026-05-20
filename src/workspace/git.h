#ifndef ROTIDE_WORKSPACE_GIT_H
#define ROTIDE_WORKSPACE_GIT_H

#include "rotide.h"

int editorGitInit(void);
void editorGitRefresh(void);
void editorGitFree(void);
const char *editorGitBranch(void);
enum editorGitStatus editorGitFileStatus(const char *abs_path);
enum editorGitStatus editorGitDirStatus(const char *abs_path);
char *editorGitGenerateDiff(const char *rel_path, char index_status, char worktree_status,
                            size_t *len_out);

#endif
