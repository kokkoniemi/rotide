#include "workspace/git_ops.h"

#include "editing/edit.h"
#include "rotide.h"
#include "support/alloc.h"
#include "support/size_utils.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define GIT_OPS_MAX_OUTPUT_BYTES (2U * 1024U * 1024U)

struct gitOpsResult {
	char *output;
	size_t len;
	int exit_code;
	int truncated;
};

static void gitOpsResultClear(struct gitOpsResult *result) {
	if (result == NULL) {
		return;
	}
	free(result->output);
	memset(result, 0, sizeof(*result));
}

static int gitOpsOutputAppend(struct gitOpsResult *result, const char *buf, size_t len) {
	if (len == 0 || result->truncated) {
		return 1;
	}
	size_t new_len = 0;
	if (!editorSizeAdd(result->len, len, &new_len) || new_len > GIT_OPS_MAX_OUTPUT_BYTES) {
		result->truncated = 1;
		return 1;
	}
	char *grown = editorRealloc(result->output, new_len + 1);
	if (grown == NULL) {
		return 0;
	}
	memcpy(grown + result->len, buf, len);
	grown[new_len] = '\0';
	result->output = grown;
	result->len = new_len;
	return 1;
}

/* Runs argv (no shell) capturing stdout; capture_stderr merges stderr into the
 * same pipe so mutation failures carry git's error text. */
static int gitOpsRun(char *const argv[], int capture_stderr, struct gitOpsResult *result) {
	int pipefd[2] = {-1, -1};
	if (result == NULL || pipe(pipefd) == -1) {
		return 0;
	}
	memset(result, 0, sizeof(*result));
	result->exit_code = 126;

	pid_t pid = fork();
	if (pid == -1) {
		close(pipefd[0]);
		close(pipefd[1]);
		return 0;
	}
	if (pid == 0) {
		(void)signal(SIGPIPE, SIG_DFL);
		close(pipefd[0]);
		if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
			_exit(126);
		}
		if (capture_stderr) {
			if (dup2(pipefd[1], STDERR_FILENO) == -1) {
				_exit(126);
			}
		} else {
			int devnull = open("/dev/null", O_WRONLY);
			if (devnull != -1) {
				(void)dup2(devnull, STDERR_FILENO);
				close(devnull);
			}
		}
		close(pipefd[1]);
		execvp(argv[0], argv);
		_exit(errno == ENOENT ? 127 : 126);
	}

	close(pipefd[1]);
	char buf[4096];
	while (1) {
		ssize_t nread = read(pipefd[0], buf, sizeof(buf));
		if (nread == -1) {
			if (errno == EINTR) {
				continue;
			}
			close(pipefd[0]);
			(void)waitpid(pid, NULL, 0);
			gitOpsResultClear(result);
			return 0;
		}
		if (nread == 0) {
			break;
		}
		if (!gitOpsOutputAppend(result, buf, (size_t)nread)) {
			close(pipefd[0]);
			(void)waitpid(pid, NULL, 0);
			gitOpsResultClear(result);
			return 0;
		}
	}
	close(pipefd[0]);

	int status = 0;
	while (waitpid(pid, &status, 0) == -1) {
		if (errno != EINTR) {
			gitOpsResultClear(result);
			return 0;
		}
	}
	result->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 126;
	if (result->output == NULL) {
		result->output = strdup("");
		if (result->output == NULL) {
			return 0;
		}
	}
	return 1;
}

static int gitOpsRequireRepo(void) {
	if (E.git_repo_root == NULL) {
		editorSetStatusMsg("Not a Git repository");
		return 0;
	}
	return 1;
}

static void gitOpsFailStatusMsg(const char *fallback, const struct gitOpsResult *result) {
	if (result != NULL && result->output != NULL && result->output[0] != '\0') {
		size_t line_len = 0;
		const char *text = result->output;
		while (text[line_len] != '\0' && text[line_len] != '\n' && text[line_len] != '\r') {
			line_len++;
		}
		if (line_len > 0) {
			editorSetStatusMsg("git: %.*s", (int)line_len, text);
			return;
		}
	}
	editorSetStatusMsg("git: %s", fallback);
}

/* Runs a repo-scoped mutation: success is exit code 0; failure surfaces git's
 * first stderr line (or fallback_msg) in the status bar. Callers pass argv
 * beginning after the fixed "git -C <repo_root>" prefix. */
static int gitOpsRunMutation(char *const tail[], const char *fallback_msg) {
	if (!gitOpsRequireRepo()) {
		return 0;
	}
	char *argv[16] = {"git", "-C", E.git_repo_root};
	size_t argc = 3;
	for (size_t i = 0; tail[i] != NULL; i++) {
		if (argc + 1 >= sizeof(argv) / sizeof(argv[0])) {
			return 0;
		}
		argv[argc++] = tail[i];
	}
	argv[argc] = NULL;

	struct gitOpsResult result;
	if (!gitOpsRun(argv, 1, &result)) {
		editorSetStatusMsg("git: failed to run");
		return 0;
	}
	if (result.exit_code != 0) {
		gitOpsFailStatusMsg(fallback_msg, &result);
		gitOpsResultClear(&result);
		return 0;
	}
	gitOpsResultClear(&result);
	return 1;
}

/* Runs a repo-scoped query returning malloc'd stdout (stderr discarded). */
static char *gitOpsRunQueryDup(char *const tail[], size_t *len_out) {
	if (len_out != NULL) {
		*len_out = 0;
	}
	if (!gitOpsRequireRepo()) {
		return NULL;
	}
	char *argv[16] = {"git", "-C", E.git_repo_root};
	size_t argc = 3;
	for (size_t i = 0; tail[i] != NULL; i++) {
		if (argc + 1 >= sizeof(argv) / sizeof(argv[0])) {
			return NULL;
		}
		argv[argc++] = tail[i];
	}
	argv[argc] = NULL;

	struct gitOpsResult result;
	if (!gitOpsRun(argv, 0, &result)) {
		return NULL;
	}
	if (result.exit_code != 0) {
		gitOpsResultClear(&result);
		return NULL;
	}
	if (len_out != NULL) {
		*len_out = result.len;
	}
	return result.output;
}

int editorGitOpsStageFile(const char *rel_path) {
	if (rel_path == NULL) {
		return 0;
	}
	char *tail[] = {"add", "--", (char *)rel_path, NULL};
	return gitOpsRunMutation(tail, "stage failed");
}

int editorGitOpsUnstageFile(const char *rel_path) {
	if (rel_path == NULL) {
		return 0;
	}
	char *tail[] = {"restore", "--staged", "--", (char *)rel_path, NULL};
	return gitOpsRunMutation(tail, "unstage failed");
}

int editorGitOpsStageAll(void) {
	char *tail[] = {"add", "-A", NULL};
	return gitOpsRunMutation(tail, "stage all failed");
}

int editorGitOpsDiscardFile(const char *rel_path, int untracked) {
	if (rel_path == NULL) {
		return 0;
	}
	if (untracked) {
		char *clean_tail[] = {"clean", "-f", "--", (char *)rel_path, NULL};
		return gitOpsRunMutation(clean_tail, "discard failed");
	}
	char *tail[] = {"checkout", "HEAD", "--", (char *)rel_path, NULL};
	return gitOpsRunMutation(tail, "discard failed");
}

int editorGitOpsCommit(const char *message, int amend, char *short_sha_out, size_t sha_size) {
	if (message == NULL || message[0] == '\0') {
		return 0;
	}
	int ok = 0;
	if (amend) {
		char *amend_tail[] = {"commit", "--amend", "-m", (char *)message, NULL};
		ok = gitOpsRunMutation(amend_tail, "commit failed");
	} else {
		char *tail[] = {"commit", "-m", (char *)message, NULL};
		ok = gitOpsRunMutation(tail, "commit failed");
	}
	if (!ok) {
		return 0;
	}
	if (short_sha_out != NULL && sha_size > 0) {
		short_sha_out[0] = '\0';
		size_t len = 0;
		char *sha_tail[] = {"rev-parse", "--short", "HEAD", NULL};
		char *sha = gitOpsRunQueryDup(sha_tail, &len);
		if (sha != NULL) {
			while (len > 0 && (sha[len - 1] == '\n' || sha[len - 1] == '\r')) {
				sha[--len] = '\0';
			}
			(void)snprintf(short_sha_out, sha_size, "%s", sha);
			free(sha);
		}
	}
	return 1;
}

char *editorGitOpsLastCommitMessageDup(void) {
	char *tail[] = {"log", "-1", "--format=%B", NULL};
	return gitOpsRunQueryDup(tail, NULL);
}

char *editorGitOpsBranchListRawDup(size_t *len_out) {
	char *tail[] = {"for-each-ref", "refs/heads", "refs/remotes",
	                "--format=%(HEAD)%09%(refname)%09%(refname:short)"
	                "%09%(upstream:short)%09%(upstream:track)%09%(committerdate:unix)",
	                NULL};
	return gitOpsRunQueryDup(tail, len_out);
}

int editorGitOpsBranchCreate(const char *name) {
	if (name == NULL || name[0] == '\0') {
		return 0;
	}
	char *tail[] = {"checkout", "-b", (char *)name, NULL};
	return gitOpsRunMutation(tail, "branch create failed");
}

int editorGitOpsCheckout(const char *name) {
	if (name == NULL || name[0] == '\0') {
		return 0;
	}
	char *tail[] = {"checkout", (char *)name, NULL};
	return gitOpsRunMutation(tail, "checkout failed");
}

int editorGitOpsBranchDelete(const char *name) {
	if (name == NULL || name[0] == '\0') {
		return 0;
	}
	char *tail[] = {"branch", "-d", (char *)name, NULL};
	return gitOpsRunMutation(tail, "branch delete failed");
}

char *editorGitOpsLogRawDup(int max_count, size_t *len_out) {
	char count_arg[32];
	if (max_count <= 0) {
		max_count = 200;
	}
	(void)snprintf(count_arg, sizeof(count_arg), "-n%d", max_count);
	char *tail[] = {"log", count_arg, "--format=%h%x09%d%x09%s%x09%an%x09%at", NULL};
	return gitOpsRunQueryDup(tail, len_out);
}

char *editorGitOpsStashListRawDup(size_t *len_out) {
	char *tail[] = {"stash", "list", "--format=%gd: %gs", NULL};
	return gitOpsRunQueryDup(tail, len_out);
}

int editorGitOpsStashApply(const char *ref) {
	if (ref == NULL || ref[0] == '\0') {
		return 0;
	}
	char *tail[] = {"stash", "apply", (char *)ref, NULL};
	return gitOpsRunMutation(tail, "stash apply failed");
}

int editorGitOpsStashPop(const char *ref) {
	if (ref == NULL || ref[0] == '\0') {
		return 0;
	}
	char *tail[] = {"stash", "pop", (char *)ref, NULL};
	return gitOpsRunMutation(tail, "stash pop failed");
}

int editorGitOpsStashDrop(const char *ref) {
	if (ref == NULL || ref[0] == '\0') {
		return 0;
	}
	char *tail[] = {"stash", "drop", (char *)ref, NULL};
	return gitOpsRunMutation(tail, "stash drop failed");
}

int editorGitOpsCherryPick(const char *sha) {
	if (sha == NULL || sha[0] == '\0') {
		return 0;
	}
	char *tail[] = {"cherry-pick", (char *)sha, NULL};
	return gitOpsRunMutation(tail, "cherry-pick failed");
}

int editorGitOpsRevert(const char *sha) {
	if (sha == NULL || sha[0] == '\0') {
		return 0;
	}
	char *tail[] = {"revert", "--no-edit", (char *)sha, NULL};
	return gitOpsRunMutation(tail, "revert failed");
}

int editorGitOpsTag(const char *name, const char *sha) {
	if (name == NULL || name[0] == '\0' || sha == NULL || sha[0] == '\0') {
		return 0;
	}
	char *tail[] = {"tag", (char *)name, (char *)sha, NULL};
	return gitOpsRunMutation(tail, "tag failed");
}

char *editorGitOpsShowCommitDup(const char *sha, size_t *len_out) {
	if (sha == NULL || sha[0] == '\0') {
		if (len_out != NULL) {
			*len_out = 0;
		}
		return NULL;
	}
	char *tail[] = {"show", "--stat", "-p", (char *)sha, NULL};
	return gitOpsRunQueryDup(tail, len_out);
}

char *editorGitOpsStashShowDup(const char *ref, size_t *len_out) {
	if (ref == NULL || ref[0] == '\0') {
		if (len_out != NULL) {
			*len_out = 0;
		}
		return NULL;
	}
	char *tail[] = {"stash", "show", "-p", (char *)ref, NULL};
	return gitOpsRunQueryDup(tail, len_out);
}
