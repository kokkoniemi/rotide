#include "workspace/git.h"

#include "rotide.h"
#include "support/alloc.h"
#include "support/file_io.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
	GIT_BLAME_MAX_OUTPUT_BYTES = 65536,
	GIT_BLAME_MAX_FIELD_BYTES = 4096,
	GIT_BLAME_SHORT_SHA_BYTES = 12
};

static int gitAppendShellQuotedArg(char *cmd, size_t cmd_size, size_t *pos, const char *value);
static int gitAppendLiteral(char *cmd, size_t cmd_size, size_t *pos, const char *literal);
static void gitBlameFileCacheReset(void);
static int gitReplaceString(char **slot, const char *value);
static char *gitDupStringLimited(const char *s);
static char *gitDupLimited(const char *data, size_t len);
static int gitBlameParseField(char *line, struct editorGitBlameLine *out);
static int gitShaIsAllZero(const char *sha);
static char *gitRelativePathDup(const char *abs_path);
static char *gitReadCappedCommandOutput(FILE *fp, size_t max_len, size_t *len_out);

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
	free(E.git_head);
	E.git_head = NULL;
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

static void gitRefreshHead(void) {
	if (E.git_repo_root == NULL) {
		return;
	}

	char cmd[PATH_MAX + 128];
	size_t pos = 0;
	if (!gitAppendLiteral(cmd, sizeof(cmd), &pos, "git -C ") ||
	    !gitAppendShellQuotedArg(cmd, sizeof(cmd), &pos, E.git_repo_root) ||
	    !gitAppendLiteral(cmd, sizeof(cmd), &pos, " rev-parse --verify HEAD 2>/dev/null")) {
		return;
	}

	FILE *fp = popen(cmd, "r");
	if (fp == NULL) {
		return;
	}

	char line[64];
	if (fgets(line, sizeof(line), fp) != NULL) {
		size_t len = strlen(line);
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
			line[--len] = '\0';
		}
		if (len > 0) {
			E.git_head = strdup(line);
		}
	}
	(void)pclose(fp);
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

enum editorGitStatus editorGitStatusFromChar(char c) {
	switch (c) {
		case 'A':
			return EDITOR_GIT_STATUS_ADDED;
		case 'D':
			return EDITOR_GIT_STATUS_DELETED;
		case '?':
			return EDITOR_GIT_STATUS_UNTRACKED;
		case 'U':
			return EDITOR_GIT_STATUS_CONFLICT;
		case ' ':
		case '.':
		case '\0':
			return EDITOR_GIT_STATUS_CLEAN;
		default:
			/* M, R, C, T, … */
			return EDITOR_GIT_STATUS_MODIFIED;
	}
}

/* Explicit severity so directory rollup color is independent of enum order:
 * conflict always wins, then the more disruptive worktree states. */
static int gitStatusSeverity(enum editorGitStatus status) {
	switch (status) {
		case EDITOR_GIT_STATUS_CLEAN:
			return 0;
		case EDITOR_GIT_STATUS_MODIFIED:
			return 1;
		case EDITOR_GIT_STATUS_ADDED:
			return 2;
		case EDITOR_GIT_STATUS_DELETED:
			return 3;
		case EDITOR_GIT_STATUS_UNTRACKED:
			return 4;
		case EDITOR_GIT_STATUS_CONFLICT:
			return 5;
	}
	return 0;
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
	const char *suffix = "' status --porcelain=v2 --branch -z --untracked-files=normal"
	                     " 2>/dev/null";
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

/* Advances past `count` space-separated fields; returns the next field or
 * NULL when the record is shorter than expected. */
static const char *gitStatusSkipFields(const char *cursor, int count) {
	for (int i = 0; i < count; i++) {
		cursor = strchr(cursor, ' ');
		if (cursor == NULL) {
			return NULL;
		}
		cursor++;
	}
	return cursor;
}

/* Porcelain v2 uses '.' for "unchanged"; v1 (and the drawer logic) use ' '. */
static char gitStatusChar(char c) {
	return c == '.' ? ' ' : c;
}

static void gitStatusParseAheadBehind(const char *header, int *ahead_out, int *behind_out) {
	const char *plus = strchr(header, '+');
	const char *minus = strchr(header, '-');
	if (ahead_out != NULL && plus != NULL) {
		*ahead_out = (int)strtol(plus + 1, NULL, 10);
	}
	if (behind_out != NULL && minus != NULL) {
		*behind_out = (int)strtol(minus + 1, NULL, 10);
	}
}

/* Parses `git status --porcelain=v2 --branch -z` output: NUL-terminated
 * records; `# branch.ab +A -B` carries ahead/behind; `2` (rename) records are
 * followed by one extra NUL-terminated original-path field. */
static void gitRefreshParseStatus(char *buf, size_t buf_len, struct gitEntryCache *cache,
                                  int *ahead_out, int *behind_out) {
	size_t p = 0;
	while (p < buf_len) {
		char *rec = buf + p;
		size_t rec_len = 0;
		while (p + rec_len < buf_len && rec[rec_len] != '\0') {
			rec_len++;
		}
		rec[rec_len] = '\0';
		p += rec_len + 1;
		if (rec_len == 0) {
			continue;
		}

		char x = ' ';
		char y = ' ';
		const char *rel_path = NULL;
		switch (rec[0]) {
			case '#':
				if (strncmp(rec, "# branch.ab ", 12) == 0) {
					gitStatusParseAheadBehind(rec + 12, ahead_out, behind_out);
				}
				continue;
			case '1':
			case '2':
			case 'u': {
				if (rec_len < 4) {
					continue;
				}
				x = gitStatusChar(rec[2]);
				y = gitStatusChar(rec[3]);
				int skip = rec[0] == '1' ? 7 : (rec[0] == '2' ? 8 : 9);
				rel_path = gitStatusSkipFields(rec + 2, skip);
				if (rec[0] == '2') {
					/* Consume the original-path record. */
					while (p < buf_len && buf[p] != '\0') {
						p++;
					}
					p++;
				}
				break;
			}
			case '?':
				if (rec_len < 3) {
					continue;
				}
				x = '?';
				y = '?';
				rel_path = rec + 2;
				break;
			default:
				continue;
		}
		if (rel_path == NULL || rel_path[0] == '\0') {
			continue;
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

	editorGitBlameCacheClearAll();
	gitRefreshBranch();
	gitRefreshHead();
	gitFreeEntries();
	E.git_ahead = 0;
	E.git_behind = 0;

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
	gitRefreshParseStatus(buf, buf_len, &cache, &E.git_ahead, &E.git_behind);
	free(buf);
	gitRefreshSortCache(&cache);
	gitRefreshSwapCache(&cache);
	gitEntryCacheFree(&cache);
}

/* Test hook: runs the porcelain-v2 parser on a fixture buffer and swaps the
 * result into E.git_entries / E.git_ahead / E.git_behind. */
int editorGitTestParseStatus(const char *data, size_t len, int *ahead_out, int *behind_out) {
	char *buf = editorMalloc(len + 1);
	if (buf == NULL) {
		return 0;
	}
	memcpy(buf, data, len);
	buf[len] = '\0';

	E.git_ahead = 0;
	E.git_behind = 0;
	struct gitEntryCache cache = {0};
	gitRefreshParseStatus(buf, len, &cache, &E.git_ahead, &E.git_behind);
	free(buf);
	gitRefreshSortCache(&cache);
	gitRefreshSwapCache(&cache);
	gitEntryCacheFree(&cache);
	if (ahead_out != NULL) {
		*ahead_out = E.git_ahead;
	}
	if (behind_out != NULL) {
		*behind_out = E.git_behind;
	}
	return 1;
}

void editorGitFree(void) {
	editorGitBlameCacheClearAll();
	gitBlameFileCacheReset();
	free(E.git_repo_root);
	E.git_repo_root = NULL;
	free(E.git_branch);
	E.git_branch = NULL;
	free(E.git_head);
	E.git_head = NULL;
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

int editorGitBuildRepoCommand(char *cmd, size_t cmd_size, const char *args_literal) {
	if (cmd == NULL || args_literal == NULL || E.git_repo_root == NULL) {
		return 0;
	}
	size_t pos = 0;
	return gitAppendLiteral(cmd, cmd_size, &pos, "git -C ") &&
	       gitAppendShellQuotedArg(cmd, cmd_size, &pos, E.git_repo_root) &&
	       gitAppendLiteral(cmd, cmd_size, &pos, " ") &&
	       gitAppendLiteral(cmd, cmd_size, &pos, args_literal);
}

static void gitBlameLineClear(struct editorGitBlameLine *line) {
	if (line == NULL) {
		return;
	}
	free(line->commit_sha);
	free(line->short_sha);
	free(line->author_name);
	free(line->author_email);
	free(line->committer_name);
	free(line->summary);
	free(line->filename);
	free(line->original_path);
	memset(line, 0, sizeof(*line));
}

void editorGitBlameLineFree(struct editorGitBlameLine *line) {
	gitBlameLineClear(line);
}

void editorGitBlameCacheClear(struct editorBuffer *buffer) {
	if (buffer == NULL) {
		return;
	}
	if (buffer->git_blame_line != NULL) {
		editorGitBlameLineFree(buffer->git_blame_line);
		free(buffer->git_blame_line);
		buffer->git_blame_line = NULL;
	}
	free(buffer->git_blame_filename);
	free(buffer->git_blame_repo_root);
	free(buffer->git_blame_branch);
	free(buffer->git_blame_head);
	buffer->git_blame_filename = NULL;
	buffer->git_blame_repo_root = NULL;
	buffer->git_blame_branch = NULL;
	buffer->git_blame_head = NULL;
	memset(&buffer->git_blame_disk_state, 0, sizeof(buffer->git_blame_disk_state));
	buffer->git_blame_line_number = 0;
	buffer->git_blame_line_miss = 0;
}

void editorGitBlameCacheClearAll(void) {
	/* The whole-file cache is intentionally not cleared here: its key
	 * (branch/head/disk_state) already forces a reload when blame actually
	 * changes, so a routine git refresh (e.g. from a file-watch tick) keeps
	 * the in-memory blame instead of respawning `git blame`. */
	editorGitBlameCacheClear(&E.active_buffer);
	if (E.tabs == NULL) {
		return;
	}
	for (int i = 0; i < E.tab_count; i++) {
		if (i == E.active_tab) {
			continue;
		}
		editorGitBlameCacheClear(&E.tabs[i].buffer);
	}
}

static int gitBlameCacheStringEqual(const char *a, const char *b) {
	if (a == NULL || b == NULL) {
		return a == b;
	}
	return strcmp(a, b) == 0;
}

static int gitBlameTimeEqual(struct timespec left, struct timespec right) {
	return left.tv_sec == right.tv_sec && left.tv_nsec == right.tv_nsec;
}

static int gitBlameDiskStateEqual(const struct editorFileDiskState *left,
                                  const struct editorFileDiskState *right) {
	if (left == NULL || right == NULL) {
		return 0;
	}
	if (left->known != right->known || left->exists != right->exists) {
		return 0;
	}
	if (!left->known || !left->exists) {
		return 1;
	}
	return left->dev == right->dev && left->ino == right->ino && left->size == right->size &&
	       gitBlameTimeEqual(left->mtime, right->mtime) &&
	       gitBlameTimeEqual(left->ctime, right->ctime);
}

static int gitBlameActiveCacheMatches(int one_based_line) {
	return E.git_blame_line_number == one_based_line &&
	       gitBlameCacheStringEqual(E.git_blame_filename, E.filename) &&
	       gitBlameCacheStringEqual(E.git_blame_repo_root, E.git_repo_root) &&
	       gitBlameCacheStringEqual(E.git_blame_branch, E.git_branch) &&
	       gitBlameCacheStringEqual(E.git_blame_head, E.git_head) &&
	       gitBlameDiskStateEqual(&E.git_blame_disk_state, &E.disk_state);
}

static int gitBlameStoreActiveCacheKey(int one_based_line) {
	E.git_blame_line_number = one_based_line;
	E.git_blame_filename = E.filename != NULL ? strdup(E.filename) : NULL;
	E.git_blame_repo_root = E.git_repo_root != NULL ? strdup(E.git_repo_root) : NULL;
	E.git_blame_branch = E.git_branch != NULL ? strdup(E.git_branch) : NULL;
	E.git_blame_head = E.git_head != NULL ? strdup(E.git_head) : NULL;
	E.git_blame_disk_state = E.disk_state;
	if ((E.filename != NULL && E.git_blame_filename == NULL) ||
	    (E.git_repo_root != NULL && E.git_blame_repo_root == NULL) ||
	    (E.git_branch != NULL && E.git_blame_branch == NULL) ||
	    (E.git_head != NULL && E.git_blame_head == NULL)) {
		editorGitBlameCacheClear(&E.active_buffer);
		return 0;
	}
	return 1;
}

/* Incremental blame emits commit metadata once but terminates every group with
 * its filename. Keep commits deduplicated while retaining group-specific paths. */
enum { GIT_BLAME_FILE_MAX_LINES = 2000000, GIT_BLAME_FILE_MAX_OUTPUT_BYTES = 4 * 1024 * 1024 };

enum gitBlameFileState {
	GIT_BLAME_FILE_EMPTY = 0,  /* no key stored */
	GIT_BLAME_FILE_LOADED,     /* commits/lines populated for the stored key */
	GIT_BLAME_FILE_UNAVAILABLE /* key stored but too big / failed: use fallback */
};

struct gitBlameLineRef {
	int commit_idx;
	int group_idx;
	int orig_line;
};

struct gitBlameGroup {
	char *filename;
};

struct gitBlameFileCache {
	enum gitBlameFileState state;
	char *filename;
	char *repo_root;
	char *branch;
	char *head;
	struct editorFileDiskState disk_state;
	struct editorGitBlameLine *commits;
	int commit_count;
	int commit_capacity;
	struct gitBlameGroup *groups;
	int group_count;
	int group_capacity;
	struct gitBlameLineRef *lines;
	int line_count;
	int line_capacity;
};

static struct gitBlameFileCache g_git_blame_file_cache;
static long g_git_blame_load_count;

static void gitBlameFileCacheClearData(struct gitBlameFileCache *c) {
	for (int i = 0; i < c->commit_count; i++) {
		gitBlameLineClear(&c->commits[i]);
	}
	free(c->commits);
	c->commits = NULL;
	c->commit_count = 0;
	c->commit_capacity = 0;
	for (int i = 0; i < c->group_count; i++) {
		free(c->groups[i].filename);
	}
	free(c->groups);
	c->groups = NULL;
	c->group_count = 0;
	c->group_capacity = 0;
	free(c->lines);
	c->lines = NULL;
	c->line_count = 0;
	c->line_capacity = 0;
	c->state = GIT_BLAME_FILE_EMPTY;
}

static void gitBlameFileCacheReset(void) {
	struct gitBlameFileCache *c = &g_git_blame_file_cache;
	gitBlameFileCacheClearData(c);
	free(c->filename);
	free(c->repo_root);
	free(c->branch);
	free(c->head);
	c->filename = NULL;
	c->repo_root = NULL;
	c->branch = NULL;
	c->head = NULL;
	memset(&c->disk_state, 0, sizeof(c->disk_state));
}

static int gitBlameFileCacheKeyMatchesActive(const struct gitBlameFileCache *c) {
	return gitBlameCacheStringEqual(c->filename, E.filename) &&
	       gitBlameCacheStringEqual(c->repo_root, E.git_repo_root) &&
	       gitBlameCacheStringEqual(c->branch, E.git_branch) &&
	       gitBlameCacheStringEqual(c->head, E.git_head) &&
	       gitBlameDiskStateEqual(&c->disk_state, &E.disk_state);
}

static int gitBlameFileCacheStoreKey(struct gitBlameFileCache *c) {
	c->filename = E.filename != NULL ? strdup(E.filename) : NULL;
	c->repo_root = E.git_repo_root != NULL ? strdup(E.git_repo_root) : NULL;
	c->branch = E.git_branch != NULL ? strdup(E.git_branch) : NULL;
	c->head = E.git_head != NULL ? strdup(E.git_head) : NULL;
	c->disk_state = E.disk_state;
	return !((E.filename != NULL && c->filename == NULL) ||
	         (E.git_repo_root != NULL && c->repo_root == NULL) ||
	         (E.git_branch != NULL && c->branch == NULL) ||
	         (E.git_head != NULL && c->head == NULL));
}

static int gitBlameHexDigit(char c) {
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static size_t gitBlameIncrementalObjectIdLength(const char *line) {
	const char *space = strchr(line, ' ');
	if (space == NULL) {
		return 0;
	}
	size_t len = (size_t)(space - line);
	if (len != 40 && len != 64) {
		return 0;
	}
	for (size_t i = 0; i < len; i++) {
		if (!gitBlameHexDigit(line[i])) {
			return 0;
		}
	}
	return len;
}

static int gitBlameCommitCopy(struct editorGitBlameLine *out,
                              const struct editorGitBlameLine *src) {
	gitBlameLineClear(out);
	if ((src->commit_sha != NULL && !gitReplaceString(&out->commit_sha, src->commit_sha)) ||
	    (src->short_sha != NULL && !gitReplaceString(&out->short_sha, src->short_sha)) ||
	    (src->author_name != NULL && !gitReplaceString(&out->author_name, src->author_name)) ||
	    (src->author_email != NULL &&
	     !gitReplaceString(&out->author_email, src->author_email)) ||
	    (src->committer_name != NULL &&
	     !gitReplaceString(&out->committer_name, src->committer_name)) ||
	    (src->summary != NULL && !gitReplaceString(&out->summary, src->summary)) ||
	    (src->original_path != NULL &&
	     !gitReplaceString(&out->original_path, src->original_path))) {
		gitBlameLineClear(out);
		return 0;
	}
	out->author_time = src->author_time;
	out->committer_time = src->committer_time;
	return 1;
}

/* Find the commit with `sha` (reverse scan — the same commit tends to recur in
 * nearby groups), or append a new entry seeded with sha/short_sha. Sets *is_new
 * so the caller knows whether the following metadata lines populate it. */
static int gitBlameFileCacheFindOrAddCommit(struct gitBlameFileCache *c, const char *sha,
                                            int *is_new) {
	*is_new = 0;
	for (int i = c->commit_count - 1; i >= 0; i--) {
		if (c->commits[i].commit_sha != NULL &&
		    strcmp(c->commits[i].commit_sha, sha) == 0) {
			return i;
		}
	}
	if (c->commit_count == c->commit_capacity) {
		int new_cap = c->commit_capacity < 16 ? 16 : c->commit_capacity * 2;
		struct editorGitBlameLine *grown =
		        editorRealloc(c->commits, (size_t)new_cap * sizeof(*grown));
		if (grown == NULL) {
			return -1;
		}
		c->commits = grown;
		c->commit_capacity = new_cap;
	}
	struct editorGitBlameLine *entry = &c->commits[c->commit_count];
	memset(entry, 0, sizeof(*entry));
	entry->commit_sha = gitDupStringLimited(sha);
	size_t sha_len = strlen(sha);
	size_t short_len =
	        sha_len < GIT_BLAME_SHORT_SHA_BYTES ? sha_len : GIT_BLAME_SHORT_SHA_BYTES;
	entry->short_sha = gitDupLimited(sha, short_len);
	if (entry->commit_sha == NULL || entry->short_sha == NULL) {
		gitBlameLineClear(entry);
		return -1;
	}
	*is_new = 1;
	return c->commit_count++;
}

static int gitBlameFileCacheEnsureLineCapacity(struct gitBlameFileCache *c, int needed) {
	if (needed <= c->line_capacity) {
		return 1;
	}
	int new_cap = c->line_capacity < 256 ? 256 : c->line_capacity;
	while (new_cap < needed) {
		new_cap *= 2;
	}
	struct gitBlameLineRef *grown = editorRealloc(c->lines, (size_t)new_cap * sizeof(*grown));
	if (grown == NULL) {
		return 0;
	}
	for (int i = c->line_capacity; i < new_cap; i++) {
		grown[i].commit_idx = -1;
		grown[i].group_idx = -1;
		grown[i].orig_line = 0;
	}
	c->lines = grown;
	c->line_capacity = new_cap;
	return 1;
}

static int gitBlameFileCacheAddGroup(struct gitBlameFileCache *c) {
	if (c->group_count == c->group_capacity) {
		int new_cap = c->group_capacity < 16 ? 16 : c->group_capacity * 2;
		struct gitBlameGroup *grown =
		        editorRealloc(c->groups, (size_t)new_cap * sizeof(*grown));
		if (grown == NULL) {
			return -1;
		}
		c->groups = grown;
		c->group_capacity = new_cap;
	}
	struct gitBlameGroup *group = &c->groups[c->group_count];
	memset(group, 0, sizeof(*group));
	return c->group_count++;
}

static int gitBlameFileCacheAssignLines(struct gitBlameFileCache *c, long final_line,
                                        long num_lines, int commit_idx, int group_idx,
                                        long orig_line) {
	if (final_line < 1 || num_lines < 1 || orig_line < 1 ||
	    final_line > GIT_BLAME_FILE_MAX_LINES || num_lines > GIT_BLAME_FILE_MAX_LINES ||
	    num_lines - 1 > GIT_BLAME_FILE_MAX_LINES - final_line || orig_line > INT_MAX ||
	    num_lines - 1 > INT_MAX - orig_line) {
		return 0;
	}
	long last = final_line + num_lines - 1;
	if (!gitBlameFileCacheEnsureLineCapacity(c, (int)last)) {
		return 0;
	}
	for (long i = 0; i < num_lines; i++) {
		int idx = (int)(final_line - 1 + i);
		c->lines[idx].commit_idx = commit_idx;
		c->lines[idx].group_idx = group_idx;
		c->lines[idx].orig_line = (int)(orig_line + i);
	}
	if ((int)last > c->line_count) {
		c->line_count = (int)last;
	}
	return 1;
}

/* Parse `git blame --incremental` output into deduped commits + a per-line
 * index. Returns 1 on success (at least one blamed line). */
static int gitBlameParseIncremental(const char *data, size_t len, struct gitBlameFileCache *c) {
	if (data == NULL || len == 0) {
		return 0;
	}
	char *buf = editorMalloc(len + 1);
	if (buf == NULL) {
		return 0;
	}
	memcpy(buf, data, len);
	buf[len] = '\0';

	int ok = 1;
	int cur = -1;
	int cur_group = -1;
	int cur_is_new = 0;
	char *line = buf;
	while (line != NULL) {
		char *next = strchr(line, '\n');
		if (next != NULL) {
			*next++ = '\0';
		}
		size_t line_len = strlen(line);
		if (line_len > 0 && line[line_len - 1] == '\r') {
			line[line_len - 1] = '\0';
		}
		size_t object_id_len = gitBlameIncrementalObjectIdLength(line);
		if (object_id_len > 0) {
			char sha[65];
			memcpy(sha, line, object_id_len);
			sha[object_id_len] = '\0';
			long orig = 0;
			long final = 0;
			long num = 0;
			// NOLINTNEXTLINE(cert-err34-c)
			if (sscanf(line + object_id_len, " %ld %ld %ld", &orig, &final, &num) !=
			    3) {
				ok = 0;
				break;
			}
			cur_group = gitBlameFileCacheAddGroup(c);
			if (gitShaIsAllZero(sha)) {
				cur = -1;
				cur_is_new = 0;
			} else {
				cur = gitBlameFileCacheFindOrAddCommit(c, sha, &cur_is_new);
			}
			if (cur_group < 0 || (!gitShaIsAllZero(sha) && cur < 0) ||
			    !gitBlameFileCacheAssignLines(c, final, num, cur, cur_group, orig)) {
				ok = 0;
				break;
			}
		} else if (cur_group >= 0 && strncmp(line, "filename ", 9) == 0) {
			if (!gitReplaceString(&c->groups[cur_group].filename, line + 9)) {
				ok = 0;
				break;
			}
		} else if (cur_is_new && cur >= 0 && line[0] != '\0') {
			if (!gitBlameParseField(line, &c->commits[cur])) {
				ok = 0;
				break;
			}
		}
		line = next;
	}

	free(buf);
	return ok && c->line_count > 0;
}

static int gitBlameLoadFileCache(struct gitBlameFileCache *c) {
	if (E.filename == NULL || E.git_repo_root == NULL) {
		return 0;
	}
	char *rel_path = gitRelativePathDup(E.filename);
	if (rel_path == NULL) {
		return 0;
	}
	char cmd[PATH_MAX * 4 + 256];
	size_t pos = 0;
	if (!gitAppendLiteral(cmd, sizeof(cmd), &pos, "git -C ") ||
	    !gitAppendShellQuotedArg(cmd, sizeof(cmd), &pos, E.git_repo_root) ||
	    !gitAppendLiteral(cmd, sizeof(cmd), &pos, " --no-pager blame --incremental -- ") ||
	    !gitAppendShellQuotedArg(cmd, sizeof(cmd), &pos, rel_path) ||
	    !gitAppendLiteral(cmd, sizeof(cmd), &pos, " 2>/dev/null")) {
		free(rel_path);
		return 0;
	}
	free(rel_path);

	g_git_blame_load_count++;
	FILE *fp = popen(cmd, "r");
	if (fp == NULL) {
		return 0;
	}
	size_t buf_len = 0;
	char *buf = gitReadCappedCommandOutput(fp, GIT_BLAME_FILE_MAX_OUTPUT_BYTES, &buf_len);
	int status = pclose(fp);
	if (buf == NULL || status != 0 || buf_len == 0) {
		free(buf);
		return 0;
	}
	int ok = gitBlameParseIncremental(buf, buf_len, c);
	free(buf);
	return ok;
}

/* Ensure the whole-file cache is valid for the active file, loading it once on
 * a key miss. Returns 1 only when line lookups are available (state LOADED). */
static int gitBlameFileCacheEnsureActive(void) {
	struct gitBlameFileCache *c = &g_git_blame_file_cache;
	if (c->state != GIT_BLAME_FILE_EMPTY && gitBlameFileCacheKeyMatchesActive(c)) {
		return c->state == GIT_BLAME_FILE_LOADED;
	}
	gitBlameFileCacheReset(); /* drop stale key + data */
	if (!gitBlameFileCacheStoreKey(c)) {
		gitBlameFileCacheReset();
		return 0;
	}
	if (E.numrows > GIT_BLAME_FILE_MAX_LINES || !gitBlameLoadFileCache(c)) {
		gitBlameFileCacheClearData(c); /* keep the key so we don't retry until it changes */
		c->state = GIT_BLAME_FILE_UNAVAILABLE;
		return 0;
	}
	c->state = GIT_BLAME_FILE_LOADED;
	return 1;
}

enum gitBlameFileLookup {
	GIT_BLAME_FILE_LOOKUP_UNAVAILABLE = 0,
	GIT_BLAME_FILE_LOOKUP_FOUND,
	GIT_BLAME_FILE_LOOKUP_UNBLAMED
};

static enum gitBlameFileLookup gitBlameFillLineFromFileCache(int one_based_line,
                                                             struct editorGitBlameLine *out) {
	if (!gitBlameFileCacheEnsureActive()) {
		return GIT_BLAME_FILE_LOOKUP_UNAVAILABLE;
	}
	struct gitBlameFileCache *c = &g_git_blame_file_cache;
	if (one_based_line < 1 || one_based_line > c->line_count) {
		return GIT_BLAME_FILE_LOOKUP_UNAVAILABLE;
	}
	struct gitBlameLineRef ref = c->lines[one_based_line - 1];
	if (ref.commit_idx < 0 && ref.orig_line > 0) {
		return GIT_BLAME_FILE_LOOKUP_UNBLAMED;
	}
	if (ref.commit_idx < 0 || ref.commit_idx >= c->commit_count || ref.group_idx < 0 ||
	    ref.group_idx >= c->group_count) {
		return GIT_BLAME_FILE_LOOKUP_UNAVAILABLE;
	}
	if (!gitBlameCommitCopy(out, &c->commits[ref.commit_idx])) {
		return GIT_BLAME_FILE_LOOKUP_UNAVAILABLE;
	}
	const char *filename = c->groups[ref.group_idx].filename;
	if (filename != NULL && !gitReplaceString(&out->filename, filename)) {
		gitBlameLineClear(out);
		return GIT_BLAME_FILE_LOOKUP_UNAVAILABLE;
	}
	out->original_line = ref.orig_line;
	out->final_line = one_based_line;
	return GIT_BLAME_FILE_LOOKUP_FOUND;
}

int editorGitBlameTestIncrementalLookup(const char *incremental, int one_based_line,
                                        char *author_out, size_t author_size, char *filename_out,
                                        size_t filename_size, int *unique_commits_out) {
	struct gitBlameFileCache tmp;
	memset(&tmp, 0, sizeof(tmp));
	if (author_out != NULL && author_size > 0) {
		author_out[0] = '\0';
	}
	if (filename_out != NULL && filename_size > 0) {
		filename_out[0] = '\0';
	}
	int parsed = incremental != NULL &&
	             gitBlameParseIncremental(incremental, strlen(incremental), &tmp);
	if (unique_commits_out != NULL) {
		*unique_commits_out = tmp.commit_count;
	}
	int found = 0;
	if (parsed && one_based_line >= 1 && one_based_line <= tmp.line_count) {
		struct gitBlameLineRef ref = tmp.lines[one_based_line - 1];
		if (ref.commit_idx >= 0 && ref.commit_idx < tmp.commit_count) {
			const struct editorGitBlameLine *commit = &tmp.commits[ref.commit_idx];
			if (author_out != NULL && author_size > 0 && commit->author_name != NULL) {
				(void)snprintf(author_out, author_size, "%s", commit->author_name);
			}
			if (filename_out != NULL && filename_size > 0 && ref.group_idx >= 0 &&
			    ref.group_idx < tmp.group_count &&
			    tmp.groups[ref.group_idx].filename != NULL) {
				(void)snprintf(filename_out, filename_size, "%s",
				               tmp.groups[ref.group_idx].filename);
			}
			found = 1;
		}
	}
	gitBlameFileCacheClearData(&tmp);
	return found;
}

const struct editorGitBlameLine *editorGitBlameActiveLine(int one_based_line) {
	if (one_based_line <= 0 || E.tab_kind != EDITOR_TAB_FILE || E.filename == NULL ||
	    E.filename[0] == '\0' || E.dirty != 0 || E.git_repo_root == NULL) {
		return NULL;
	}
	if (gitBlameActiveCacheMatches(one_based_line)) {
		return E.git_blame_line_miss ? NULL : E.git_blame_line;
	}

	editorGitBlameCacheClear(&E.active_buffer);
	if (!gitBlameStoreActiveCacheKey(one_based_line)) {
		return NULL;
	}

	struct editorGitBlameLine *line = editorMalloc(sizeof(*line));
	if (line == NULL) {
		editorGitBlameCacheClear(&E.active_buffer);
		return NULL;
	}
	memset(line, 0, sizeof(*line));
	enum gitBlameFileLookup lookup = gitBlameFillLineFromFileCache(one_based_line, line);
	if (lookup == GIT_BLAME_FILE_LOOKUP_UNBLAMED ||
	    (lookup == GIT_BLAME_FILE_LOOKUP_UNAVAILABLE &&
	     !editorGitLoadBlameLine(E.filename, one_based_line, line))) {
		editorGitBlameLineFree(line);
		free(line);
		E.git_blame_line_miss = 1;
		return NULL;
	}
	E.git_blame_line = line;
	E.git_blame_line_miss = 0;
	return E.git_blame_line;
}

int editorGitBlameActiveInlineLabel(int one_based_line, time_t now, char *buf, size_t buf_size) {
	if (buf == NULL || buf_size == 0) {
		return 0;
	}
	const struct editorGitBlameLine *line = editorGitBlameActiveLine(one_based_line);
	if (line == NULL) {
		return 0;
	}
	char relative[64];
	if (!editorGitFormatRelativeTime(line->author_time, now, relative, sizeof(relative))) {
		return 0;
	}
	const char *author = line->author_name != NULL && line->author_name[0] != '\0'
	                             ? line->author_name
	                             : "Unknown";
	int n = snprintf(buf, buf_size, "  %s %s", author, relative);
	return n > 0 && (size_t)n < buf_size;
}

long editorGitBlameTestLoadCount(void) {
	return g_git_blame_load_count;
}

static int gitShaIsAllZero(const char *sha) {
	if (sha == NULL || *sha == '\0') {
		return 0;
	}
	for (const char *p = sha; *p != '\0'; p++) {
		if (*p != '0') {
			return 0;
		}
	}
	return 1;
}

static char *gitDupLimited(const char *data, size_t len) {
	if (len > GIT_BLAME_MAX_FIELD_BYTES) {
		len = GIT_BLAME_MAX_FIELD_BYTES;
	}
	char *dup = editorMalloc(len + 1);
	if (dup == NULL) {
		return NULL;
	}
	memcpy(dup, data, len);
	dup[len] = '\0';
	return dup;
}

static char *gitDupStringLimited(const char *s) {
	if (s == NULL) {
		return NULL;
	}
	return gitDupLimited(s, strlen(s));
}

static int gitReplaceString(char **slot, const char *value) {
	char *dup = gitDupStringLimited(value);
	if (dup == NULL) {
		return 0;
	}
	free(*slot);
	*slot = dup;
	return 1;
}

static int gitParseIntField(const char *value, int *out) {
	if (value == NULL || out == NULL) {
		return 0;
	}
	errno = 0;
	char *end = NULL;
	long parsed = strtol(value, &end, 10);
	if (errno != 0 || end == value || parsed < 0 || parsed > INT_MAX) {
		return 0;
	}
	*out = (int)parsed;
	return 1;
}

static int gitParseTimeField(const char *value, time_t *out) {
	if (value == NULL || out == NULL) {
		return 0;
	}
	errno = 0;
	char *end = NULL;
	long long parsed = strtoll(value, &end, 10);
	if (errno != 0 || end == value || parsed < 0) {
		return 0;
	}
	*out = (time_t)parsed;
	return 1;
}

static int gitBlameParseHeader(char *line, struct editorGitBlameLine *out) {
	char *save = NULL;
	char *sha = strtok_r(line, " ", &save);
	char *original = strtok_r(NULL, " ", &save);
	char *final = strtok_r(NULL, " ", &save);
	if (sha == NULL || original == NULL || final == NULL) {
		return 0;
	}
	if (sha[0] == '^') {
		sha++;
	}
	if (gitShaIsAllZero(sha)) {
		return 0;
	}
	if (!gitReplaceString(&out->commit_sha, sha)) {
		return 0;
	}
	size_t sha_len = strlen(sha);
	size_t short_len =
	        sha_len < GIT_BLAME_SHORT_SHA_BYTES ? sha_len : GIT_BLAME_SHORT_SHA_BYTES;
	char *short_sha = gitDupLimited(sha, short_len);
	if (short_sha == NULL) {
		return 0;
	}
	free(out->short_sha);
	out->short_sha = short_sha;
	(void)gitParseIntField(original, &out->original_line);
	(void)gitParseIntField(final, &out->final_line);
	return 1;
}

static char *gitBlameNormalizeEmail(const char *value) {
	if (value == NULL) {
		return NULL;
	}
	size_t len = strlen(value);
	if (len >= 2 && value[0] == '<' && value[len - 1] == '>') {
		return gitDupLimited(value + 1, len - 2);
	}
	return gitDupStringLimited(value);
}

static int gitBlameSetEmail(char **slot, const char *value) {
	char *dup = gitBlameNormalizeEmail(value);
	if (dup == NULL) {
		return 0;
	}
	free(*slot);
	*slot = dup;
	return 1;
}

static int gitBlameParsePrevious(const char *value, struct editorGitBlameLine *out) {
	if (value == NULL) {
		return 1;
	}
	const char *space = strchr(value, ' ');
	if (space == NULL || space[1] == '\0') {
		return 1;
	}
	return gitReplaceString(&out->original_path, space + 1);
}

static int gitBlameParseField(char *line, struct editorGitBlameLine *out) {
	if (strncmp(line, "author ", 7) == 0) {
		return gitReplaceString(&out->author_name, line + 7);
	}
	if (strncmp(line, "author-mail ", 12) == 0) {
		return gitBlameSetEmail(&out->author_email, line + 12);
	}
	if (strncmp(line, "author-time ", 12) == 0) {
		(void)gitParseTimeField(line + 12, &out->author_time);
		return 1;
	}
	if (strncmp(line, "committer ", 10) == 0) {
		return gitReplaceString(&out->committer_name, line + 10);
	}
	if (strncmp(line, "committer-time ", 15) == 0) {
		(void)gitParseTimeField(line + 15, &out->committer_time);
		return 1;
	}
	if (strncmp(line, "summary ", 8) == 0) {
		return gitReplaceString(&out->summary, line + 8);
	}
	if (strncmp(line, "filename ", 9) == 0) {
		return gitReplaceString(&out->filename, line + 9);
	}
	if (strncmp(line, "previous ", 9) == 0) {
		return gitBlameParsePrevious(line + 9, out);
	}
	return 1;
}

int editorGitParseBlamePorcelain(const char *data, size_t len, struct editorGitBlameLine *out) {
	if (out == NULL) {
		return 0;
	}
	gitBlameLineClear(out);
	if (data == NULL || len == 0 || len > GIT_BLAME_MAX_OUTPUT_BYTES) {
		return 0;
	}

	char *buf = editorMalloc(len + 1);
	if (buf == NULL) {
		return 0;
	}
	memcpy(buf, data, len);
	buf[len] = '\0';

	int ok = 0;
	int saw_header = 0;
	char *line = buf;
	while (line != NULL) {
		char *next = strchr(line, '\n');
		if (next != NULL) {
			*next++ = '\0';
		}
		size_t line_len = strlen(line);
		if (line_len > 0 && line[line_len - 1] == '\r') {
			line[line_len - 1] = '\0';
		}
		if (!saw_header) {
			if (!gitBlameParseHeader(line, out)) {
				goto cleanup;
			}
			saw_header = 1;
		} else if (!gitBlameParseField(line, out)) {
			goto cleanup;
		}
		line = next;
	}

	ok = saw_header && out->commit_sha != NULL;

cleanup:
	free(buf);
	if (!ok) {
		gitBlameLineClear(out);
	}
	return ok;
}

static char *gitRelativePathDup(const char *abs_path) {
	if (E.git_repo_root == NULL || abs_path == NULL) {
		return NULL;
	}
	size_t root_len = strlen(E.git_repo_root);
	if (strncmp(abs_path, E.git_repo_root, root_len) != 0) {
		return NULL;
	}
	const char *rel = abs_path + root_len;
	if (*rel == '/') {
		rel++;
	} else if (*rel != '\0') {
		return NULL;
	}
	if (*rel == '\0') {
		return NULL;
	}
	return gitDupStringLimited(rel);
}

static char *gitReadCappedCommandOutput(FILE *fp, size_t max_len, size_t *len_out) {
	if (len_out != NULL) {
		*len_out = 0;
	}
	char *buf = editorMalloc(max_len + 1);
	if (buf == NULL) {
		return NULL;
	}
	size_t len = 0;
	while (len < max_len) {
		size_t n = fread(buf + len, 1, max_len - len, fp);
		if (n == 0) {
			break;
		}
		len += n;
	}
	if (!feof(fp)) {
		free(buf);
		return NULL;
	}
	buf[len] = '\0';
	if (len_out != NULL) {
		*len_out = len;
	}
	return buf;
}

int editorGitLoadBlameLine(const char *abs_path, int one_based_line,
                           struct editorGitBlameLine *out) {
	if (out != NULL) {
		gitBlameLineClear(out);
	}
	if (out == NULL || abs_path == NULL || one_based_line <= 0 || E.git_repo_root == NULL) {
		return 0;
	}

	char *rel_path = gitRelativePathDup(abs_path);
	if (rel_path == NULL) {
		return 0;
	}

	char line_arg[64];
	int line_len =
	        snprintf(line_arg, sizeof(line_arg), "%d,%d", one_based_line, one_based_line);
	if (line_len <= 0 || line_len >= (int)sizeof(line_arg)) {
		free(rel_path);
		return 0;
	}

	char cmd[PATH_MAX * 4 + 256];
	size_t pos = 0;
	if (!gitAppendLiteral(cmd, sizeof(cmd), &pos, "git -C ") ||
	    !gitAppendShellQuotedArg(cmd, sizeof(cmd), &pos, E.git_repo_root) ||
	    !gitAppendLiteral(cmd, sizeof(cmd), &pos, " --no-pager blame --line-porcelain -L ") ||
	    !gitAppendShellQuotedArg(cmd, sizeof(cmd), &pos, line_arg) ||
	    !gitAppendLiteral(cmd, sizeof(cmd), &pos, " -- ") ||
	    !gitAppendShellQuotedArg(cmd, sizeof(cmd), &pos, rel_path) ||
	    !gitAppendLiteral(cmd, sizeof(cmd), &pos, " 2>/dev/null")) {
		free(rel_path);
		return 0;
	}
	free(rel_path);

	g_git_blame_load_count++;
	FILE *fp = popen(cmd, "r");
	if (fp == NULL) {
		return 0;
	}
	size_t buf_len = 0;
	char *buf = gitReadCappedCommandOutput(fp, GIT_BLAME_MAX_OUTPUT_BYTES, &buf_len);
	int status = pclose(fp);
	if (buf == NULL || status == -1 || buf_len == 0) {
		free(buf);
		return 0;
	}

	int ok = editorGitParseBlamePorcelain(buf, buf_len, out);
	free(buf);
	return ok;
}

static int gitFormatRelativeUnit(long long value, const char *singular, const char *plural,
                                 char *buf, size_t buf_size) {
	const char *unit = value == 1 ? singular : plural;
	int n = snprintf(buf, buf_size, "%lld %s ago", value, unit);
	return n > 0 && (size_t)n < buf_size;
}

int editorGitFormatRelativeTime(time_t then, time_t now, char *buf, size_t buf_size) {
	if (buf == NULL || buf_size == 0) {
		return 0;
	}
	long long diff = 0;
	if (now > then) {
		diff = (long long)(now - then);
	}
	if (diff < 60) {
		int n = snprintf(buf, buf_size, "just now");
		return n > 0 && (size_t)n < buf_size;
	}
	if (diff < 3600) {
		return gitFormatRelativeUnit(diff / 60, "minute", "minutes", buf, buf_size);
	}
	if (diff < 86400) {
		return gitFormatRelativeUnit(diff / 3600, "hour", "hours", buf, buf_size);
	}
	if (diff < 172800) {
		int n = snprintf(buf, buf_size, "yesterday");
		return n > 0 && (size_t)n < buf_size;
	}
	if (diff < 604800) {
		return gitFormatRelativeUnit(diff / 86400, "day", "days", buf, buf_size);
	}
	if (diff < 2592000) {
		return gitFormatRelativeUnit(diff / 604800, "week", "weeks", buf, buf_size);
	}
	if (diff < 31536000) {
		return gitFormatRelativeUnit(diff / 2592000, "month", "months", buf, buf_size);
	}
	return gitFormatRelativeUnit(diff / 31536000, "year", "years", buf, buf_size);
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
			if (gitStatusSeverity(s) > gitStatusSeverity(worst)) {
				worst = s;
			}
			if (worst == EDITOR_GIT_STATUS_CONFLICT) {
				break;
			}
		}
	}
	return worst;
}
