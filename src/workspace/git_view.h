#ifndef ROTIDE_WORKSPACE_GIT_VIEW_H
#define ROTIDE_WORKSPACE_GIT_VIEW_H

#include "config/theme_config.h"
#include "language/syntax.h"
#include "rotide.h"
#include "workspace/git_ops.h"

#include <time.h>

/* Handles the EDITOR_ACTION_GIT_* family. Returns 1 when the action was
 * consumed (even if the operation itself failed and only set a status
 * message), 0 when the action is not a git action. */
int editorGitViewHandleMappedAction(enum editorAction action);

void editorGitViewOpenCommit(int amend);
void editorGitViewCommitFromActiveTab(void);
char *editorGitViewCleanCommitMessageDup(const char *text);

/* Per-line classification of generated git diff tabs; +/- prefixes are
 * stripped so the code gets real language highlighting, and the kind array
 * drives the added/removed background tints instead. */
enum editorGitViewLineKind {
	EDITOR_GIT_VIEW_LINE_TEXT = 0,
	EDITOR_GIT_VIEW_LINE_ADDED,
	EDITOR_GIT_VIEW_LINE_REMOVED,
	EDITOR_GIT_VIEW_LINE_HEADER
};

char *editorGitViewBuildDiffDup(const char *patch, size_t patch_len, unsigned char **line_kinds_out,
                                int **line_numbers_out, int *line_kind_count_out,
                                char **source_path_out);
int editorGitViewLineNumber(int row_idx);
/* A partially-staged file lists under both Staged and Changes; staged_group
 * picks that row's side (cached index diff vs worktree diff). */
int editorGitViewOpenDiffForEntry(const char *rel_path, char index_status, char worktree_status,
                                  int staged_group);
enum editorGitOpsPatchKind editorGitViewDiffKindForStatus(char index_status, char worktree_status,
                                                          int staged_group);
/* Returns 1 when the active tab is the diff opened for this entry (matching
 * rel_path and the group-derived patch kind), so the Git drawer highlights only
 * the row whose side is currently shown. */
int editorGitViewActiveDiffMatchesEntry(const char *rel_path, char index_status,
                                        char worktree_status, int staged_group);
void editorGitViewToggleDiffContext(void);

void editorGitViewOpenBranches(void);
void editorGitViewOpenLog(void);
void editorGitViewOpenStashes(void);
char *editorGitViewFormatBranchesDup(const char *raw, time_t now);
char *editorGitViewFormatLogDup(const char *raw, time_t now);
char *editorGitViewFormatStashDup(const char *raw);
int editorGitViewLineEntity(enum editorTabKind kind, const char *line, char *entity_out,
                            size_t entity_size);

/* Render hooks: per-row background tint (diff added/removed lines, header
 * rows) and synthetic syntax spans for the git list views. */
int editorGitViewRowBgColor(int row_idx, struct editorThemeColor *color_out);
int editorGitViewRowSyntaxSpans(int row_idx, struct editorRowSyntaxSpan *spans, int max_spans,
                                int *count_out);

#endif
