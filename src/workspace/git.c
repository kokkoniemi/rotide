#include "workspace/git.h"

#include "rotide.h"
#include "support/alloc.h"
#include "support/file_io.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void gitFreeEntries(void) {
	for (int i = 0; i < E.git_entry_count; i++) {
		free(E.git_entries[i].rel_path);
	}
	free(E.git_entries);
	E.git_entries = NULL;
	E.git_entry_count = 0;
	E.git_entry_capacity = 0;
}

static void gitRefreshBranch(void) {
	char head_path[PATH_MAX];
	int n = 0;

	free(E.git_branch);
	E.git_branch = NULL;
	if (E.git_repo_root == NULL) {
		return;
	}

	n = snprintf(head_path, sizeof(head_path), "%s/.git/HEAD", E.git_repo_root);
	if (n <= 0 || n >= (int)sizeof(head_path)) {
		return;
	}

	FILE *f = fopen(head_path, "r");
	if (f == NULL) {
		return;
	}

	char line[256];
	if (fgets(line, sizeof(line), f) != NULL) {
		size_t len = strlen(line);
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
			line[--len] = '\0';
		}
		const char *ref_prefix = "ref: refs/heads/";
		size_t prefix_len = strlen(ref_prefix);
		if (strncmp(line, ref_prefix, prefix_len) == 0) {
			E.git_branch = strdup(line + prefix_len);
		} else if (len >= 7) {
			char sha_short[8];
			memcpy(sha_short, line, 7);
			sha_short[7] = '\0';
			E.git_branch = strdup(sha_short);
		}
	}
	(void)fclose(f);
}

static int gitEntryCompare(const void *a, const void *b) {
	const struct editorGitEntry *ea = (const struct editorGitEntry *)a;
	const struct editorGitEntry *eb = (const struct editorGitEntry *)b;
	return strcmp(ea->rel_path, eb->rel_path);
}

struct gitEntryCache {
	struct editorGitEntry *entries;
	int count;
	int capacity;
};

static void gitEntryCacheFree(struct gitEntryCache *cache) {
	if (cache == NULL) {
		return;
	}
	for (int i = 0; i < cache->count; i++) {
		free(cache->entries[i].rel_path);
	}
	free(cache->entries);
	cache->entries = NULL;
	cache->count = 0;
	cache->capacity = 0;
}

static enum editorGitStatus gitStatusFromXY(char x, char y) {
	if (x == 'U' || y == 'U' || (x == 'A' && y == 'A') || (x == 'D' && y == 'D')) {
		return EDITOR_GIT_STATUS_CONFLICT;
	}
	if (x == '?' && y == '?') {
		return EDITOR_GIT_STATUS_UNTRACKED;
	}
	if (x == '!' && y == '!') {
		return EDITOR_GIT_STATUS_CLEAN;
	}
	if (x != ' ' || y != ' ') {
		return EDITOR_GIT_STATUS_MODIFIED;
	}
	return EDITOR_GIT_STATUS_CLEAN;
}

static int gitEntryCacheAdd(struct gitEntryCache *cache, const char *rel_path,
                            enum editorGitStatus status, char index_status, char worktree_status) {
	if (cache->count >= cache->capacity) {
		int new_cap = cache->capacity == 0 ? 16 : cache->capacity * 2;
		struct editorGitEntry *new_entries = editorRealloc(
		        cache->entries, (size_t)new_cap * sizeof(struct editorGitEntry));
		if (new_entries == NULL) {
			return 0;
		}
		cache->entries = new_entries;
		cache->capacity = new_cap;
	}
	char *path_dup = strdup(rel_path);
	if (path_dup == NULL) {
		return 0;
	}
	cache->entries[cache->count].rel_path = path_dup;
	cache->entries[cache->count].status = status;
	cache->entries[cache->count].index_status = index_status;
	cache->entries[cache->count].worktree_status = worktree_status;
	cache->count++;
	return 1;
}

int editorGitInit(void) {
	char *cwd = editorPathCwdDup();
	if (cwd == NULL) {
		return 0;
	}

	const char *markers[] = {".git"};
	char *repo_root = editorPathFindMarkerUpward(cwd, markers, 1);
	free(cwd);

	if (repo_root == NULL) {
		return 1;
	}

	E.git_repo_root = repo_root;

	gitRefreshBranch();

	editorGitRefresh();
	return 1;
}

static int gitRefreshBuildStatusCommand(char *cmd, size_t cmd_size) {
	size_t pos = 0;
	const char *prefix = "git -C '";
	size_t prefix_len = strlen(prefix);
	if (prefix_len >= cmd_size) {
		return 0;
	}
	memcpy(cmd, prefix, prefix_len);
	cmd[prefix_len] = '\0';
	pos = prefix_len;
	for (const char *p = E.git_repo_root; *p != '\0'; p++) {
		if (*p == '\'') {
			if (pos + 4 >= cmd_size) {
				return 0;
			}
			cmd[pos++] = '\'';
			cmd[pos++] = '\\';
			cmd[pos++] = '\'';
			cmd[pos++] = '\'';
		} else {
			if (pos + 1 >= cmd_size) {
				return 0;
			}
			cmd[pos++] = *p;
		}
	}
	const char *suffix = "' status --porcelain=v1 -z --untracked-files=normal 2>/dev/null";
	size_t suffix_len = strlen(suffix);
	if (pos + suffix_len + 1 >= cmd_size) {
		return 0;
	}
	memcpy(cmd + pos, suffix, suffix_len + 1);
	return 1;
}

static FILE *gitRefreshSpawnStatus(void) {
	char cmd[PATH_MAX * 2 + 64];
	if (!gitRefreshBuildStatusCommand(cmd, sizeof(cmd))) {
		return NULL;
	}
	FILE *fp = popen(cmd, "r");
	return fp;
}

static char *gitRefreshReadStatus(FILE *fp, size_t *buf_len_out) {
	if (buf_len_out != NULL) {
		*buf_len_out = 0;
	}
	size_t buf_cap = 4096;
	size_t buf_len = 0;
	char *buf = editorMalloc(buf_cap);
	if (buf == NULL) {
		return NULL;
	}

	while (!feof(fp)) {
		if (buf_len + 1 >= buf_cap) {
			size_t new_cap = buf_cap * 2;
			char *new_buf = editorRealloc(buf, new_cap);
			if (new_buf == NULL) {
				break;
			}
			buf = new_buf;
			buf_cap = new_cap;
		}
		size_t n_read = fread(buf + buf_len, 1, buf_cap - buf_len - 1, fp);
		if (n_read == 0) {
			break;
		}
		buf_len += n_read;
	}
	if (buf_len_out != NULL) {
		*buf_len_out = buf_len;
	}
	return buf;
}

static void gitRefreshParseStatus(char *buf, size_t buf_len, struct gitEntryCache *cache) {
	size_t p = 0;
	while (p < buf_len) {
		if (p + 3 > buf_len) {
			break;
		}
		char x = buf[p];
		char y = buf[p + 1];
		if (buf[p + 2] != ' ') {
			break;
		}
		p += 3;

		size_t path_start = p;
		while (p < buf_len && buf[p] != '\0') {
			p++;
		}
		if (p >= buf_len) {
			break;
		}
		buf[p] = '\0';
		const char *rel_path = buf + path_start;
		p++;

		// Renames/copies have an extra NUL-terminated original path
		if (x == 'R' || x == 'C' || y == 'R' || y == 'C') {
			while (p < buf_len && buf[p] != '\0') {
				p++;
			}
			p++;
		}

		enum editorGitStatus status = gitStatusFromXY(x, y);
		if (status != EDITOR_GIT_STATUS_CLEAN) {
			(void)gitEntryCacheAdd(cache, rel_path, status, x, y);
		}
	}
}

static void gitRefreshSortCache(struct gitEntryCache *cache) {
	if (cache->count > 1) {
		qsort(cache->entries, (size_t)cache->count, sizeof(struct editorGitEntry),
		      gitEntryCompare);
	}
}

static void gitRefreshSwapCache(struct gitEntryCache *cache) {
	gitFreeEntries();
	E.git_entries = cache->entries;
	E.git_entry_count = cache->count;
	E.git_entry_capacity = cache->capacity;
	cache->entries = NULL;
	cache->count = 0;
	cache->capacity = 0;
}

void editorGitRefresh(void) {
	if (E.git_repo_root == NULL) {
		return;
	}

	gitRefreshBranch();
	gitFreeEntries();

	FILE *fp = gitRefreshSpawnStatus();
	if (fp == NULL) {
		return;
	}

	size_t buf_len = 0;
	char *buf = gitRefreshReadStatus(fp, &buf_len);
	pclose(fp);
	if (buf == NULL) {
		return;
	}

	struct gitEntryCache cache = {0};
	gitRefreshParseStatus(buf, buf_len, &cache);
	free(buf);
	gitRefreshSortCache(&cache);
	gitRefreshSwapCache(&cache);
	gitEntryCacheFree(&cache);
}

void editorGitFree(void) {
	free(E.git_repo_root);
	E.git_repo_root = NULL;
	free(E.git_branch);
	E.git_branch = NULL;
	gitFreeEntries();
}

const char *editorGitBranch(void) {
	return E.git_branch;
}

enum editorGitStatus editorGitFileStatus(const char *abs_path) {
	if (E.git_repo_root == NULL || E.git_entries == NULL || E.git_entry_count == 0 ||
	    abs_path == NULL) {
		return EDITOR_GIT_STATUS_CLEAN;
	}

	size_t root_len = strlen(E.git_repo_root);
	if (strncmp(abs_path, E.git_repo_root, root_len) != 0) {
		return EDITOR_GIT_STATUS_CLEAN;
	}

	const char *rel = abs_path + root_len;
	if (*rel == '/') {
		rel++;
	}

	int lo = 0;
	int hi = E.git_entry_count - 1;
	while (lo <= hi) {
		int mid = lo + (hi - lo) / 2;
		int cmp = strcmp(E.git_entries[mid].rel_path, rel);
		if (cmp == 0) {
			return E.git_entries[mid].status;
		} else if (cmp < 0) {
			lo = mid + 1;
		} else {
			hi = mid - 1;
		}
	}
	return EDITOR_GIT_STATUS_CLEAN;
}

static int gitAppendShellQuotedArg(char *cmd, size_t cmd_size, size_t *pos, const char *value) {
	if (cmd == NULL || pos == NULL || value == NULL) {
		return 0;
	}
	size_t cur = *pos;
	if (cur + 1 >= cmd_size) {
		return 0;
	}
	cmd[cur++] = '\'';
	for (const char *p = value; *p != '\0'; p++) {
		if (*p == '\'') {
			if (cur + 4 >= cmd_size) {
				return 0;
			}
			cmd[cur++] = '\'';
			cmd[cur++] = '\\';
			cmd[cur++] = '\'';
			cmd[cur++] = '\'';
		} else {
			if (cur + 1 >= cmd_size) {
				return 0;
			}
			cmd[cur++] = *p;
		}
	}
	if (cur + 1 >= cmd_size) {
		return 0;
	}
	cmd[cur++] = '\'';
	*pos = cur;
	return 1;
}

static int gitAppendLiteral(char *cmd, size_t cmd_size, size_t *pos, const char *literal) {
	size_t literal_len = strlen(literal);
	if (*pos + literal_len + 1 > cmd_size) {
		return 0;
	}
	memcpy(cmd + *pos, literal, literal_len);
	*pos += literal_len;
	cmd[*pos] = '\0';
	return 1;
}

char *editorGitGenerateDiff(const char *rel_path, char index_status, char worktree_status,
                            size_t *len_out) {
	if (len_out != NULL) {
		*len_out = 0;
	}
	if (rel_path == NULL || rel_path[0] == '\0' || E.git_repo_root == NULL) {
		return NULL;
	}

	int is_untracked = (index_status == '?' && worktree_status == '?');
	int has_staged = !is_untracked && index_status != ' ' && index_status != '?';
	int has_worktree = !is_untracked && worktree_status != ' ' && worktree_status != '?';
	(void)has_worktree;

	char cmd[PATH_MAX * 4 + 256];
	size_t pos = 0;

	if (is_untracked) {
		// For untracked files, simulate a diff against /dev/null using git diff --no-index.
		if (!gitAppendLiteral(cmd, sizeof(cmd), &pos, "git -C ") ||
		    !gitAppendShellQuotedArg(cmd, sizeof(cmd), &pos, E.git_repo_root) ||
		    !gitAppendLiteral(cmd, sizeof(cmd), &pos,
		                      " --no-pager diff --no-color --no-index -- /dev/null ") ||
		    !gitAppendShellQuotedArg(cmd, sizeof(cmd), &pos, rel_path) ||
		    !gitAppendLiteral(cmd, sizeof(cmd), &pos, " 2>/dev/null")) {
			return NULL;
		}
	} else {
		const char *diff_subcommand = has_staged
		                                      ? " --no-pager diff --no-color --cached -- "
		                                      : " --no-pager diff --no-color -- ";
		if (!gitAppendLiteral(cmd, sizeof(cmd), &pos, "git -C ") ||
		    !gitAppendShellQuotedArg(cmd, sizeof(cmd), &pos, E.git_repo_root) ||
		    !gitAppendLiteral(cmd, sizeof(cmd), &pos, diff_subcommand) ||
		    !gitAppendShellQuotedArg(cmd, sizeof(cmd), &pos, rel_path) ||
		    !gitAppendLiteral(cmd, sizeof(cmd), &pos, " 2>/dev/null")) {
			return NULL;
		}
	}

	FILE *fp = popen(cmd, "r");
	if (fp == NULL) {
		return NULL;
	}

	size_t buf_cap = 4096;
	size_t buf_len = 0;
	char *buf = editorMalloc(buf_cap);
	if (buf == NULL) {
		pclose(fp);
		return NULL;
	}
	while (!feof(fp)) {
		if (buf_len + 1 >= buf_cap) {
			size_t new_cap = buf_cap * 2;
			char *new_buf = editorRealloc(buf, new_cap);
			if (new_buf == NULL) {
				free(buf);
				pclose(fp);
				return NULL;
			}
			buf = new_buf;
			buf_cap = new_cap;
		}
		size_t n_read = fread(buf + buf_len, 1, buf_cap - buf_len - 1, fp);
		if (n_read == 0) {
			break;
		}
		buf_len += n_read;
	}
	pclose(fp);
	buf[buf_len] = '\0';

	if (buf_len == 0) {
		const char *empty = "(no changes to display)\n";
		size_t empty_len = strlen(empty);
		char *replacement = editorMalloc(empty_len + 1);
		if (replacement == NULL) {
			free(buf);
			return NULL;
		}
		memcpy(replacement, empty, empty_len + 1);
		free(buf);
		buf = replacement;
		buf_len = empty_len;
	}

	if (len_out != NULL) {
		*len_out = buf_len;
	}
	return buf;
}

enum editorGitStatus editorGitDirStatus(const char *abs_path) {
	if (E.git_repo_root == NULL || E.git_entries == NULL || E.git_entry_count == 0 ||
	    abs_path == NULL) {
		return EDITOR_GIT_STATUS_CLEAN;
	}

	size_t root_len = strlen(E.git_repo_root);
	if (strncmp(abs_path, E.git_repo_root, root_len) != 0) {
		return EDITOR_GIT_STATUS_CLEAN;
	}

	const char *rel = abs_path + root_len;
	if (*rel == '/') {
		rel++;
	}
	size_t rel_len = strlen(rel);

	enum editorGitStatus worst = EDITOR_GIT_STATUS_CLEAN;
	for (int i = 0; i < E.git_entry_count; i++) {
		const char *entry_path = E.git_entries[i].rel_path;
		int matches = (rel_len == 0) || (strncmp(entry_path, rel, rel_len) == 0 &&
		                                 entry_path[rel_len] == '/');
		if (matches) {
			enum editorGitStatus s = E.git_entries[i].status;
			if (s > worst) {
				worst = s;
			}
			if (worst == EDITOR_GIT_STATUS_CONFLICT) {
				break;
			}
		}
	}
	return worst;
}
