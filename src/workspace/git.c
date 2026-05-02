#include "workspace/git.h"

#include "rotide.h"
#include "support/alloc.h"
#include "support/file_io.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void editorGitFreeEntries(void) {
	for (int i = 0; i < E.git_entry_count; i++) {
		free(E.git_entries[i].rel_path);
	}
	free(E.git_entries);
	E.git_entries = NULL;
	E.git_entry_count = 0;
	E.git_entry_capacity = 0;
}

static int editorGitEntryCompare(const void *a, const void *b) {
	const struct editorGitEntry *ea = (const struct editorGitEntry *)a;
	const struct editorGitEntry *eb = (const struct editorGitEntry *)b;
	return strcmp(ea->rel_path, eb->rel_path);
}

static enum editorGitStatus editorGitStatusFromXY(char x, char y) {
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

static int editorGitAddEntry(const char *rel_path, enum editorGitStatus status) {
	if (E.git_entry_count >= E.git_entry_capacity) {
		int new_cap = E.git_entry_capacity == 0 ? 16 : E.git_entry_capacity * 2;
		struct editorGitEntry *new_entries = editorRealloc(E.git_entries,
				(size_t)new_cap * sizeof(struct editorGitEntry));
		if (new_entries == NULL) {
			return 0;
		}
		E.git_entries = new_entries;
		E.git_entry_capacity = new_cap;
	}
	char *path_dup = strdup(rel_path);
	if (path_dup == NULL) {
		return 0;
	}
	E.git_entries[E.git_entry_count].rel_path = path_dup;
	E.git_entries[E.git_entry_count].status = status;
	E.git_entry_count++;
	return 1;
}

int editorGitInit(void) {
	char *cwd = editorPathGetCwd();
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

	char head_path[PATH_MAX];
	int n = snprintf(head_path, sizeof(head_path), "%s/.git/HEAD", repo_root);
	if (n > 0 && n < (int)sizeof(head_path)) {
		FILE *f = fopen(head_path, "r");
		if (f != NULL) {
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
			fclose(f);
		}
	}

	editorGitRefresh();
	return 1;
}

void editorGitRefresh(void) {
	if (E.git_repo_root == NULL) {
		return;
	}

	editorGitFreeEntries();

	// Single-quote-wrap the repo root so paths with spaces are handled safely.
	// Any single quotes in the path itself are escaped as '\''.
	char cmd[PATH_MAX * 2 + 64];
	size_t pos = 0;
	const char *prefix = "git -C '";
	size_t prefix_len = strlen(prefix);
	if (prefix_len >= sizeof(cmd)) {
		return;
	}
	memcpy(cmd, prefix, prefix_len);
	pos = prefix_len;
	for (const char *p = E.git_repo_root; *p != '\0'; p++) {
		if (*p == '\'') {
			if (pos + 4 >= sizeof(cmd)) {
				return;
			}
			cmd[pos++] = '\'';
			cmd[pos++] = '\\';
			cmd[pos++] = '\'';
			cmd[pos++] = '\'';
		} else {
			if (pos + 1 >= sizeof(cmd)) {
				return;
			}
			cmd[pos++] = *p;
		}
	}
	const char *suffix = "' status --porcelain=v1 -z --untracked-files=normal 2>/dev/null";
	size_t suffix_len = strlen(suffix);
	if (pos + suffix_len + 1 >= sizeof(cmd)) {
		return;
	}
	memcpy(cmd + pos, suffix, suffix_len + 1);

	FILE *fp = popen(cmd, "r");
	if (fp == NULL) {
		return;
	}

	size_t buf_cap = 4096;
	size_t buf_len = 0;
	char *buf = editorMalloc(buf_cap);
	if (buf == NULL) {
		pclose(fp);
		return;
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
	pclose(fp);

	// Parse: each record is "XY <path>\0", renames add an extra "<orig>\0"
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

		enum editorGitStatus status = editorGitStatusFromXY(x, y);
		if (status != EDITOR_GIT_STATUS_CLEAN) {
			(void)editorGitAddEntry(rel_path, status);
		}
	}

	free(buf);

	if (E.git_entry_count > 1) {
		qsort(E.git_entries, (size_t)E.git_entry_count,
				sizeof(struct editorGitEntry), editorGitEntryCompare);
	}
}

void editorGitFree(void) {
	free(E.git_repo_root);
	E.git_repo_root = NULL;
	free(E.git_branch);
	E.git_branch = NULL;
	editorGitFreeEntries();
}

const char *editorGitBranch(void) {
	return E.git_branch;
}

enum editorGitStatus editorGitFileStatus(const char *abs_path) {
	if (E.git_repo_root == NULL || E.git_entries == NULL ||
			E.git_entry_count == 0 || abs_path == NULL) {
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

	int lo = 0, hi = E.git_entry_count - 1;
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

enum editorGitStatus editorGitDirStatus(const char *abs_path) {
	if (E.git_repo_root == NULL || E.git_entries == NULL ||
			E.git_entry_count == 0 || abs_path == NULL) {
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
		int matches = (rel_len == 0) ||
				(strncmp(entry_path, rel, rel_len) == 0 && entry_path[rel_len] == '/');
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
