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

/* How a line of the active file differs from its HEAD version, as shown in the
 * editor gutter. Removals have no line of their own, so they are flagged on the
 * line that closes the gap (or on the last line when the tail was cut). */
enum editorGitGutterMark {
	EDITOR_GIT_GUTTER_NONE = 0,
	EDITOR_GIT_GUTTER_ADDED,
	EDITOR_GIT_GUTTER_MODIFIED,
	EDITOR_GIT_GUTTER_DELETED_ABOVE,
	EDITOR_GIT_GUTTER_DELETED_BELOW
};

int editorGitInit(void);
void editorGitRefresh(void);
void editorGitFree(void);
const char *editorGitBranch(void);
enum editorGitStatus editorGitFileStatus(const char *abs_path);
enum editorGitStatus editorGitDirStatus(const char *abs_path);
enum editorGitStatus editorGitStatusFromChar(char c);
void editorGitBlameLineFree(struct editorGitBlameLine *line);
void editorGitBlameCacheClear(struct editorBuffer *buffer);
void editorGitBlameCacheClearAll(void);
int editorGitParseBlamePorcelain(const char *data, size_t len, struct editorGitBlameLine *out);
int editorGitLoadBlameLine(const char *abs_path, int one_based_line,
                           struct editorGitBlameLine *out);
const struct editorGitBlameLine *editorGitBlameActiveLine(int one_based_line);
int editorGitBlameActiveInlineLabel(int one_based_line, time_t now, char *buf, size_t buf_size);
int editorGitFormatRelativeTime(time_t then, time_t now, char *buf, size_t buf_size);
enum editorGitGutterMark editorGitGutterMarkForRow(int row_idx);
int editorGitBuildRepoCommand(char *cmd, size_t cmd_size, const char *args_literal);
int editorGitBuildRepoCommandArgs(char *cmd, size_t cmd_size, char *const args[]);

#endif
