#include "workspace/git_view.h"

#include "config/theme_config.h"
#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "editing/text_source.h"
#include "input/prompt.h"
#include "language/syntax.h"
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

static int gitViewKindsAppend(unsigned char **kinds, int *count, int *cap, unsigned char kind) {
	if (*count + 1 > *cap) {
		int new_cap = *cap == 0 ? 64 : *cap * 2;
		unsigned char *grown = editorRealloc(*kinds, (size_t)new_cap);
		if (grown == NULL) {
			return 0;
		}
		*kinds = grown;
		*cap = new_cap;
	}
	(*kinds)[(*count)++] = kind;
	return 1;
}

/* Strips a diff header path ("a/foo", "b/foo", or a bare path) to the file
 * path, or NULL for /dev/null. The returned pointer aliases `field`. */
static const char *gitViewDiffHeaderPath(const char *field) {
	if (field == NULL || strcmp(field, "/dev/null") == 0) {
		return NULL;
	}
	if ((field[0] == 'a' || field[0] == 'b') && field[1] == '/') {
		return field + 2;
	}
	return field;
}

/* Rebuilds a unified diff for display: +/-/space prefixes are stripped (their
 * kind moves into line_kinds_out), per-file metadata collapses into the
 * "diff --git" header line, and hunk headers stay as separators. When the
 * patch touches exactly one file its path lands in source_path_out so the tab
 * can be highlighted with the file's language. */
char *editorGitViewBuildDiffDup(const char *patch, size_t patch_len, unsigned char **line_kinds_out,
                                int *line_kind_count_out, char **source_path_out) {
	if (line_kinds_out != NULL) {
		*line_kinds_out = NULL;
	}
	if (line_kind_count_out != NULL) {
		*line_kind_count_out = 0;
	}
	if (source_path_out != NULL) {
		*source_path_out = NULL;
	}
	if (patch == NULL || line_kinds_out == NULL || line_kind_count_out == NULL) {
		return NULL;
	}

	char *buf = NULL;
	size_t len = 0;
	size_t cap = 0;
	unsigned char *kinds = NULL;
	int kind_count = 0;
	int kind_cap = 0;
	int in_hunk = 0;
	int file_count = 0;
	char first_path[PATH_MAX] = "";
	char minus_path[PATH_MAX] = "";

	size_t pos = 0;
	while (pos < patch_len) {
		size_t line_end = pos;
		while (line_end < patch_len && patch[line_end] != '\n') {
			line_end++;
		}
		size_t line_len = line_end - pos;
		const char *line = patch + pos;
		pos = line_end + 1;
		if (line_len > 0 && line[line_len - 1] == '\r') {
			line_len--;
		}

		char line_copy[4096];
		size_t copy_len =
		        line_len < sizeof(line_copy) - 1 ? line_len : sizeof(line_copy) - 1;
		memcpy(line_copy, line, copy_len);
		line_copy[copy_len] = '\0';

		unsigned char kind = EDITOR_GIT_VIEW_LINE_TEXT;
		const char *emit = line_copy;
		int skip = 0;
		if (strncmp(line_copy, "diff --git ", 11) == 0 ||
		    strncmp(line_copy, "diff --no-index", 15) == 0) {
			in_hunk = 0;
			file_count++;
			kind = EDITOR_GIT_VIEW_LINE_HEADER;
		} else if (in_hunk &&
		           (line_copy[0] == ' ' || line_copy[0] == '+' || line_copy[0] == '-')) {
			kind = line_copy[0] == '+'
			               ? EDITOR_GIT_VIEW_LINE_ADDED
			               : (line_copy[0] == '-' ? EDITOR_GIT_VIEW_LINE_REMOVED
			                                      : EDITOR_GIT_VIEW_LINE_TEXT);
			emit = line_copy + 1;
		} else if (in_hunk && line_copy[0] == '\\') {
			skip = 1; /* "\ No newline at end of file" */
		} else if (strncmp(line_copy, "@@", 2) == 0) {
			in_hunk = 1;
			kind = EDITOR_GIT_VIEW_LINE_HEADER;
		} else if (strncmp(line_copy, "--- ", 4) == 0) {
			(void)snprintf(minus_path, sizeof(minus_path), "%s", line_copy + 4);
			skip = 1;
		} else if (strncmp(line_copy, "+++ ", 4) == 0) {
			const char *path = gitViewDiffHeaderPath(line_copy + 4);
			if (path == NULL) {
				path = gitViewDiffHeaderPath(minus_path);
			}
			if (path != NULL && file_count == 1 && first_path[0] == '\0') {
				(void)snprintf(first_path, sizeof(first_path), "%s", path);
			}
			skip = 1;
		} else if (strncmp(line_copy, "index ", 6) == 0 ||
		           strncmp(line_copy, "old mode", 8) == 0 ||
		           strncmp(line_copy, "new mode", 8) == 0 ||
		           strncmp(line_copy, "deleted file mode", 17) == 0 ||
		           strncmp(line_copy, "new file mode", 13) == 0 ||
		           strncmp(line_copy, "similarity index", 16) == 0 ||
		           strncmp(line_copy, "dissimilarity index", 19) == 0 ||
		           strncmp(line_copy, "rename from", 11) == 0 ||
		           strncmp(line_copy, "rename to", 9) == 0 ||
		           strncmp(line_copy, "copy from", 9) == 0 ||
		           strncmp(line_copy, "copy to", 7) == 0) {
			skip = file_count > 0;
		} else if (strncmp(line_copy, "Binary files", 12) == 0) {
			kind = EDITOR_GIT_VIEW_LINE_HEADER;
		}

		if (skip) {
			continue;
		}
		if (!gitViewAppend(&buf, &len, &cap, emit) ||
		    !gitViewAppend(&buf, &len, &cap, "\n") ||
		    !gitViewKindsAppend(&kinds, &kind_count, &kind_cap, kind)) {
			free(buf);
			free(kinds);
			return NULL;
		}
	}

	if (buf == NULL && !gitViewAppend(&buf, &len, &cap, "")) {
		free(kinds);
		return NULL;
	}
	if (source_path_out != NULL && file_count == 1 && first_path[0] != '\0') {
		*source_path_out = strdup(first_path);
	}
	*line_kinds_out = kinds;
	*line_kind_count_out = kind_count;
	return buf;
}

/* Installs regenerable-patch state on the active (git diff) tab. Takes
 * ownership of every pointer and re-runs syntax setup so the source file's
 * language applies. */
static void gitViewApplyPatchState(unsigned char *kinds, int kind_count, char *source_path,
                                   int regen_kind, char *regen_arg, int whole_file) {
	free(E.git_view_line_kinds);
	E.git_view_line_kinds = kinds;
	E.git_view_line_kind_count = kind_count;
	free(E.git_view_source_path);
	E.git_view_source_path = source_path;
	free(E.git_view_regen_arg);
	E.git_view_regen_arg = regen_arg;
	E.git_view_regen_kind = regen_kind;
	E.git_view_whole_file = whole_file;
	(void)editorSyntaxParseFullActive();
}

static int gitViewOpenPatchTab(enum editorGitOpsPatchKind patch_kind, const char *arg,
                               const char *title, int whole_file) {
	size_t patch_len = 0;
	char *patch = editorGitOpsPatchDup(patch_kind, arg, whole_file, &patch_len);
	if (patch == NULL) {
		editorSetStatusMsg("git: nothing to show for %s", arg);
		return 0;
	}
	unsigned char *kinds = NULL;
	int kind_count = 0;
	char *source_path = NULL;
	char *text = editorGitViewBuildDiffDup(patch, patch_len, &kinds, &kind_count, &source_path);
	free(patch);
	if (text == NULL) {
		editorSetStatusMsg("git: out of memory");
		return 0;
	}
	char *arg_copy = strdup(arg);
	int ok = arg_copy != NULL && editorTabOpenGenerated(EDITOR_TAB_GIT_DIFF, title, text);
	free(text);
	if (!ok) {
		free(kinds);
		free(source_path);
		free(arg_copy);
		return 0;
	}
	gitViewApplyPatchState(kinds, kind_count, source_path, (int)patch_kind, arg_copy,
	                       whole_file);
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	return 1;
}

int editorGitViewOpenDiffForEntry(const char *rel_path, char index_status, char worktree_status) {
	if (rel_path == NULL || rel_path[0] == '\0') {
		return 0;
	}
	int untracked = index_status == '?' && worktree_status == '?';
	int staged = !untracked && index_status != ' ' && index_status != '?';
	enum editorGitOpsPatchKind patch_kind =
	        untracked ? EDITOR_GIT_OPS_PATCH_DIFF_UNTRACKED
	                  : (staged ? EDITOR_GIT_OPS_PATCH_DIFF_CACHED
	                            : EDITOR_GIT_OPS_PATCH_DIFF_WORKTREE);
	char title[PATH_MAX + 16];
	(void)snprintf(title, sizeof(title), "git diff: %s", rel_path);
	return gitViewOpenPatchTab(patch_kind, rel_path, title, 0);
}

static void gitViewRebuildActivePatch(void) {
	if (E.tab_kind != EDITOR_TAB_GIT_DIFF ||
	    E.git_view_regen_kind == EDITOR_GIT_OPS_PATCH_NONE || E.git_view_regen_arg == NULL ||
	    E.tab_title == NULL) {
		return;
	}
	char arg[PATH_MAX];
	char title[PATH_MAX + 16];
	(void)snprintf(arg, sizeof(arg), "%s", E.git_view_regen_arg);
	(void)snprintf(title, sizeof(title), "%s", E.tab_title);
	(void)gitViewOpenPatchTab((enum editorGitOpsPatchKind)E.git_view_regen_kind, arg, title,
	                          E.git_view_whole_file);
}

void editorGitViewToggleDiffContext(void) {
	if (E.tab_kind != EDITOR_TAB_GIT_DIFF ||
	    E.git_view_regen_kind == EDITOR_GIT_OPS_PATCH_NONE) {
		editorSetStatusMsg("Not a regenerable git diff tab");
		return;
	}
	E.git_view_whole_file = !E.git_view_whole_file;
	int whole = E.git_view_whole_file;
	gitViewRebuildActivePatch();
	editorSetStatusMsg(whole ? "Showing whole file" : "Showing changed chunks");
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
		case EDITOR_TAB_GIT_DIFF:
			gitViewRebuildActivePatch();
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
	char title[560];
	(void)snprintf(title, sizeof(title), "git show %s", entity);
	(void)gitViewOpenPatchTab(E.tab_kind == EDITOR_TAB_GIT_LOG
	                                  ? EDITOR_GIT_OPS_PATCH_SHOW_COMMIT
	                                  : EDITOR_GIT_OPS_PATCH_SHOW_STASH,
	                          entity, title, 0);
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

int editorGitViewRowBgColor(int row_idx, struct editorThemeColor *color_out) {
	if (color_out == NULL || row_idx < 0 || row_idx >= E.numrows) {
		return 0;
	}
	switch (E.tab_kind) {
		case EDITOR_TAB_GIT_DIFF:
			if (E.git_view_line_kinds == NULL ||
			    row_idx >= E.git_view_line_kind_count) {
				return 0;
			}
			switch (E.git_view_line_kinds[row_idx]) {
				case EDITOR_GIT_VIEW_LINE_ADDED:
					*color_out = editorThemeGitDiffBgColor(&E.theme, 1);
					return 1;
				case EDITOR_GIT_VIEW_LINE_REMOVED:
					*color_out = editorThemeGitDiffBgColor(&E.theme, 0);
					return 1;
				case EDITOR_GIT_VIEW_LINE_HEADER:
					*color_out = E.theme.ui[EDITOR_THEME_UI_DRAWER_HEADER_BG];
					return 1;
				default:
					return 0;
			}
		case EDITOR_TAB_GIT_BRANCHES:
		case EDITOR_TAB_GIT_LOG:
		case EDITOR_TAB_GIT_STASH:
		case EDITOR_TAB_GIT_COMMIT: {
			struct editorLineView line = {0};
			if (!editorDocumentLineView(E.document, row_idx, &line)) {
				return 0;
			}
			int is_header = line.size > 0 && line.data[0] == '#';
			editorLineViewRelease(&line);
			if (!is_header) {
				return 0;
			}
			*color_out = E.theme.ui[EDITOR_THEME_UI_DRAWER_HEADER_BG];
			return 1;
		}
		default:
			return 0;
	}
}

static void gitViewSpanAdd(struct editorRowSyntaxSpan *spans, int max_spans, int *count, int start,
                           int end, enum editorSyntaxHighlightClass highlight_class) {
	if (end <= start || *count >= max_spans) {
		return;
	}
	spans[*count].start_render_idx = start;
	spans[*count].end_render_idx = end;
	spans[*count].highlight_class = highlight_class;
	(*count)++;
}

/* Synthetic highlight spans for git list views so shas, names, decorations,
 * and metadata read apart without a grammar; header/action-bar rows dim like
 * comments. Spans use render byte indices (view content has no tabs). */
int editorGitViewRowSyntaxSpans(int row_idx, struct editorRowSyntaxSpan *spans, int max_spans,
                                int *count_out) {
	if (count_out != NULL) {
		*count_out = 0;
	}
	if (spans == NULL || count_out == NULL || max_spans <= 0 || row_idx < 0 ||
	    row_idx >= E.numrows) {
		return 0;
	}
	const char *text = E.rows[row_idx].render;
	int size = E.rows[row_idx].rsize;
	if (text == NULL || size <= 0) {
		return 0;
	}

	if (E.tab_kind == EDITOR_TAB_GIT_DIFF) {
		if (E.git_view_line_kinds == NULL || row_idx >= E.git_view_line_kind_count ||
		    E.git_view_line_kinds[row_idx] != EDITOR_GIT_VIEW_LINE_HEADER) {
			return 0;
		}
		gitViewSpanAdd(spans, max_spans, count_out, 0, size, EDITOR_SYNTAX_HL_COMMENT);
		return *count_out > 0;
	}

	if (E.tab_kind != EDITOR_TAB_GIT_BRANCHES && E.tab_kind != EDITOR_TAB_GIT_LOG &&
	    E.tab_kind != EDITOR_TAB_GIT_STASH && E.tab_kind != EDITOR_TAB_GIT_COMMIT) {
		return 0;
	}
	if (text[0] == '#' || text[0] == '(') {
		gitViewSpanAdd(spans, max_spans, count_out, 0, size, EDITOR_SYNTAX_HL_COMMENT);
		return *count_out > 0;
	}
	switch (E.tab_kind) {
		case EDITOR_TAB_GIT_BRANCHES: {
			if (size < 3) {
				return 0;
			}
			int name_end = 2;
			while (name_end < size && text[name_end] != ' ') {
				name_end++;
			}
			gitViewSpanAdd(spans, max_spans, count_out, 0, 2,
			               EDITOR_SYNTAX_HL_CONSTANT);
			gitViewSpanAdd(spans, max_spans, count_out, 2, name_end,
			               text[0] == '*' ? EDITOR_SYNTAX_HL_KEYWORD
			                              : EDITOR_SYNTAX_HL_FUNCTION);
			gitViewSpanAdd(spans, max_spans, count_out, name_end, size,
			               EDITOR_SYNTAX_HL_COMMENT);
			break;
		}
		case EDITOR_TAB_GIT_LOG: {
			int sha_end = 0;
			while (sha_end < size && ((text[sha_end] >= '0' && text[sha_end] <= '9') ||
			                          (text[sha_end] >= 'a' && text[sha_end] <= 'f'))) {
				sha_end++;
			}
			if (sha_end == 0) {
				return 0;
			}
			gitViewSpanAdd(spans, max_spans, count_out, 0, sha_end,
			               EDITOR_SYNTAX_HL_NUMBER);
			int pos = sha_end;
			while (pos < size && text[pos] == ' ') {
				pos++;
			}
			if (pos < size && text[pos] == '(') {
				int deco_end = pos;
				while (deco_end < size && text[deco_end] != ')') {
					deco_end++;
				}
				if (deco_end < size) {
					deco_end++;
				}
				gitViewSpanAdd(spans, max_spans, count_out, pos, deco_end,
				               EDITOR_SYNTAX_HL_STRING);
			}
			/* " — author, age" suffix (em dash is 3 UTF-8 bytes). */
			for (int i = size - 3; i > sha_end; i--) {
				if (memcmp(text + i, "\xE2\x80\x94", 3) == 0) {
					gitViewSpanAdd(spans, max_spans, count_out, i, size,
					               EDITOR_SYNTAX_HL_COMMENT);
					break;
				}
			}
			break;
		}
		case EDITOR_TAB_GIT_STASH: {
			const char *colon = memchr(text, ':', (size_t)size);
			if (colon == NULL) {
				return 0;
			}
			gitViewSpanAdd(spans, max_spans, count_out, 0, (int)(colon - text),
			               EDITOR_SYNTAX_HL_NUMBER);
			break;
		}
		default:
			return 0;
	}
	return *count_out > 0;
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
		case EDITOR_ACTION_GIT_DIFF_TOGGLE_CONTEXT:
			editorGitViewToggleDiffContext();
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
