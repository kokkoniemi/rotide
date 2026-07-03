#include "editor_test_api.h"
#include "rotide.h"
#include "test_case.h"
#include "test_helpers.h"
#include "workspace/git.h"
#include "workspace/git_ops.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

/* Integration tests run against a throwaway repo under /tmp; they skip (pass
 * with a note) when no git binary is available. */

static int git_ops_run_cmd(const char *fmt, ...) {
	char cmd[1024];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(cmd, sizeof(cmd), fmt, ap);
	va_end(ap);
	if (n <= 0 || n >= (int)sizeof(cmd)) {
		return 0;
	}
	int status = system(cmd);
	return status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int git_ops_git_available(void) {
	static int checked = 0;
	static int available = 0;
	if (!checked) {
		checked = 1;
		available = git_ops_run_cmd("git --version >/dev/null 2>&1");
	}
	return available;
}

#define SKIP_WITHOUT_GIT()                                                                         \
	do {                                                                                       \
		if (!git_ops_git_available()) {                                                    \
			(void)fprintf(stderr, "%s: skipped (no git binary)\n", __func__);          \
			return 0;                                                                  \
		}                                                                                  \
	} while (0)

static int git_ops_write_file(const char *repo, const char *rel_path, const char *content) {
	char path[512];
	int n = snprintf(path, sizeof(path), "%s/%s", repo, rel_path);
	if (n <= 0 || n >= (int)sizeof(path)) {
		return 0;
	}
	FILE *f = fopen(path, "w");
	if (f == NULL) {
		return 0;
	}
	int ok = fputs(content, f) >= 0;
	ok = (fclose(f) == 0) && ok;
	return ok;
}

/* Creates a repo with one committed file a.txt, points E.git_repo_root at it,
 * and refreshes the git snapshot. Returns a malloc'd repo path or NULL. */
static char *git_ops_test_repo_create(void) {
	setenv("GIT_CONFIG_GLOBAL", "/dev/null", 1);
	setenv("GIT_CONFIG_NOSYSTEM", "1", 1);

	char dir_template[] = "/tmp/rotide-test-git-XXXXXX";
	char *dir = mkdtemp(dir_template);
	if (dir == NULL) {
		return NULL;
	}
	if (!git_ops_run_cmd("git -C '%s' init -q", dir) ||
	    !git_ops_run_cmd("git -C '%s' symbolic-ref HEAD refs/heads/main", dir) ||
	    !git_ops_run_cmd("git -C '%s' config user.name test", dir) ||
	    !git_ops_run_cmd("git -C '%s' config user.email test@test", dir) ||
	    !git_ops_write_file(dir, "a.txt", "one\n") ||
	    !git_ops_run_cmd("git -C '%s' add a.txt", dir) ||
	    !git_ops_run_cmd("git -C '%s' commit -q -m initial", dir)) {
		(void)git_ops_run_cmd("rm -rf '%s'", dir);
		return NULL;
	}
	free(E.git_repo_root);
	E.git_repo_root = strdup(dir);
	editorGitRefresh();
	return strdup(dir);
}

static void git_ops_test_repo_destroy(char *repo) {
	if (repo != NULL && strncmp(repo, "/tmp/rotide-test-git-", 21) == 0) {
		(void)git_ops_run_cmd("rm -rf '%s'", repo);
	}
	free(repo);
	editorGitFree();
}

static const struct editorGitEntry *git_ops_find_entry(const char *rel_path) {
	for (int i = 0; i < E.git_entry_count; i++) {
		if (strcmp(E.git_entries[i].rel_path, rel_path) == 0) {
			return &E.git_entries[i];
		}
	}
	return NULL;
}

static int test_git_ops_stage_and_unstage_file(void) {
	SKIP_WITHOUT_GIT();
	reset_editor_state();
	char *repo = git_ops_test_repo_create();
	ASSERT_TRUE(repo != NULL);

	ASSERT_TRUE(git_ops_write_file(repo, "a.txt", "two\n"));
	editorGitRefresh();
	const struct editorGitEntry *entry = git_ops_find_entry("a.txt");
	ASSERT_TRUE(entry != NULL);
	ASSERT_EQ_INT('M', entry->worktree_status);

	ASSERT_TRUE(editorGitOpsStageFile("a.txt"));
	editorGitRefresh();
	entry = git_ops_find_entry("a.txt");
	ASSERT_TRUE(entry != NULL);
	ASSERT_EQ_INT('M', entry->index_status);

	ASSERT_TRUE(editorGitOpsUnstageFile("a.txt"));
	editorGitRefresh();
	entry = git_ops_find_entry("a.txt");
	ASSERT_TRUE(entry != NULL);
	ASSERT_EQ_INT('M', entry->worktree_status);
	ASSERT_TRUE(entry->index_status != 'M');

	git_ops_test_repo_destroy(repo);
	return 0;
}

static int test_git_ops_stage_all_covers_untracked(void) {
	SKIP_WITHOUT_GIT();
	reset_editor_state();
	char *repo = git_ops_test_repo_create();
	ASSERT_TRUE(repo != NULL);

	ASSERT_TRUE(git_ops_write_file(repo, "b.txt", "new\n"));
	editorGitRefresh();
	const struct editorGitEntry *entry = git_ops_find_entry("b.txt");
	ASSERT_TRUE(entry != NULL);
	ASSERT_EQ_INT(EDITOR_GIT_STATUS_UNTRACKED, entry->status);

	ASSERT_TRUE(editorGitOpsStageAll());
	editorGitRefresh();
	entry = git_ops_find_entry("b.txt");
	ASSERT_TRUE(entry != NULL);
	ASSERT_EQ_INT('A', entry->index_status);

	git_ops_test_repo_destroy(repo);
	return 0;
}

static int test_git_ops_commit_and_amend(void) {
	SKIP_WITHOUT_GIT();
	reset_editor_state();
	char *repo = git_ops_test_repo_create();
	ASSERT_TRUE(repo != NULL);

	ASSERT_TRUE(git_ops_write_file(repo, "a.txt", "two\n"));
	ASSERT_TRUE(editorGitOpsStageFile("a.txt"));
	char sha[16] = "";
	ASSERT_TRUE(editorGitOpsCommit("second change", 0, sha, sizeof(sha)));
	ASSERT_TRUE(sha[0] != '\0');

	char *message = editorGitOpsLastCommitMessageDup();
	ASSERT_TRUE(message != NULL);
	ASSERT_TRUE(strstr(message, "second change") != NULL);
	free(message);

	ASSERT_TRUE(editorGitOpsCommit("second change reworded", 1, NULL, 0));
	message = editorGitOpsLastCommitMessageDup();
	ASSERT_TRUE(message != NULL);
	ASSERT_TRUE(strstr(message, "reworded") != NULL);
	free(message);

	size_t log_len = 0;
	char *log = editorGitOpsLogRawDup(10, &log_len);
	ASSERT_TRUE(log != NULL);
	ASSERT_TRUE(strstr(log, "reworded") != NULL);
	ASSERT_TRUE(strstr(log, "initial") != NULL);
	/* Amend must not add a commit: exactly the initial + amended one. */
	int newline_count = 0;
	for (size_t i = 0; i < log_len; i++) {
		if (log[i] == '\n') {
			newline_count++;
		}
	}
	ASSERT_EQ_INT(2, newline_count);
	free(log);

	git_ops_test_repo_destroy(repo);
	return 0;
}

static int test_git_ops_commit_nothing_staged_fails(void) {
	SKIP_WITHOUT_GIT();
	reset_editor_state();
	char *repo = git_ops_test_repo_create();
	ASSERT_TRUE(repo != NULL);

	E.statusmsg[0] = '\0';
	ASSERT_TRUE(!editorGitOpsCommit("nothing here", 0, NULL, 0));
	ASSERT_TRUE(E.statusmsg[0] != '\0');

	git_ops_test_repo_destroy(repo);
	return 0;
}

static int test_git_ops_branch_round_trip(void) {
	SKIP_WITHOUT_GIT();
	reset_editor_state();
	char *repo = git_ops_test_repo_create();
	ASSERT_TRUE(repo != NULL);

	ASSERT_TRUE(editorGitOpsBranchCreate("feat"));
	editorGitRefresh();
	ASSERT_EQ_STR("feat", editorGitBranch());

	size_t raw_len = 0;
	char *raw = editorGitOpsBranchListRawDup(&raw_len);
	ASSERT_TRUE(raw != NULL);
	ASSERT_TRUE(strstr(raw, "feat") != NULL);
	ASSERT_TRUE(strstr(raw, "main") != NULL);
	free(raw);

	ASSERT_TRUE(editorGitOpsCheckout("main"));
	editorGitRefresh();
	ASSERT_EQ_STR("main", editorGitBranch());

	ASSERT_TRUE(editorGitOpsBranchDelete("feat"));
	raw = editorGitOpsBranchListRawDup(&raw_len);
	ASSERT_TRUE(raw != NULL);
	ASSERT_TRUE(strstr(raw, "feat") == NULL);
	free(raw);

	git_ops_test_repo_destroy(repo);
	return 0;
}

/* Ref/tag names git would parse as options must be refused before reaching argv
 * (these subcommands take the name positionally, so "--" cannot shield it). A
 * regression would let e.g. checkout "-f" through and mutate the working tree. */
static int test_git_ops_rejects_option_like_names(void) {
	SKIP_WITHOUT_GIT();
	reset_editor_state();
	char *repo = git_ops_test_repo_create();
	ASSERT_TRUE(repo != NULL);

	ASSERT_TRUE(!editorGitOpsBranchCreate("-b"));
	ASSERT_TRUE(!editorGitOpsCheckout("-f"));
	ASSERT_TRUE(!editorGitOpsBranchDelete("-D"));
	ASSERT_TRUE(!editorGitOpsTag("-d", "HEAD"));

	/* Nothing ran: still exactly branch main, HEAD unmoved. */
	editorGitRefresh();
	ASSERT_EQ_STR("main", editorGitBranch());
	size_t raw_len = 0;
	char *raw = editorGitOpsBranchListRawDup(&raw_len);
	ASSERT_TRUE(raw != NULL);
	ASSERT_TRUE(strstr(raw, "main") != NULL);
	free(raw);

	git_ops_test_repo_destroy(repo);
	return 0;
}

static int test_git_ops_branch_delete_unmerged_fails(void) {
	SKIP_WITHOUT_GIT();
	reset_editor_state();
	char *repo = git_ops_test_repo_create();
	ASSERT_TRUE(repo != NULL);

	ASSERT_TRUE(editorGitOpsBranchCreate("feat"));
	ASSERT_TRUE(git_ops_write_file(repo, "a.txt", "feat change\n"));
	ASSERT_TRUE(editorGitOpsStageFile("a.txt"));
	ASSERT_TRUE(editorGitOpsCommit("feat commit", 0, NULL, 0));
	ASSERT_TRUE(editorGitOpsCheckout("main"));

	E.statusmsg[0] = '\0';
	ASSERT_TRUE(!editorGitOpsBranchDelete("feat"));
	ASSERT_TRUE(strncmp(E.statusmsg, "git: ", 5) == 0);

	git_ops_test_repo_destroy(repo);
	return 0;
}

static int test_git_ops_stash_round_trip(void) {
	SKIP_WITHOUT_GIT();
	reset_editor_state();
	char *repo = git_ops_test_repo_create();
	ASSERT_TRUE(repo != NULL);

	ASSERT_TRUE(git_ops_write_file(repo, "a.txt", "stash me\n"));
	ASSERT_TRUE(git_ops_run_cmd("git -C '%s' stash push -q -m wip", repo));

	size_t raw_len = 0;
	char *raw = editorGitOpsStashListRawDup(&raw_len);
	ASSERT_TRUE(raw != NULL);
	ASSERT_TRUE(strstr(raw, "stash@{0}") != NULL);
	ASSERT_TRUE(strstr(raw, "wip") != NULL);
	free(raw);

	char *shown =
	        editorGitOpsPatchDup(EDITOR_GIT_OPS_PATCH_SHOW_STASH, "stash@{0}", 0, &raw_len);
	ASSERT_TRUE(shown != NULL);
	ASSERT_TRUE(strstr(shown, "a.txt") != NULL);
	free(shown);

	ASSERT_TRUE(editorGitOpsStashApply("stash@{0}"));
	editorGitRefresh();
	ASSERT_TRUE(git_ops_find_entry("a.txt") != NULL);
	ASSERT_TRUE(editorGitOpsDiscardFile("a.txt", 0));

	ASSERT_TRUE(editorGitOpsStashDrop("stash@{0}"));
	raw = editorGitOpsStashListRawDup(&raw_len);
	ASSERT_TRUE(raw != NULL);
	ASSERT_EQ_INT(0, (int)raw_len);
	free(raw);

	ASSERT_TRUE(git_ops_write_file(repo, "a.txt", "stash me again\n"));
	ASSERT_TRUE(git_ops_run_cmd("git -C '%s' stash push -q -m wip2", repo));
	ASSERT_TRUE(editorGitOpsStashPop("stash@{0}"));
	raw = editorGitOpsStashListRawDup(&raw_len);
	ASSERT_TRUE(raw != NULL);
	ASSERT_EQ_INT(0, (int)raw_len);
	free(raw);
	editorGitRefresh();
	ASSERT_TRUE(git_ops_find_entry("a.txt") != NULL);

	git_ops_test_repo_destroy(repo);
	return 0;
}

static int test_git_ops_discard_tracked_and_untracked(void) {
	SKIP_WITHOUT_GIT();
	reset_editor_state();
	char *repo = git_ops_test_repo_create();
	ASSERT_TRUE(repo != NULL);

	ASSERT_TRUE(git_ops_write_file(repo, "a.txt", "dirty\n"));
	editorGitRefresh();
	ASSERT_TRUE(git_ops_find_entry("a.txt") != NULL);
	ASSERT_TRUE(editorGitOpsDiscardFile("a.txt", 0));
	editorGitRefresh();
	ASSERT_TRUE(git_ops_find_entry("a.txt") == NULL);

	ASSERT_TRUE(git_ops_write_file(repo, "junk.txt", "junk\n"));
	editorGitRefresh();
	ASSERT_TRUE(git_ops_find_entry("junk.txt") != NULL);
	ASSERT_TRUE(editorGitOpsDiscardFile("junk.txt", 1));
	editorGitRefresh();
	ASSERT_TRUE(git_ops_find_entry("junk.txt") == NULL);

	git_ops_test_repo_destroy(repo);
	return 0;
}

static int test_git_ops_history_operations(void) {
	SKIP_WITHOUT_GIT();
	reset_editor_state();
	char *repo = git_ops_test_repo_create();
	ASSERT_TRUE(repo != NULL);

	/* Tag the initial commit. */
	size_t log_len = 0;
	char *log = editorGitOpsLogRawDup(1, &log_len);
	ASSERT_TRUE(log != NULL);
	char sha[16] = "";
	size_t sha_len = 0;
	while (sha_len < sizeof(sha) - 1 && log[sha_len] != '\0' && log[sha_len] != '\t') {
		sha[sha_len] = log[sha_len];
		sha_len++;
	}
	sha[sha_len] = '\0';
	free(log);
	ASSERT_TRUE(sha_len > 0);
	ASSERT_TRUE(editorGitOpsTag("v1", sha));
	log = editorGitOpsLogRawDup(1, &log_len);
	ASSERT_TRUE(log != NULL);
	ASSERT_TRUE(strstr(log, "tag: v1") != NULL);
	free(log);

	char *shown = editorGitOpsPatchDup(EDITOR_GIT_OPS_PATCH_SHOW_COMMIT, sha, 0, &log_len);
	ASSERT_TRUE(shown != NULL);
	ASSERT_TRUE(strstr(shown, "initial") != NULL);
	free(shown);

	/* Commit on a feature branch, then cherry-pick it onto main. */
	ASSERT_TRUE(editorGitOpsBranchCreate("feat"));
	ASSERT_TRUE(git_ops_write_file(repo, "c.txt", "cherry\n"));
	ASSERT_TRUE(editorGitOpsStageFile("c.txt"));
	char feat_sha[16] = "";
	ASSERT_TRUE(editorGitOpsCommit("cherry commit", 0, feat_sha, sizeof(feat_sha)));
	ASSERT_TRUE(editorGitOpsCheckout("main"));
	ASSERT_TRUE(editorGitOpsCherryPick(feat_sha));
	log = editorGitOpsLogRawDup(1, &log_len);
	ASSERT_TRUE(log != NULL);
	ASSERT_TRUE(strstr(log, "cherry commit") != NULL);
	free(log);

	/* Revert the cherry-picked commit; subject references the original. */
	ASSERT_TRUE(editorGitOpsRevert("HEAD"));
	log = editorGitOpsLogRawDup(1, &log_len);
	ASSERT_TRUE(log != NULL);
	ASSERT_TRUE(strstr(log, "Revert") != NULL);
	free(log);

	git_ops_test_repo_destroy(repo);
	return 0;
}

static int test_git_ops_parse_status_v2_fixture(void) {
	reset_editor_state();
	/* NUL-terminated records as emitted by `status --porcelain=v2 --branch -z`. */
	static const char data[] = "# branch.oid abcdef\0"
	                           "# branch.head main\0"
	                           "# branch.ab +3 -1\0"
	                           "1 .M N... 100644 100644 100644 aaa bbb a.txt\0"
	                           "1 M. N... 100644 100644 100644 aaa bbb b.txt\0"
	                           "2 R. N... 100644 100644 100644 aaa bbb R100 new.txt\0old.txt\0"
	                           "u UU N... 100644 100644 100644 100644 a1 a2 a3 conflict.txt\0"
	                           "? untracked.txt\0"
	                           "! ignored.txt\0";
	int ahead = 0;
	int behind = 0;
	ASSERT_TRUE(editorGitTestParseStatus(data, sizeof(data) - 1, &ahead, &behind));
	ASSERT_EQ_INT(3, ahead);
	ASSERT_EQ_INT(1, behind);
	ASSERT_EQ_INT(5, E.git_entry_count);

	const struct editorGitEntry *entry = git_ops_find_entry("a.txt");
	ASSERT_TRUE(entry != NULL);
	ASSERT_EQ_INT(' ', entry->index_status);
	ASSERT_EQ_INT('M', entry->worktree_status);

	entry = git_ops_find_entry("b.txt");
	ASSERT_TRUE(entry != NULL);
	ASSERT_EQ_INT('M', entry->index_status);
	ASSERT_EQ_INT(' ', entry->worktree_status);

	entry = git_ops_find_entry("new.txt");
	ASSERT_TRUE(entry != NULL);
	ASSERT_EQ_INT('R', entry->index_status);

	entry = git_ops_find_entry("conflict.txt");
	ASSERT_TRUE(entry != NULL);
	ASSERT_EQ_INT(EDITOR_GIT_STATUS_CONFLICT, entry->status);

	entry = git_ops_find_entry("untracked.txt");
	ASSERT_TRUE(entry != NULL);
	ASSERT_EQ_INT(EDITOR_GIT_STATUS_UNTRACKED, entry->status);

	ASSERT_TRUE(git_ops_find_entry("ignored.txt") == NULL);
	ASSERT_TRUE(git_ops_find_entry("old.txt") == NULL);

	editorGitFree();
	return 0;
}

static int test_git_ops_parse_status_v2_no_upstream(void) {
	reset_editor_state();
	static const char data[] = "# branch.oid abcdef\0"
	                           "# branch.head main\0"
	                           "? untracked.txt\0";
	int ahead = -1;
	int behind = -1;
	ASSERT_TRUE(editorGitTestParseStatus(data, sizeof(data) - 1, &ahead, &behind));
	ASSERT_EQ_INT(0, ahead);
	ASSERT_EQ_INT(0, behind);
	ASSERT_EQ_INT(1, E.git_entry_count);
	editorGitFree();
	return 0;
}

static int test_git_ops_without_repo_sets_statusmsg(void) {
	SKIP_WITHOUT_GIT();
	reset_editor_state();
	free(E.git_repo_root);
	E.git_repo_root = NULL;

	E.statusmsg[0] = '\0';
	ASSERT_TRUE(!editorGitOpsStageFile("a.txt"));
	ASSERT_EQ_STR("Not a Git repository", E.statusmsg);

	E.statusmsg[0] = '\0';
	size_t len = 0;
	ASSERT_TRUE(editorGitOpsBranchListRawDup(&len) == NULL);
	ASSERT_EQ_STR("Not a Git repository", E.statusmsg);
	return 0;
}

const struct editorTestCase g_git_ops_tests[] = {
        {"git_ops_stage_and_unstage_file", test_git_ops_stage_and_unstage_file},
        {"git_ops_stage_all_covers_untracked", test_git_ops_stage_all_covers_untracked},
        {"git_ops_commit_and_amend", test_git_ops_commit_and_amend},
        {"git_ops_commit_nothing_staged_fails", test_git_ops_commit_nothing_staged_fails},
        {"git_ops_branch_round_trip", test_git_ops_branch_round_trip},
        {"git_ops_rejects_option_like_names", test_git_ops_rejects_option_like_names},
        {"git_ops_branch_delete_unmerged_fails", test_git_ops_branch_delete_unmerged_fails},
        {"git_ops_stash_round_trip", test_git_ops_stash_round_trip},
        {"git_ops_discard_tracked_and_untracked", test_git_ops_discard_tracked_and_untracked},
        {"git_ops_history_operations", test_git_ops_history_operations},
        {"git_ops_parse_status_v2_fixture", test_git_ops_parse_status_v2_fixture},
        {"git_ops_parse_status_v2_no_upstream", test_git_ops_parse_status_v2_no_upstream},
        {"git_ops_without_repo_sets_statusmsg", test_git_ops_without_repo_sets_statusmsg},
};

const int g_git_ops_test_count = (int)(sizeof(g_git_ops_tests) / sizeof(g_git_ops_tests[0]));
