#ifndef ROTIDE_WORKSPACE_GIT_OPS_H
#define ROTIDE_WORKSPACE_GIT_OPS_H

#include <stddef.h>

/* Mutating git commands (argv-based, no shell). Every mutator returns 1 on
 * success and 0 on failure; failures set a status message from git's stderr.
 * Query helpers return malloc'd output (caller frees) or NULL. */

int editorGitOpsStageFile(const char *rel_path);
int editorGitOpsUnstageFile(const char *rel_path);
int editorGitOpsStageAll(void);
int editorGitOpsDiscardFile(const char *rel_path, int untracked);
int editorGitOpsCommit(const char *message, int amend, char *short_sha_out, size_t sha_size);
char *editorGitOpsLastCommitMessageDup(void);
char *editorGitOpsBranchListRawDup(size_t *len_out);
int editorGitOpsBranchCreate(const char *name);
int editorGitOpsCheckout(const char *name);
int editorGitOpsBranchDelete(const char *name);
char *editorGitOpsLogRawDup(int max_count, size_t *len_out);
char *editorGitOpsStashListRawDup(size_t *len_out);
int editorGitOpsStashApply(const char *ref);
int editorGitOpsStashPop(const char *ref);
int editorGitOpsStashDrop(const char *ref);
int editorGitOpsCherryPick(const char *sha);
int editorGitOpsRevert(const char *sha);
int editorGitOpsTag(const char *name, const char *sha);
char *editorGitOpsShowCommitDup(const char *sha, size_t *len_out);
char *editorGitOpsStashShowDup(const char *ref, size_t *len_out);

#endif
