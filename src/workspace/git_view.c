#include "workspace/git_view.h"

#include "editing/edit.h"
#include "editing/text_source.h"
#include "input/prompt.h"
#include "rotide.h"
#include "support/alloc.h"
#include "text/document.h"
#include "workspace/drawer.h"
#include "workspace/drawer_internal.h"
#include "workspace/git.h"
#include "workspace/git_ops.h"
#include "workspace/tabs.h"
#include "workspace/task.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int gitViewAppend(char **buf, size_t *len, size_t *cap, const char *text) {
	size_t text_len = strlen(text);
	if (*len + text_len + 1 > *cap) {
		size_t new_cap = *cap == 0 ? 256 : *cap;
		while (*len + text_len + 1 > new_cap) {
			new_cap *= 2;
		}
		char *grown = editorRealloc(*buf, new_cap);
		if (grown == NULL) {
			return 0;
		}
		*buf = grown;
		*cap = new_cap;
	}
	memcpy(*buf + *len, text, text_len);
	*len += text_len;
	(*buf)[*len] = '\0';
	return 1;
}

/* Copies the selected drawer file's rel_path into name_out (git ops refresh the
 * entry array, invalidating entry pointers). Returns 0 with a status message
 * when no file row is selected. */
static int gitViewSelectedDrawerFile(char *name_out, size_t name_size, int *staged_group_out,
                                     int *untracked_out) {
	int entry_idx = 0;
	if (!editorDrawerGitSelectedFile(&entry_idx, staged_group_out)) {
		editorSetStatusMsg("Select a file in the Git drawer");
		return 0;
	}
	const struct editorGitEntry *entry = &E.git_entries[entry_idx];
	if (untracked_out != NULL) {
		*untracked_out = entry->index_status == '?' && entry->worktree_status == '?';
	}
	int n = snprintf(name_out, name_size, "%s", entry->rel_path);
	return n > 0 && n < (int)name_size;
}

static void gitViewAfterDrawerOp(void) {
	editorGitRefresh();
	if (E.drawer_mode == EDITOR_DRAWER_MODE_GIT) {
		editorDrawerClampSelectionAndScroll(E.window_rows);
	}
}

static void gitViewStageToggleSelected(int force_unstage) {
	char name[PATH_MAX];
	int staged_group = 0;
	if (!gitViewSelectedDrawerFile(name, sizeof(name), &staged_group, NULL)) {
		return;
	}
	int unstage = force_unstage || staged_group;
	int ok = unstage ? editorGitOpsUnstageFile(name) : editorGitOpsStageFile(name);
	if (ok) {
		gitViewAfterDrawerOp();
		editorSetStatusMsg("%s %s", unstage ? "Unstaged" : "Staged", name);
	}
}

static void gitViewDiscardSelected(void) {
	char name[PATH_MAX];
	int staged_group = 0;
	int untracked = 0;
	if (!gitViewSelectedDrawerFile(name, sizeof(name), &staged_group, &untracked)) {
		return;
	}
	if (!editorPromptYesNo("Discard changes to selected file? [y/N] %s")) {
		editorSetStatusMsg("Discard cancelled");
		return;
	}
	if (editorGitOpsDiscardFile(name, untracked)) {
		gitViewAfterDrawerOp();
		editorSetStatusMsg("Discarded %s", name);
	}
}

static void gitViewStartNetworkTask(const char *subcommand, const char *title,
                                    const char *success_status, const char *failure_status) {
	char cmd[PATH_MAX + 64];
	if (E.git_repo_root == NULL) {
		editorSetStatusMsg("Not a Git repository");
		return;
	}
	if (!editorGitBuildRepoCommand(cmd, sizeof(cmd), subcommand)) {
		editorSetStatusMsg("git: command too long");
		return;
	}
	(void)editorTaskStart(title, cmd, success_status, failure_status);
}

/* Splits one raw line into at most max_fields tab-separated fields (in place:
 * tabs and the line terminator become NULs). Returns the field count. */
static int gitViewSplitFields(char *line, char **fields, int max_fields) {
	int count = 0;
	char *cursor = line;
	while (count < max_fields) {
		fields[count++] = cursor;
		char *tab = strchr(cursor, '\t');
		if (tab == NULL) {
			break;
		}
		*tab = '\0';
		cursor = tab + 1;
	}
	return count;
}

static void gitViewFormatTrack(const char *track, char *buf, size_t buf_size) {
	buf[0] = '\0';
	if (track == NULL || track[0] == '\0') {
		return;
	}
	int ahead = 0;
	int behind = 0;
	const char *ahead_pos = strstr(track, "ahead ");
	const char *behind_pos = strstr(track, "behind ");
	if (ahead_pos != NULL) {
		ahead = (int)strtol(ahead_pos + 6, NULL, 10);
	}
	if (behind_pos != NULL) {
		behind = (int)strtol(behind_pos + 7, NULL, 10);
	}
	if (ahead > 0 && behind > 0) {
		(void)snprintf(buf, buf_size, "↑%d↓%d", ahead, behind);
	} else if (ahead > 0) {
		(void)snprintf(buf, buf_size, "↑%d", ahead);
	} else if (behind > 0) {
		(void)snprintf(buf, buf_size, "↓%d", behind);
	} else if (strstr(track, "gone") != NULL) {
		(void)snprintf(buf, buf_size, "gone");
	}
}

static void gitViewFormatAge(const char *unix_time, time_t now, char *buf, size_t buf_size) {
	buf[0] = '\0';
	if (unix_time == NULL || unix_time[0] == '\0') {
		return;
	}
	time_t then = (time_t)strtoll(unix_time, NULL, 10);
	if (then > 0) {
		(void)editorGitFormatRelativeTime(then, now, buf, buf_size);
	}
}

char *editorGitViewFormatBranchesDup(const char *raw, time_t now) {
	if (raw == NULL) {
		return NULL;
	}
	char *buf = NULL;
	size_t len = 0;
	size_t cap = 0;
	if (!gitViewAppend(&buf, &len, &cap,
	                   "# branches · Enter checkout · n new · d delete · "
	                   "R refresh · P push · p pull · f fetch\n")) {
		goto err;
	}

	char *raw_copy = strdup(raw);
	if (raw_copy == NULL) {
		goto err;
	}
	int local_count = 0;
	int remote_header_emitted = 0;
	char *save = NULL;
	for (char *line = strtok_r(raw_copy, "\n", &save); line != NULL;
	     line = strtok_r(NULL, "\n", &save)) {
		char *fields[6] = {0};
		int field_count = gitViewSplitFields(line, fields, 6);
		if (field_count < 3 || fields[2][0] == '\0') {
			continue;
		}
		const char *name = fields[2];
		int is_remote = strncmp(fields[1], "refs/remotes/", 13) == 0;
		char track[32] = "";
		char age[32] = "";
		if (field_count >= 5) {
			gitViewFormatTrack(fields[4], track, sizeof(track));
		}
		if (field_count >= 6) {
			gitViewFormatAge(fields[5], now, age, sizeof(age));
		}

		if (is_remote && !remote_header_emitted) {
			if (!gitViewAppend(&buf, &len, &cap, "# remotes\n")) {
				free(raw_copy);
				goto err;
			}
			remote_header_emitted = 1;
		}
		if (!is_remote) {
			local_count++;
		}

		char formatted[PATH_MAX + 128];
		size_t pos = 0;
		int n = snprintf(formatted, sizeof(formatted), "%s %s",
		                 fields[0][0] == '*' ? "*" : " ", name);
		if (n <= 0 || n >= (int)sizeof(formatted)) {
			continue;
		}
		pos = (size_t)n;
		if (track[0] != '\0') {
			n = snprintf(formatted + pos, sizeof(formatted) - pos, "  %s", track);
			pos += n > 0 ? (size_t)n : 0;
		}
		if (!is_remote && field_count >= 4 && fields[3][0] != '\0') {
			n = snprintf(formatted + pos, sizeof(formatted) - pos, "  %s", fields[3]);
			pos += n > 0 ? (size_t)n : 0;
		}
		if (age[0] != '\0') {
			n = snprintf(formatted + pos, sizeof(formatted) - pos, "  %s", age);
			pos += n > 0 ? (size_t)n : 0;
		}
		(void)snprintf(formatted + pos, sizeof(formatted) - pos, "\n");
		if (!gitViewAppend(&buf, &len, &cap, formatted)) {
			free(raw_copy);
			goto err;
		}
	}
	free(raw_copy);
	if (local_count == 0) {
		if (!gitViewAppend(&buf, &len, &cap, "(no branches)\n")) {
			goto err;
		}
	}
	return buf;

err:
	free(buf);
	return NULL;
}

char *editorGitViewFormatLogDup(const char *raw, time_t now) {
	if (raw == NULL) {
		return NULL;
	}
	char *buf = NULL;
	size_t len = 0;
	size_t cap = 0;
	if (!gitViewAppend(&buf, &len, &cap,
	                   "# commits · Enter show · c cherry-pick · r revert "
	                   "· t tag · R refresh\n")) {
		goto err;
	}

	char *raw_copy = strdup(raw);
	if (raw_copy == NULL) {
		goto err;
	}
	int commit_count = 0;
	char *save = NULL;
	for (char *line = strtok_r(raw_copy, "\n", &save); line != NULL;
	     line = strtok_r(NULL, "\n", &save)) {
		char *fields[5] = {0};
		int field_count = gitViewSplitFields(line, fields, 5);
		if (field_count < 3 || fields[0][0] == '\0') {
			continue;
		}
		commit_count++;
		char age[32] = "";
		if (field_count >= 5) {
			gitViewFormatAge(fields[4], now, age, sizeof(age));
		}
		const char *decorations = fields[1];
		while (*decorations == ' ') {
			decorations++;
		}

		char formatted[1024];
		int n;
		if (decorations[0] != '\0') {
			n = snprintf(formatted, sizeof(formatted), "%s  %s %s — %s, %s\n",
			             fields[0], decorations, fields[2],
			             field_count >= 4 ? fields[3] : "", age);
		} else {
			n = snprintf(formatted, sizeof(formatted), "%s  %s — %s, %s\n", fields[0],
			             fields[2], field_count >= 4 ? fields[3] : "", age);
		}
		if (n <= 0) {
			continue;
		}
		if (!gitViewAppend(&buf, &len, &cap, formatted)) {
			free(raw_copy);
			goto err;
		}
	}
	free(raw_copy);
	if (commit_count == 0) {
		if (!gitViewAppend(&buf, &len, &cap, "(no commits)\n")) {
			goto err;
		}
	}
	return buf;

err:
	free(buf);
	return NULL;
}

char *editorGitViewFormatStashDup(const char *raw) {
	if (raw == NULL) {
		return NULL;
	}
	char *buf = NULL;
	size_t len = 0;
	size_t cap = 0;
	if (!gitViewAppend(&buf, &len, &cap,
	                   "# stashes · Enter show · a apply · p pop · "
	                   "d drop · R refresh\n")) {
		goto err;
	}
	if (raw[0] == '\0') {
		if (!gitViewAppend(&buf, &len, &cap, "(no stashes)\n")) {
			goto err;
		}
		return buf;
	}
	if (!gitViewAppend(&buf, &len, &cap, raw)) {
		goto err;
	}
	return buf;

err:
	free(buf);
	return NULL;
}

/* Extracts the actionable token (branch name, commit sha, stash ref) from one
 * rendered view line. Header/placeholder lines yield 0. */
int editorGitViewLineEntity(enum editorTabKind kind, const char *line, char *entity_out,
                            size_t entity_size) {
	if (line == NULL || entity_out == NULL || entity_size == 0) {
		return 0;
	}
	entity_out[0] = '\0';
	if (line[0] == '#' || line[0] == '(' || line[0] == '\0') {
		return 0;
	}
	const char *start = NULL;
	const char *end = NULL;
	switch (kind) {
		case EDITOR_TAB_GIT_BRANCHES:
			if (strlen(line) < 3 || (line[0] != '*' && line[0] != ' ')) {
				return 0;
			}
			start = line + 2;
			end = start;
			while (*end != '\0' && *end != ' ') {
				end++;
			}
			break;
		case EDITOR_TAB_GIT_LOG:
			start = line;
			end = start;
			while ((*end >= '0' && *end <= '9') || (*end >= 'a' && *end <= 'f')) {
				end++;
			}
			if (end - start < 4 || (*end != ' ' && *end != '\0')) {
				return 0;
			}
			break;
		case EDITOR_TAB_GIT_STASH:
			if (strncmp(line, "stash@{", 7) != 0) {
				return 0;
			}
			start = line;
			end = strchr(line, ':');
			if (end == NULL) {
				return 0;
			}
			break;
		default:
			return 0;
	}
	size_t entity_len = (size_t)(end - start);
	if (entity_len == 0 || entity_len + 1 > entity_size) {
		return 0;
	}
	memcpy(entity_out, start, entity_len);
	entity_out[entity_len] = '\0';
	return 1;
}

static int gitViewActiveLineEntity(char *entity_out, size_t entity_size) {
	size_t line_len = 0;
	char *line = editorDocumentLineDup(E.document, E.cy, &line_len);
	if (line == NULL) {
		return 0;
	}
	int ok = editorGitViewLineEntity(E.tab_kind, line, entity_out, entity_size);
	free(line);
	if (!ok) {
		editorSetStatusMsg("No entry on this line");
	}
	return ok;
}

void editorGitViewOpenBranches(void) {
	size_t raw_len = 0;
	char *raw = editorGitOpsBranchListRawDup(&raw_len);
	if (raw == NULL) {
		return;
	}
	char *text = editorGitViewFormatBranchesDup(raw, time(NULL));
	free(raw);
	if (text == NULL) {
		editorSetStatusMsg("git: out of memory");
		return;
	}
	if (editorTabOpenGenerated(EDITOR_TAB_GIT_BRANCHES, "git branches", text)) {
		E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	}
	free(text);
}

void editorGitViewOpenLog(void) {
	size_t raw_len = 0;
	char *raw = editorGitOpsLogRawDup(200, &raw_len);
	if (raw == NULL) {
		return;
	}
	char *text = editorGitViewFormatLogDup(raw, time(NULL));
	free(raw);
	if (text == NULL) {
		editorSetStatusMsg("git: out of memory");
		return;
	}
	if (editorTabOpenGenerated(EDITOR_TAB_GIT_LOG, "git log", text)) {
		E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	}
	free(text);
}

void editorGitViewOpenStashes(void) {
	size_t raw_len = 0;
	char *raw = editorGitOpsStashListRawDup(&raw_len);
	if (raw == NULL) {
		return;
	}
	char *text = editorGitViewFormatStashDup(raw);
	free(raw);
	if (text == NULL) {
		editorSetStatusMsg("git: out of memory");
		return;
	}
	if (editorTabOpenGenerated(EDITOR_TAB_GIT_STASH, "git stash", text)) {
		E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	}
	free(text);
}

/* Regenerates the active tab when it is a git view (after any git op). */
static void gitViewRefreshActiveView(void) {
	switch (E.tab_kind) {
		case EDITOR_TAB_GIT_BRANCHES:
			editorGitViewOpenBranches();
			break;
		case EDITOR_TAB_GIT_LOG:
			editorGitViewOpenLog();
			break;
		case EDITOR_TAB_GIT_STASH:
			editorGitViewOpenStashes();
			break;
		default:
			break;
	}
}

static void gitViewActivateSelection(void) {
	char entity[512];
	if (E.tab_kind != EDITOR_TAB_GIT_BRANCHES && E.tab_kind != EDITOR_TAB_GIT_LOG &&
	    E.tab_kind != EDITOR_TAB_GIT_STASH) {
		return;
	}
	size_t line_len = 0;
	char *line = editorDocumentLineDup(E.document, E.cy, &line_len);
	if (line == NULL) {
		return;
	}
	int is_current_branch = line[0] == '*';
	int ok = editorGitViewLineEntity(E.tab_kind, line, entity, sizeof(entity));
	free(line);
	if (!ok) {
		editorSetStatusMsg("No entry on this line");
		return;
	}
	if (E.tab_kind == EDITOR_TAB_GIT_BRANCHES) {
		/* The rendered `*` marker is fresher than any cached branch name. */
		if (is_current_branch) {
			editorSetStatusMsg("Already on %s", entity);
			return;
		}
		if (editorGitOpsCheckout(entity)) {
			gitViewAfterDrawerOp();
			gitViewRefreshActiveView();
			editorSetStatusMsg("Switched to %s", entity);
		}
		return;
	}
	size_t text_len = 0;
	char *text = E.tab_kind == EDITOR_TAB_GIT_LOG ? editorGitOpsShowCommitDup(entity, &text_len)
	                                              : editorGitOpsStashShowDup(entity, &text_len);
	if (text == NULL) {
		editorSetStatusMsg("git: nothing to show for %s", entity);
		return;
	}
	char title[560];
	(void)snprintf(title, sizeof(title), "git show %s", entity);
	(void)editorTabOpenGitDiff(title, text);
	free(text);
}

static void gitViewBranchNew(void) {
	char *name = editorPrompt("New branch name: %s");
	if (name == NULL || name[0] == '\0') {
		free(name);
		editorSetStatusMsg("Branch creation cancelled");
		return;
	}
	if (editorGitOpsBranchCreate(name)) {
		gitViewAfterDrawerOp();
		gitViewRefreshActiveView();
		editorSetStatusMsg("Created and switched to %s", name);
	}
	free(name);
}

static void gitViewBranchDelete(void) {
	char entity[512];
	if (E.tab_kind != EDITOR_TAB_GIT_BRANCHES ||
	    !gitViewActiveLineEntity(entity, sizeof(entity))) {
		return;
	}
	if (!editorPromptYesNo("Delete selected branch? [y/N] %s")) {
		editorSetStatusMsg("Delete cancelled");
		return;
	}
	if (editorGitOpsBranchDelete(entity)) {
		gitViewRefreshActiveView();
		editorSetStatusMsg("Deleted branch %s", entity);
	}
}

static void gitViewHistoryOp(enum editorAction action) {
	char entity[512];
	if (E.tab_kind != EDITOR_TAB_GIT_LOG || !gitViewActiveLineEntity(entity, sizeof(entity))) {
		return;
	}
	if (action == EDITOR_ACTION_GIT_TAG) {
		char *name = editorPrompt("Tag name: %s");
		if (name == NULL || name[0] == '\0') {
			free(name);
			editorSetStatusMsg("Tag cancelled");
			return;
		}
		if (editorGitOpsTag(name, entity)) {
			gitViewRefreshActiveView();
			editorSetStatusMsg("Tagged %s as %s", entity, name);
		}
		free(name);
		return;
	}
	int ok = action == EDITOR_ACTION_GIT_CHERRY_PICK ? editorGitOpsCherryPick(entity)
	                                                 : editorGitOpsRevert(entity);
	if (ok) {
		gitViewAfterDrawerOp();
		gitViewRefreshActiveView();
		editorSetStatusMsg("%s %s",
		                   action == EDITOR_ACTION_GIT_CHERRY_PICK ? "Cherry-picked"
		                                                           : "Reverted",
		                   entity);
	}
}

static void gitViewStashOp(enum editorAction action) {
	char entity[512];
	if (E.tab_kind != EDITOR_TAB_GIT_STASH ||
	    !gitViewActiveLineEntity(entity, sizeof(entity))) {
		return;
	}
	int ok = 0;
	const char *verb = NULL;
	switch (action) {
		case EDITOR_ACTION_GIT_STASH_APPLY:
			ok = editorGitOpsStashApply(entity);
			verb = "Applied";
			break;
		case EDITOR_ACTION_GIT_STASH_POP:
			ok = editorGitOpsStashPop(entity);
			verb = "Popped";
			break;
		case EDITOR_ACTION_GIT_STASH_DROP:
			if (!editorPromptYesNo("Drop selected stash? [y/N] %s")) {
				editorSetStatusMsg("Drop cancelled");
				return;
			}
			ok = editorGitOpsStashDrop(entity);
			verb = "Dropped";
			break;
		default:
			return;
	}
	if (ok) {
		gitViewAfterDrawerOp();
		gitViewRefreshActiveView();
		editorSetStatusMsg("%s %s", verb, entity);
	}
}

static int gitViewHasStagedEntry(void) {
	for (int i = 0; i < E.git_entry_count; i++) {
		char x = E.git_entries[i].index_status;
		if (x != ' ' && x != '?' && x != '\0') {
			return 1;
		}
	}
	return 0;
}

static char *gitViewCommitTemplateDup(int amend) {
	char *buf = NULL;
	size_t len = 0;
	size_t cap = 0;

	if (amend) {
		char *last = editorGitOpsLastCommitMessageDup();
		int ok = gitViewAppend(&buf, &len, &cap, last != NULL ? last : "\n");
		free(last);
		if (!ok) {
			goto err;
		}
	} else if (!gitViewAppend(&buf, &len, &cap, "\n")) {
		goto err;
	}

	if (!gitViewAppend(&buf, &len, &cap,
	                   "# Save to commit, close the tab to abort.\n"
	                   "# Lines starting with '#' are ignored.\n#\n")) {
		goto err;
	}
	const char *branch = editorGitBranch();
	char line[PATH_MAX + 16];
	(void)snprintf(line, sizeof(line), "# On branch %s\n", branch != NULL ? branch : "?");
	if (!gitViewAppend(&buf, &len, &cap, line)) {
		goto err;
	}
	if (!gitViewAppend(&buf, &len, &cap, "# Staged changes:\n")) {
		goto err;
	}
	for (int i = 0; i < E.git_entry_count; i++) {
		char x = E.git_entries[i].index_status;
		if (x == ' ' || x == '?' || x == '\0') {
			continue;
		}
		(void)snprintf(line, sizeof(line), "#   %c  %s\n", x, E.git_entries[i].rel_path);
		if (!gitViewAppend(&buf, &len, &cap, line)) {
			goto err;
		}
	}
	return buf;

err:
	free(buf);
	return NULL;
}

char *editorGitViewCleanCommitMessageDup(const char *text) {
	if (text == NULL) {
		return NULL;
	}
	size_t text_len = strlen(text);
	char *out = editorMalloc(text_len + 1);
	if (out == NULL) {
		return NULL;
	}
	size_t out_len = 0;
	size_t pos = 0;
	while (pos < text_len) {
		size_t line_end = pos;
		while (line_end < text_len && text[line_end] != '\n') {
			line_end++;
		}
		if (text[pos] != '#') {
			size_t copy_len = line_end - pos;
			while (copy_len > 0 && text[pos + copy_len - 1] == '\r') {
				copy_len--;
			}
			memcpy(out + out_len, text + pos, copy_len);
			out_len += copy_len;
			out[out_len++] = '\n';
		}
		pos = line_end + 1;
	}
	while (out_len > 0 &&
	       (out[out_len - 1] == '\n' || out[out_len - 1] == ' ' || out[out_len - 1] == '\t')) {
		out_len--;
	}
	out[out_len] = '\0';
	return out;
}

void editorGitViewOpenCommit(int amend) {
	if (E.git_repo_root == NULL) {
		editorSetStatusMsg("Not a Git repository");
		return;
	}
	editorGitRefresh();
	if (!amend && !gitViewHasStagedEntry()) {
		editorSetStatusMsg("No staged changes to commit");
		return;
	}
	char *text = gitViewCommitTemplateDup(amend);
	if (text == NULL) {
		editorSetStatusMsg("git: out of memory");
		return;
	}
	const char *title = amend ? "git commit --amend" : "git commit";
	if (editorTabOpenGenerated(EDITOR_TAB_GIT_COMMIT, title, text)) {
		E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
		editorSetStatusMsg("Type the commit message; save to commit, close tab to abort");
	}
	free(text);
}

void editorGitViewCommitFromActiveTab(void) {
	if (E.tab_kind != EDITOR_TAB_GIT_COMMIT) {
		return;
	}
	int amend = E.tab_title != NULL && strstr(E.tab_title, "--amend") != NULL;
	size_t text_len = 0;
	char *text = editorDupActiveTextSource(&text_len);
	char *message = editorGitViewCleanCommitMessageDup(text);
	free(text);
	if (message == NULL || message[0] == '\0') {
		free(message);
		editorSetStatusMsg("Aborting commit due to empty commit message");
		return;
	}
	char sha[16] = "";
	if (!editorGitOpsCommit(message, amend, sha, sizeof(sha))) {
		free(message);
		return;
	}
	free(message);
	E.dirty = 0;
	(void)editorTabCloseActive();
	gitViewAfterDrawerOp();
	const char *branch = editorGitBranch();
	editorSetStatusMsg("[%s %s] committed", branch != NULL ? branch : "?", sha);
}

int editorGitViewHandleMappedAction(enum editorAction action) {
	switch (action) {
		case EDITOR_ACTION_GIT_COMMIT:
			editorGitViewOpenCommit(0);
			return 1;
		case EDITOR_ACTION_GIT_COMMIT_AMEND:
			editorGitViewOpenCommit(1);
			return 1;
		case EDITOR_ACTION_GIT_STAGE:
			gitViewStageToggleSelected(0);
			return 1;
		case EDITOR_ACTION_GIT_UNSTAGE:
			gitViewStageToggleSelected(1);
			return 1;
		case EDITOR_ACTION_GIT_STAGE_ALL:
			if (editorGitOpsStageAll()) {
				gitViewAfterDrawerOp();
				editorSetStatusMsg("Staged all changes");
			}
			return 1;
		case EDITOR_ACTION_GIT_DISCARD:
			gitViewDiscardSelected();
			return 1;
		case EDITOR_ACTION_GIT_REFRESH:
			if (E.git_repo_root == NULL) {
				editorSetStatusMsg("Not a Git repository");
				return 1;
			}
			gitViewAfterDrawerOp();
			gitViewRefreshActiveView();
			editorSetStatusMsg("Git status refreshed");
			return 1;
		case EDITOR_ACTION_GIT_BRANCHES:
			editorGitViewOpenBranches();
			return 1;
		case EDITOR_ACTION_GIT_LOG:
			editorGitViewOpenLog();
			return 1;
		case EDITOR_ACTION_GIT_STASHES:
			editorGitViewOpenStashes();
			return 1;
		case EDITOR_ACTION_GIT_VIEW_ACTIVATE:
			gitViewActivateSelection();
			return 1;
		case EDITOR_ACTION_GIT_BRANCH_NEW:
			gitViewBranchNew();
			return 1;
		case EDITOR_ACTION_GIT_BRANCH_DELETE:
			gitViewBranchDelete();
			return 1;
		case EDITOR_ACTION_GIT_CHERRY_PICK:
		case EDITOR_ACTION_GIT_REVERT:
		case EDITOR_ACTION_GIT_TAG:
			gitViewHistoryOp(action);
			return 1;
		case EDITOR_ACTION_GIT_STASH_APPLY:
		case EDITOR_ACTION_GIT_STASH_POP:
		case EDITOR_ACTION_GIT_STASH_DROP:
			gitViewStashOp(action);
			return 1;
		case EDITOR_ACTION_GIT_PUSH:
			gitViewStartNetworkTask("push", "git push", "Push finished", "Push failed");
			return 1;
		case EDITOR_ACTION_GIT_PULL:
			gitViewStartNetworkTask("pull", "git pull", "Pull finished", "Pull failed");
			return 1;
		case EDITOR_ACTION_GIT_FETCH:
			gitViewStartNetworkTask("fetch", "git fetch", "Fetch finished",
			                        "Fetch failed");
			return 1;
		default:
			return 0;
	}
}
