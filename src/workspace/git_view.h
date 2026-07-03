#ifndef ROTIDE_WORKSPACE_GIT_VIEW_H
#define ROTIDE_WORKSPACE_GIT_VIEW_H

#include "rotide.h"

#include <time.h>

/* Handles the EDITOR_ACTION_GIT_* family. Returns 1 when the action was
 * consumed (even if the operation itself failed and only set a status
 * message), 0 when the action is not a git action. */
int editorGitViewHandleMappedAction(enum editorAction action);

void editorGitViewOpenCommit(int amend);
void editorGitViewCommitFromActiveTab(void);
char *editorGitViewCleanCommitMessageDup(const char *text);

void editorGitViewOpenBranches(void);
void editorGitViewOpenLog(void);
void editorGitViewOpenStashes(void);
char *editorGitViewFormatBranchesDup(const char *raw, time_t now);
char *editorGitViewFormatLogDup(const char *raw, time_t now);
char *editorGitViewFormatStashDup(const char *raw);
int editorGitViewLineEntity(enum editorTabKind kind, const char *line, char *entity_out,
                            size_t entity_size);

#endif
