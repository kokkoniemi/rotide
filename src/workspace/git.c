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

	editorGitBlameCacheClearAll();
	gitRefreshBranch();
	gitRefreshHead();
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
	editorGitBlameCacheClearAll();
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
	if (!editorGitLoadBlameLine(E.filename, one_based_line, line)) {
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
