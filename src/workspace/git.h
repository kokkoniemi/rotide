#ifndef ROTIDE_WORKSPACE_GIT_H
#define ROTIDE_WORKSPACE_GIT_H

#include "rotide.h"

struct editorGitBlameLine {
	char *commit_sha;
	char *short_sha;
	char *author_name;
	char *author_email;
	time_t author_time;
	char *committer_name;
	time_t committer_time;
	char *summary;
	char *filename;
	char *original_path;
	int original_line;
	int final_line;
};

int editorGitInit(void);
void editorGitRefresh(void);
void editorGitFree(void);
const char *editorGitBranch(void);
enum editorGitStatus editorGitFileStatus(const char *abs_path);
enum editorGitStatus editorGitDirStatus(const char *abs_path);
char *editorGitGenerateDiff(const char *rel_path, char index_status, char worktree_status,
                            size_t *len_out);
void editorGitBlameLineFree(struct editorGitBlameLine *line);
void editorGitBlameCacheClear(struct editorBuffer *buffer);
void editorGitBlameCacheClearAll(void);
int editorGitParseBlamePorcelain(const char *data, size_t len, struct editorGitBlameLine *out);
int editorGitLoadBlameLine(const char *abs_path, int one_based_line,
                           struct editorGitBlameLine *out);
const struct editorGitBlameLine *editorGitBlameActiveLine(int one_based_line);
int editorGitBlameActiveInlineLabel(int one_based_line, time_t now, char *buf, size_t buf_size);
int editorGitFormatRelativeTime(time_t then, time_t now, char *buf, size_t buf_size);
int editorGitBuildRepoCommand(char *cmd, size_t cmd_size, const char *args_literal);

#endif
