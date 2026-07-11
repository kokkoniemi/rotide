#ifndef ROTIDE_WORKSPACE_GIT_OPS_H
#define ROTIDE_WORKSPACE_GIT_OPS_H

#include <stddef.h>

/* What a git patch tab shows; carried per tab so its content can be
 * regenerated (refresh, hunks-only vs whole-file toggle). */
enum editorGitOpsPatchKind {
	EDITOR_GIT_OPS_PATCH_NONE = 0,
	EDITOR_GIT_OPS_PATCH_DIFF_WORKTREE,
	EDITOR_GIT_OPS_PATCH_DIFF_CACHED,
	EDITOR_GIT_OPS_PATCH_DIFF_UNTRACKED,
	EDITOR_GIT_OPS_PATCH_SHOW_COMMIT,
	EDITOR_GIT_OPS_PATCH_SHOW_STASH
};

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
int editorGitOpsCurrentBranchPushRemote(char *remote_out, size_t remote_size,
                                        int *has_upstream_out);
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
char *editorGitOpsPatchDup(enum editorGitOpsPatchKind kind, const char *arg, int whole_file,
                           size_t *len_out);

#endif
