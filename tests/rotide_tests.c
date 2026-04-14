#include "rotide.h"

#include "config/editor_config.h"
#include "config/keymap.h"
#include "config/lsp_config.h"
#include "config/theme_config.h"
#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "editing/history.h"
#include "editing/selection.h"
#include "editor_test_api.h"
#include "input/dispatch.h"
#include "language/lsp.h"
#include "language/syntax.h"
#include "render/screen.h"
#include "support/file_io.h"
#include "support/terminal.h"
#include "text/document.h"
#include "text/row.h"
#include "text/utf8.h"
#include "workspace/drawer.h"
#include "workspace/recovery.h"
#include "workspace/task.h"
#include "workspace/tabs.h"
#include "alloc_test_hooks.h"
#include "save_syscalls_test_hooks.h"
#include "test_helpers.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

struct editorConfig E;

static int count_tmp_save_artifacts(const char *target_path) {
	const char *base = strrchr(target_path, '/');
	if (base != NULL) {
		base++;
	} else {
		base = target_path;
	}

	static const char suffix[] = ".rotide-tmp-";
	size_t base_len = strlen(base);
	size_t prefix_len = base_len + sizeof(suffix) - 1;
	char *prefix = malloc(prefix_len + 1);
	if (prefix == NULL) {
		return -1;
	}

	memcpy(prefix, base, base_len);
	memcpy(prefix + base_len, suffix, sizeof(suffix));

	DIR *dir = opendir("/tmp");
	if (dir == NULL) {
		free(prefix);
		return -1;
	}

	int count = 0;
	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		if (strncmp(entry->d_name, prefix, prefix_len) == 0) {
			count++;
		}
	}

	closedir(dir);
	free(prefix);
	return count;
}

static int remove_tmp_save_artifacts(const char *target_path) {
	const char *base = strrchr(target_path, '/');
	if (base != NULL) {
		base++;
	} else {
		base = target_path;
	}

	static const char suffix[] = ".rotide-tmp-";
	size_t base_len = strlen(base);
	size_t prefix_len = base_len + sizeof(suffix) - 1;
	char *prefix = malloc(prefix_len + 1);
	if (prefix == NULL) {
		return -1;
	}

	memcpy(prefix, base, base_len);
	memcpy(prefix + base_len, suffix, sizeof(suffix));

	DIR *dir = opendir("/tmp");
	if (dir == NULL) {
		free(prefix);
		return -1;
	}

	int failures = 0;
	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		if (strncmp(entry->d_name, prefix, prefix_len) != 0) {
			continue;
		}

		size_t path_len = sizeof("/tmp/") - 1 + strlen(entry->d_name) + 1;
		char *full_path = malloc(path_len);
		if (full_path == NULL) {
			failures++;
			continue;
		}

		snprintf(full_path, path_len, "/tmp/%s", entry->d_name);
		if (unlink(full_path) == -1) {
			failures++;
		}
		free(full_path);
	}

	closedir(dir);
	free(prefix);
	return failures == 0 ? 0 : -1;
}

static int write_fixture_to_temp_path(char path_buf[], int suffix_len,
		const char *fixture_relative_path);

static int write_temp_file_with_suffix(char path_buf[], size_t path_buf_size, const char *prefix,
		const char *suffix, const char *content) {
	if (path_buf == NULL || prefix == NULL || suffix == NULL || content == NULL) {
		return 0;
	}
	int min_size = snprintf(NULL, 0, "/tmp/%sXXXXXX%s", prefix, suffix) + 1;
	if (min_size <= 0 || (size_t)min_size > path_buf_size) {
		return 0;
	}
	int written = snprintf(path_buf, path_buf_size, "/tmp/%sXXXXXX%s", prefix, suffix);
	if (written <= 0 || (size_t)written >= path_buf_size) {
		return 0;
	}

	int fd = mkstemps(path_buf, (int)strlen(suffix));
	if (fd == -1) {
		return 0;
	}

	size_t len = strlen(content);
	int ok = write_all(fd, content, len) == 0 && close(fd) == 0;
	if (!ok) {
		(void)close(fd);
		(void)unlink(path_buf);
	}
	return ok;
}

static int copy_fixture_to_temp_file_with_suffix(char path_buf[], size_t path_buf_size,
		const char *prefix, const char *suffix, const char *fixture_relative_path) {
	if (path_buf == NULL || prefix == NULL || suffix == NULL || fixture_relative_path == NULL) {
		return 0;
	}
	int min_size = snprintf(NULL, 0, "/tmp/%sXXXXXX%s", prefix, suffix) + 1;
	if (min_size <= 0 || (size_t)min_size > path_buf_size) {
		return 0;
	}
	int written = snprintf(path_buf, path_buf_size, "/tmp/%sXXXXXX%s", prefix, suffix);
	if (written <= 0 || (size_t)written >= path_buf_size) {
		return 0;
	}
	return write_fixture_to_temp_path(path_buf, (int)strlen(suffix), fixture_relative_path);
}

static int copy_fixture_to_path(const char *dest_path, const char *fixture_relative_path) {
	if (dest_path == NULL || fixture_relative_path == NULL) {
		return 0;
	}
	return copyTestFixtureToPath(fixture_relative_path, dest_path);
}

static int write_temp_go_file(char path_buf[], size_t path_buf_size, const char *content) {
	return write_temp_file_with_suffix(path_buf, path_buf_size, "rotide-test-go-lsp-", ".go",
			content);
}

static int write_temp_c_file(char path_buf[], size_t path_buf_size, const char *content) {
	return write_temp_file_with_suffix(path_buf, path_buf_size, "rotide-test-c-lsp-", ".c",
			content);
}

static int write_temp_html_file(char path_buf[], size_t path_buf_size, const char *content) {
	return write_temp_file_with_suffix(path_buf, path_buf_size, "rotide-test-html-lsp-", ".html",
			content);
}

static int write_temp_text_file(char path_buf[], size_t path_buf_size, const char *content) {
	return write_temp_file_with_suffix(path_buf, path_buf_size, "rotide-test-lsp-", ".txt",
			content);
}

static char *dup_active_source_text(size_t *len_out) {
	struct editorTextSource source = {0};
	if (!editorBuildActiveTextSource(&source)) {
		if (len_out != NULL) {
			*len_out = 0;
		}
		return NULL;
	}
	return editorTextSourceDupRange(&source, 0, source.length, len_out);
}

static int assert_active_source_matches_rows(void) {
	size_t source_len = 0;
	size_t rows_len = 0;
	char *source_text = dup_active_source_text(&source_len);
	char *rows_text = editorRowsToStr(&rows_len);
	ASSERT_TRUE(source_text != NULL || source_len == 0);
	ASSERT_TRUE(rows_text != NULL || rows_len == 0);
	ASSERT_EQ_INT((int)rows_len, (int)source_len);
	if (rows_len > 0) {
		ASSERT_MEM_EQ(rows_text, source_text, rows_len);
	}
	free(source_text);
	free(rows_text);
	return 0;
}

static int set_active_search_match(int row, int col, int len) {
	size_t offset = 0;
	if (!editorBufferPosToOffset(row, col, &offset)) {
		return 0;
	}
	E.search_match_offset = offset;
	E.search_match_len = len;
	return 1;
}

static int assert_active_search_match(int row, int col, int len) {
	ASSERT_EQ_INT(len, E.search_match_len);
	if (len <= 0) {
		return 0;
	}

	int match_row = -1;
	int match_col = -1;
	ASSERT_TRUE(editorBufferOffsetToPos(E.search_match_offset, &match_row, &match_col));
	ASSERT_EQ_INT(row, match_row);
	ASSERT_EQ_INT(col, match_col);
	return 0;
}

static int set_selection_anchor(int row, int col) {
	return editorBufferPosToOffset(row, col, &E.selection_anchor_offset);
}

static int assert_selection_anchor(int row, int col) {
	int anchor_row = -1;
	int anchor_col = -1;
	ASSERT_TRUE(editorBufferOffsetToPos(E.selection_anchor_offset, &anchor_row, &anchor_col));
	ASSERT_EQ_INT(row, anchor_row);
	ASSERT_EQ_INT(col, anchor_col);
	return 0;
}

static int write_fixture_to_temp_path(char path_buf[], int suffix_len,
		const char *fixture_relative_path) {
	int fd = suffix_len > 0 ? mkstemps(path_buf, suffix_len) : mkstemp(path_buf);
	if (fd == -1) {
		return 0;
	}
	if (close(fd) != 0) {
		(void)unlink(path_buf);
		return 0;
	}
	if (!copyTestFixtureToPath(fixture_relative_path, path_buf)) {
		(void)unlink(path_buf);
		return 0;
	}
	return 1;
}

static int editor_process_keypress_with_input_silent(const char *input, size_t len) {
	int saved_stdout;
	if (redirect_stdout_to_devnull(&saved_stdout) == -1) {
		return -1;
	}

	int ret = editor_process_keypress_with_input(input, len);
	if (restore_stdout(saved_stdout) == -1) {
		return -1;
	}

	return ret;
}

static int wait_for_task_completion_with_timeout(int timeout_ms) {
	const int poll_interval_us = 10000;
	int waited_us = 0;

	while (waited_us <= timeout_ms * 1000) {
		(void)editorTaskPoll();
		if (!editorTaskIsRunning()) {
			return 1;
		}
		if (usleep((useconds_t)poll_interval_us) == -1 && errno != EINTR) {
			return 0;
		}
		waited_us += poll_interval_us;
	}

	return 0;
}

static int editor_process_single_key(int key) {
	char c = (char)key;
	return editor_process_keypress_with_input(&c, 1);
}

static int format_sgr_mouse_event(char *buf, size_t bufsz, int cb, int x, int y, char suffix) {
	int written = snprintf(buf, bufsz, "\x1b[<%d;%d;%d%c", cb, x, y, suffix);
	return written > 0 && (size_t)written < bufsz;
}

static int wait_for_child_exit_with_timeout(pid_t pid, int timeout_ms, int *status_out) {
	const int poll_interval_us = 10000;
	int waited_us = 0;

	while (waited_us <= timeout_ms * 1000) {
		pid_t waited = waitpid(pid, status_out, WNOHANG);
		if (waited == pid) {
			return 0;
		}
		if (waited == -1) {
			return -1;
		}

		if (usleep((useconds_t)poll_interval_us) == -1 && errno != EINTR) {
			return -1;
		}
		waited_us += poll_interval_us;
	}

	(void)kill(pid, SIGKILL);
	(void)waitpid(pid, status_out, 0);
	errno = ETIMEDOUT;
	return -1;
}

struct envVarBackup {
	const char *name;
	char *value;
	int was_set;
};

static int backup_env_var(struct envVarBackup *backup, const char *name) {
	backup->name = name;
	backup->value = NULL;
	backup->was_set = 0;

	const char *current = getenv(name);
	if (current == NULL) {
		return 1;
	}

	backup->value = strdup(current);
	if (backup->value == NULL) {
		return 0;
	}
	backup->was_set = 1;
	return 1;
}

static int restore_env_var(struct envVarBackup *backup) {
	int ok = 0;
	if (backup->was_set) {
		ok = setenv(backup->name, backup->value, 1) == 0;
	} else {
		ok = unsetenv(backup->name) == 0;
	}
	free(backup->value);
	backup->value = NULL;
	backup->was_set = 0;
	return ok;
}

static int write_text_file(const char *path, const char *text) {
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd == -1) {
		return 0;
	}

	size_t len = strlen(text);
	int write_ok = write_all(fd, text, len) == 0;
	int close_ok = close(fd) == 0;
	return write_ok && close_ok;
}

static char *build_repeated_text(const char *line, int repeats, size_t *len_out) {
	if (line == NULL || repeats < 0 || len_out == NULL) {
		return NULL;
	}

	size_t line_len = strlen(line);
	if (line_len > 0 && (size_t)repeats > SIZE_MAX / line_len) {
		return NULL;
	}
	size_t total_len = line_len * (size_t)repeats;
	char *text = malloc(total_len + 1);
	if (text == NULL) {
		return NULL;
	}

	char *out = text;
	for (int i = 0; i < repeats; i++) {
		memcpy(out, line, line_len);
		out += line_len;
	}
	text[total_len] = '\0';
	*len_out = total_len;
	return text;
}

static int path_join(char *buf, size_t bufsz, const char *dir, const char *name) {
	int written = snprintf(buf, bufsz, "%s/%s", dir, name);
	return written >= 0 && (size_t)written < bufsz;
}

static int make_dir(const char *path) {
	return mkdir(path, 0700) == 0 || errno == EEXIST;
}

struct recoveryTestEnv {
	struct envVarBackup home_backup;
	char *original_cwd;
	char root_dir[512];
	char home_dir[512];
	char project_dir[512];
};

static void remove_files_in_dir(const char *dir_path) {
	DIR *dir = opendir(dir_path);
	if (dir == NULL) {
		return;
	}

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		char full_path[1024];
		if (!path_join(full_path, sizeof(full_path), dir_path, entry->d_name)) {
			continue;
		}
		(void)unlink(full_path);
	}

	closedir(dir);
}

static void cleanup_recovery_test_env(struct recoveryTestEnv *env) {
	editorRecoveryShutdown();

	if (env->original_cwd != NULL) {
		if (chdir(env->original_cwd) != 0) {
			/* Best-effort cleanup in tests; continue tearing down temp paths. */
		}
	}
	if (env->home_backup.name != NULL) {
		(void)restore_env_var(&env->home_backup);
	}

	char dot_rotide_dir[512];
	char recovery_dir[512];
	if (env->home_dir[0] != '\0' &&
			path_join(dot_rotide_dir, sizeof(dot_rotide_dir), env->home_dir, ".rotide") &&
			path_join(recovery_dir, sizeof(recovery_dir), dot_rotide_dir, "recovery")) {
		remove_files_in_dir(recovery_dir);
		(void)rmdir(recovery_dir);
		remove_files_in_dir(dot_rotide_dir);
		(void)rmdir(dot_rotide_dir);
	}

	if (env->home_dir[0] != '\0') {
		remove_files_in_dir(env->home_dir);
		(void)rmdir(env->home_dir);
	}
	if (env->project_dir[0] != '\0') {
		remove_files_in_dir(env->project_dir);
		(void)rmdir(env->project_dir);
	}
	if (env->root_dir[0] != '\0') {
		(void)rmdir(env->root_dir);
	}

	free(env->original_cwd);
	env->original_cwd = NULL;
	env->root_dir[0] = '\0';
	env->home_dir[0] = '\0';
	env->project_dir[0] = '\0';
	env->home_backup.name = NULL;
}

static int setup_recovery_test_env(struct recoveryTestEnv *env) {
	memset(env, 0, sizeof(*env));
	if (!backup_env_var(&env->home_backup, "HOME")) {
		return 0;
	}

	env->original_cwd = getcwd(NULL, 0);
	if (env->original_cwd == NULL) {
		cleanup_recovery_test_env(env);
		return 0;
	}

	char root_template[] = "/tmp/rotide-test-recovery-XXXXXX";
	char *root_path = mkdtemp(root_template);
	if (root_path == NULL) {
		cleanup_recovery_test_env(env);
		return 0;
	}
	if (snprintf(env->root_dir, sizeof(env->root_dir), "%s", root_path) >=
			(int)sizeof(env->root_dir)) {
		cleanup_recovery_test_env(env);
		return 0;
	}

	if (!path_join(env->home_dir, sizeof(env->home_dir), env->root_dir, "home") ||
			!path_join(env->project_dir, sizeof(env->project_dir), env->root_dir, "project")) {
		cleanup_recovery_test_env(env);
		return 0;
	}
	if (mkdir(env->home_dir, 0700) == -1 || mkdir(env->project_dir, 0700) == -1) {
		cleanup_recovery_test_env(env);
		return 0;
	}
	if (setenv("HOME", env->home_dir, 1) != 0) {
		cleanup_recovery_test_env(env);
		return 0;
	}
	if (chdir(env->project_dir) != 0) {
		cleanup_recovery_test_env(env);
		return 0;
	}
	if (!editorRecoveryInitForCurrentDir()) {
		cleanup_recovery_test_env(env);
		return 0;
	}
	return 1;
}

static int test_utf8_decode_valid_sequences(void) {
	unsigned int cp = 0;
	const char e_acute[] = "\xC3\xA9";
	const char euro[] = "\xE2\x82\xAC";
	const char grin[] = "\xF0\x9F\x98\x80";

	ASSERT_EQ_INT(1, editorUtf8DecodeCodepoint("A", 1, &cp));
	ASSERT_EQ_INT('A', cp);

	ASSERT_EQ_INT(2, editorUtf8DecodeCodepoint(e_acute, (int)sizeof(e_acute) - 1, &cp));
	ASSERT_EQ_INT(0xE9, cp);

	ASSERT_EQ_INT(3, editorUtf8DecodeCodepoint(euro, (int)sizeof(euro) - 1, &cp));
	ASSERT_EQ_INT(0x20AC, cp);

	ASSERT_EQ_INT(4, editorUtf8DecodeCodepoint(grin, (int)sizeof(grin) - 1, &cp));
	ASSERT_EQ_INT(0x1F600, cp);

	return 0;
}

static int test_utf8_decode_invalid_sequences(void) {
	unsigned int cp = 0;
	const char invalid_lead[] = "\xFF";
	const char truncated[] = "\xC3";
	const char bad_continuation[] = "\xE2\x28\xA1";
	const char overlong[] = "\xC0\xAF";

	ASSERT_EQ_INT(1, editorUtf8DecodeCodepoint(invalid_lead, (int)sizeof(invalid_lead) - 1,
				&cp));
	ASSERT_EQ_INT(0xFF, cp);

	ASSERT_EQ_INT(1, editorUtf8DecodeCodepoint(truncated, (int)sizeof(truncated) - 1, &cp));
	ASSERT_EQ_INT(0xC3, cp);

	ASSERT_EQ_INT(1, editorUtf8DecodeCodepoint(bad_continuation,
				(int)sizeof(bad_continuation) - 1, &cp));
	ASSERT_EQ_INT(0xE2, cp);

	ASSERT_EQ_INT(1, editorUtf8DecodeCodepoint(overlong, (int)sizeof(overlong) - 1, &cp));
	ASSERT_EQ_INT(0xC0, cp);

	return 0;
}

static int test_rope_copy_and_dup_range(void) {
	struct editorRope rope;
	editorRopeInit(&rope);

	ASSERT_TRUE(editorRopeResetFromString(&rope, "hello world", strlen("hello world")));
	ASSERT_EQ_INT((int)strlen("hello world"), (int)editorRopeLength(&rope));

	uint32_t read_len = 0;
	const char *chunk = editorRopeRead(&rope, 6, &read_len);
	ASSERT_TRUE(chunk != NULL);
	ASSERT_EQ_INT(5, read_len);
	ASSERT_TRUE(strncmp(chunk, "world", 5) == 0);

	char copy[6] = {0};
	ASSERT_TRUE(editorRopeCopyRange(&rope, 0, 5, copy));
	ASSERT_EQ_STR("hello", copy);

	size_t dup_len = 0;
	char *dup = editorRopeDupRange(&rope, 6, 11, &dup_len);
	ASSERT_TRUE(dup != NULL);
	ASSERT_EQ_INT(5, (int)dup_len);
	ASSERT_EQ_STR("world", dup);

	free(dup);
	editorRopeFree(&rope);
	return 0;
}

static int test_rope_replace_range_across_large_text(void) {
	struct editorRope rope;
	editorRopeInit(&rope);

	size_t source_len = 2400;
	char *source = malloc(source_len + 1);
	ASSERT_TRUE(source != NULL);
	for (size_t i = 0; i < source_len; i++) {
		source[i] = (char)('a' + (int)(i % 26));
	}
	source[source_len] = '\0';

	ASSERT_TRUE(editorRopeResetFromString(&rope, source, source_len));
	ASSERT_TRUE(editorRopeReplaceRange(&rope, 900, 700, "XYZ", 3));
	ASSERT_EQ_INT((int)(source_len - 700 + 3), (int)editorRopeLength(&rope));

	size_t full_len = 0;
	char *full = editorRopeDupRange(&rope, 0, editorRopeLength(&rope), &full_len);
	ASSERT_TRUE(full != NULL);
	ASSERT_EQ_INT((int)(source_len - 700 + 3), (int)full_len);
	ASSERT_TRUE(memcmp(full, source, 900) == 0);
	ASSERT_TRUE(memcmp(full + 900, "XYZ", 3) == 0);
	ASSERT_TRUE(memcmp(full + 903, source + 1600, source_len - 1600) == 0);

	free(full);
	free(source);
	editorRopeFree(&rope);
	return 0;
}

static int test_document_line_index_tracks_blank_lines_and_trailing_newline(void) {
	struct editorDocument document;
	editorDocumentInit(&document);

	const char *text = "alpha\n\nbeta\ngamma\n";
	ASSERT_TRUE(editorDocumentResetFromString(&document, text, strlen(text)));
	ASSERT_EQ_INT((int)strlen(text), (int)editorDocumentLength(&document));
	ASSERT_EQ_INT(4, editorDocumentLineCount(&document));

	size_t line_start = 0;
	ASSERT_TRUE(editorDocumentLineStartByte(&document, 0, &line_start));
	ASSERT_EQ_INT(0, (int)line_start);
	ASSERT_TRUE(editorDocumentLineStartByte(&document, 1, &line_start));
	ASSERT_EQ_INT(6, (int)line_start);
	ASSERT_TRUE(editorDocumentLineStartByte(&document, 2, &line_start));
	ASSERT_EQ_INT(7, (int)line_start);
	ASSERT_TRUE(editorDocumentLineStartByte(&document, 3, &line_start));
	ASSERT_EQ_INT(12, (int)line_start);

	int line_idx = -1;
	ASSERT_TRUE(editorDocumentLineIndexForByteOffset(&document, 0, &line_idx));
	ASSERT_EQ_INT(0, line_idx);
	ASSERT_TRUE(editorDocumentLineIndexForByteOffset(&document, 6, &line_idx));
	ASSERT_EQ_INT(1, line_idx);
	ASSERT_TRUE(editorDocumentLineIndexForByteOffset(&document, 8, &line_idx));
	ASSERT_EQ_INT(2, line_idx);
	ASSERT_TRUE(editorDocumentLineIndexForByteOffset(&document, 16, &line_idx));
	ASSERT_EQ_INT(3, line_idx);

	editorDocumentFree(&document);
	return 0;
}

static int test_document_replace_range_updates_text_and_line_index(void) {
	struct editorDocument document;
	editorDocumentInit(&document);

	const char *text = "one\ntwo\nthree\n";
	ASSERT_TRUE(editorDocumentResetFromString(&document, text, strlen(text)));
	ASSERT_TRUE(editorDocumentReplaceRange(&document, 4, 4, "two\n2b\n", 7));
	ASSERT_EQ_INT(4, editorDocumentLineCount(&document));

	size_t line_start = 0;
	ASSERT_TRUE(editorDocumentLineStartByte(&document, 0, &line_start));
	ASSERT_EQ_INT(0, (int)line_start);
	ASSERT_TRUE(editorDocumentLineStartByte(&document, 1, &line_start));
	ASSERT_EQ_INT(4, (int)line_start);
	ASSERT_TRUE(editorDocumentLineStartByte(&document, 2, &line_start));
	ASSERT_EQ_INT(8, (int)line_start);
	ASSERT_TRUE(editorDocumentLineStartByte(&document, 3, &line_start));
	ASSERT_EQ_INT(11, (int)line_start);

	size_t full_len = 0;
	char *full = editorDocumentDupRange(&document, 0, editorDocumentLength(&document), &full_len);
	ASSERT_TRUE(full != NULL);
	ASSERT_EQ_INT((int)strlen("one\ntwo\n2b\nthree\n"), (int)full_len);
	ASSERT_EQ_STR("one\ntwo\n2b\nthree\n", full);

	int line_idx = -1;
	ASSERT_TRUE(editorDocumentLineIndexForByteOffset(&document, 10, &line_idx));
	ASSERT_EQ_INT(2, line_idx);
	ASSERT_TRUE(editorDocumentLineIndexForByteOffset(&document, full_len, &line_idx));
	ASSERT_EQ_INT(3, line_idx);

	free(full);
	editorDocumentFree(&document);
	return 0;
}

static int test_document_reset_from_text_source_streams_bytes(void) {
	struct editorDocument document;
	editorDocumentInit(&document);

	const char *text = "alpha\nbeta\ngamma\n";
	struct editorTextSource source = {0};
	editorTextSourceInitString(&source, text, strlen(text));
	ASSERT_TRUE(editorDocumentResetFromTextSource(&document, &source));
	ASSERT_EQ_INT((int)strlen(text), (int)editorDocumentLength(&document));

	size_t len = 0;
	char *dup = editorDocumentDupRange(&document, 0, editorDocumentLength(&document), &len);
	ASSERT_TRUE(dup != NULL);
	ASSERT_EQ_INT((int)strlen(text), (int)len);
	ASSERT_EQ_STR(text, dup);

	free(dup);
	editorDocumentFree(&document);
	return 0;
}

static int test_document_position_offset_roundtrip(void) {
	struct editorDocument document;
	editorDocumentInit(&document);

	const char *text = "alpha\nbeta\n";
	ASSERT_TRUE(editorDocumentResetFromString(&document, text, strlen(text)));

	size_t offset = 0;
	ASSERT_TRUE(editorDocumentPositionToByteOffset(&document, 0, 0, &offset));
	ASSERT_EQ_INT(0, (int)offset);
	ASSERT_TRUE(editorDocumentPositionToByteOffset(&document, 0, 5, &offset));
	ASSERT_EQ_INT(5, (int)offset);
	ASSERT_TRUE(editorDocumentPositionToByteOffset(&document, 1, 0, &offset));
	ASSERT_EQ_INT(6, (int)offset);
	ASSERT_TRUE(editorDocumentPositionToByteOffset(&document, 1, 4, &offset));
	ASSERT_EQ_INT(10, (int)offset);
	ASSERT_TRUE(editorDocumentPositionToByteOffset(&document, 2, 0, &offset));
	ASSERT_EQ_INT(11, (int)offset);

	int line_idx = -1;
	size_t column = 0;
	ASSERT_TRUE(editorDocumentByteOffsetToPosition(&document, 0, &line_idx, &column));
	ASSERT_EQ_INT(0, line_idx);
	ASSERT_EQ_INT(0, (int)column);
	ASSERT_TRUE(editorDocumentByteOffsetToPosition(&document, 5, &line_idx, &column));
	ASSERT_EQ_INT(0, line_idx);
	ASSERT_EQ_INT(5, (int)column);
	ASSERT_TRUE(editorDocumentByteOffsetToPosition(&document, 6, &line_idx, &column));
	ASSERT_EQ_INT(1, line_idx);
	ASSERT_EQ_INT(0, (int)column);
	ASSERT_TRUE(editorDocumentByteOffsetToPosition(&document, 10, &line_idx, &column));
	ASSERT_EQ_INT(1, line_idx);
	ASSERT_EQ_INT(4, (int)column);
	ASSERT_TRUE(editorDocumentByteOffsetToPosition(&document, 11, &line_idx, &column));
	ASSERT_EQ_INT(2, line_idx);
	ASSERT_EQ_INT(0, (int)column);

	editorDocumentFree(&document);
	return 0;
}

static int test_editor_build_active_text_source_uses_document_after_open(void) {
	char path[64];
	ASSERT_TRUE(write_temp_text_file(path, sizeof(path), "alpha\nbeta\n"));

	editorDocumentTestResetStats();
	editorOpen(path);
	ASSERT_EQ_INT(1, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(1, editorRowCacheTestFullRebuildCount());
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());

	editorDocumentTestResetStats();
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(0, editorRowCacheTestFullRebuildCount());
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(0, editorRowCacheTestFullRebuildCount());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_document_incremental_updates_for_basic_edits(void) {
	add_row("abc");
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());
	editorDocumentTestResetStats();

	E.cy = 0;
	E.cx = 2;
	editorInsertChar('x');
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());

	E.cx = 1;
	editorInsertNewline();
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());

	E.cy = 1;
	E.cx = 1;
	editorDelChar();
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());

	struct editorSelectionRange range = {
		.start_cy = 0,
		.start_cx = 0,
		.end_cy = 1,
		.end_cx = 1
	};
	ASSERT_EQ_INT(1, editorDeleteRange(&range));
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(4, editorDocumentTestIncrementalUpdateCount());
	ASSERT_EQ_INT(0, editorRowCacheTestFullRebuildCount());
	ASSERT_EQ_INT(4, editorRowCacheTestSpliceUpdateCount());
	return 0;
}

static int test_editor_buffer_offset_roundtrip_uses_document_mapping(void) {
	add_row("alpha");
	add_row("beta");

	size_t offset = 0;
	ASSERT_TRUE(editorBufferPosToOffset(0, 0, &offset));
	ASSERT_EQ_INT(0, (int)offset);
	ASSERT_TRUE(editorBufferPosToOffset(0, 5, &offset));
	ASSERT_EQ_INT(5, (int)offset);
	ASSERT_TRUE(editorBufferPosToOffset(1, 0, &offset));
	ASSERT_EQ_INT(6, (int)offset);
	ASSERT_TRUE(editorBufferPosToOffset(1, 4, &offset));
	ASSERT_EQ_INT(10, (int)offset);
	ASSERT_TRUE(editorBufferPosToOffset(2, 0, &offset));
	ASSERT_EQ_INT(11, (int)offset);

	int cy = -1;
	int cx = -1;
	ASSERT_TRUE(editorBufferOffsetToPos(0, &cy, &cx));
	ASSERT_EQ_INT(0, cy);
	ASSERT_EQ_INT(0, cx);
	ASSERT_TRUE(editorBufferOffsetToPos(5, &cy, &cx));
	ASSERT_EQ_INT(0, cy);
	ASSERT_EQ_INT(5, cx);
	ASSERT_TRUE(editorBufferOffsetToPos(6, &cy, &cx));
	ASSERT_EQ_INT(1, cy);
	ASSERT_EQ_INT(0, cx);
	ASSERT_TRUE(editorBufferOffsetToPos(10, &cy, &cx));
	ASSERT_EQ_INT(1, cy);
	ASSERT_EQ_INT(4, cx);
	ASSERT_TRUE(editorBufferOffsetToPos(11, &cy, &cx));
	ASSERT_EQ_INT(2, cy);
	ASSERT_EQ_INT(0, cx);
	return 0;
}

static int test_editor_buffer_offsets_rebuild_document_after_row_mutation(void) {
	add_row("alpha");
	add_row("beta");
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());
	editorDocumentTestResetStats();

	E.cy = 1;
	E.cx = 2;
	editorInsertChar('X');
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(1, editorDocumentTestIncrementalUpdateCount());

	size_t offset = 0;
	ASSERT_TRUE(editorBufferPosToOffset(1, 5, &offset));
	ASSERT_EQ_INT(11, (int)offset);
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(1, editorDocumentTestIncrementalUpdateCount());

	int cy = -1;
	int cx = -1;
	ASSERT_TRUE(editorBufferOffsetToPos(11, &cy, &cx));
	ASSERT_EQ_INT(1, cy);
	ASSERT_EQ_INT(5, cx);
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(1, editorDocumentTestIncrementalUpdateCount());
	return 0;
}

static int test_editor_document_lazy_rebuild_after_low_level_row_mutation(void) {
	add_row("abc");
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());
	editorDocumentTestResetStats();

	E.cy = 0;
	E.cx = 1;
	editorInsertChar('X');
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(1, editorDocumentTestIncrementalUpdateCount());
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(1, editorDocumentTestIncrementalUpdateCount());
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(1, editorDocumentTestIncrementalUpdateCount());
	return 0;
}

static int test_editor_document_restored_for_undo_redo(void) {
	add_row("abc");
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());

	E.cy = 0;
	E.cx = 1;
	editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
	editorInsertChar('X');
	editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, 1);
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());

	editorDocumentTestResetStats();
	ASSERT_EQ_INT(1, editorUndo());
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());

	editorDocumentTestResetStats();
	ASSERT_EQ_INT(1, editorRedo());
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());
	return 0;
}

static int test_editor_document_edit_capture_uses_active_source(void) {
	add_row("alpha");
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());

	editorDocumentTestResetStats();
	editorActiveTextSourceBuildTestResetCount();
	editorActiveTextSourceDupTestResetCount();
	editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(0, editorActiveTextSourceBuildTestCount());
	ASSERT_EQ_INT(0, editorActiveTextSourceDupTestCount());
	ASSERT_EQ_INT(0, E.edit_pending_entry_valid);

	editorInsertChar('Z');
	ASSERT_EQ_INT(1, E.edit_pending_entry_valid);
	ASSERT_EQ_INT(EDITOR_EDIT_INSERT_TEXT, E.edit_pending_entry.kind);
	ASSERT_EQ_INT(0, (int)E.edit_pending_entry.removed_len);
	ASSERT_EQ_INT(1, (int)E.edit_pending_entry.inserted_len);
	ASSERT_TRUE(E.edit_pending_entry.inserted_text != NULL);
	ASSERT_MEM_EQ("Z", E.edit_pending_entry.inserted_text, 1);

	editorHistoryDiscardEdit();
	ASSERT_EQ_INT(0, E.edit_pending_entry_valid);
	return 0;
}

static int test_editor_document_selection_and_delete_use_active_source(void) {
	add_row("alpha");
	add_row("beta");
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());

	struct editorSelectionRange range = {
		.start_cy = 0,
		.start_cx = 1,
		.end_cy = 1,
		.end_cx = 2
	};

	editorDocumentTestResetStats();
	editorActiveTextSourceDupTestResetCount();
	char *selected = NULL;
	size_t selected_len = 0;
	ASSERT_EQ_INT(1, editorExtractRangeText(&range, &selected, &selected_len));
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(0, editorActiveTextSourceDupTestCount());
	ASSERT_EQ_INT(7, (int)selected_len);
	ASSERT_TRUE(selected != NULL);
	ASSERT_MEM_EQ("lpha\nbe", selected, selected_len);
	free(selected);

	editorDocumentTestResetStats();
	editorActiveTextSourceBuildTestResetCount();
	editorActiveTextSourceDupTestResetCount();
	ASSERT_EQ_INT(1, editorDeleteRange(&range));
	ASSERT_EQ_INT(0, editorActiveTextSourceBuildTestCount());
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(1, editorDocumentTestIncrementalUpdateCount());
	ASSERT_EQ_INT(0, editorActiveTextSourceDupTestCount());
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());
	return 0;
}

static int test_editor_buffer_find_uses_active_source_without_full_dup(void) {
	add_row("alpha");
	add_row("beta");

	editorActiveTextSourceDupTestResetCount();
	int row = -1;
	int col = -1;
	ASSERT_TRUE(editorBufferFindForward("ta", 0, -1, &row, &col));
	ASSERT_EQ_INT(1, row);
	ASSERT_EQ_INT(2, col);
	ASSERT_EQ_INT(0, editorActiveTextSourceDupTestCount());

	ASSERT_TRUE(editorBufferFindBackward("ph", 1, 4, &row, &col));
	ASSERT_EQ_INT(0, row);
	ASSERT_EQ_INT(2, col);
	ASSERT_EQ_INT(0, editorActiveTextSourceDupTestCount());
	return 0;
}

static int test_editor_document_save_uses_active_source(void) {
	char path[] = "/tmp/rotide-test-save-mirror-XXXXXX";
	int fd = mkstemp(path);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(close(fd) == 0);

	add_row("alpha");
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());
	E.filename = strdup(path);
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 1;

	editorDocumentTestResetStats();
	editorSave();
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(0, E.dirty);

	size_t content_len = 0;
	char *contents = read_file_contents(path, &content_len);
	ASSERT_TRUE(contents != NULL);
	ASSERT_EQ_INT(6, (int)content_len);
	ASSERT_MEM_EQ("alpha\n", contents, content_len);
	free(contents);
	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_utf8_continuation_detection(void) {
	ASSERT_TRUE(editorIsUtf8ContinuationByte(0x80));
	ASSERT_TRUE(editorIsUtf8ContinuationByte(0xBF));
	ASSERT_TRUE(!editorIsUtf8ContinuationByte(0x7F));
	ASSERT_TRUE(!editorIsUtf8ContinuationByte(0xC2));
	return 0;
}

static int test_grapheme_extend_classification(void) {
	ASSERT_TRUE(editorIsGraphemeExtendCodepoint(0x0301));
	ASSERT_TRUE(editorIsGraphemeExtendCodepoint(0xFE0F));
	ASSERT_TRUE(editorIsGraphemeExtendCodepoint(0x1F3FB));
	ASSERT_TRUE(editorIsGraphemeExtendCodepoint(0x200C));
	ASSERT_TRUE(!editorIsGraphemeExtendCodepoint(0x200D));
	ASSERT_TRUE(!editorIsGraphemeExtendCodepoint('A'));
	return 0;
}

static int test_char_display_width_basics(void) {
	const char e_acute[] = "\xC3\xA9";
	const char invalid[] = "\xFF";

	ASSERT_EQ_INT(1, editorCharDisplayWidth("A", 1));
	ASSERT_EQ_INT(0, editorCharDisplayWidth(&e_acute[1], 1));
	ASSERT_EQ_INT(1, editorCharDisplayWidth(invalid, (int)sizeof(invalid) - 1));
	return 0;
}

static int test_row_char_boundaries(void) {
	const char text[] = "A\xC3\xA9" "Z";
	add_row_bytes(text, sizeof(text) - 1);

	struct erow *row = &E.rows[0];
	ASSERT_EQ_INT(1, editorRowClampCxToCharBoundary(row, 2));
	ASSERT_EQ_INT(0, editorRowPrevCharIdx(row, 1));
	ASSERT_EQ_INT(1, editorRowPrevCharIdx(row, 3));
	ASSERT_EQ_INT(3, editorRowNextCharIdx(row, 1));
	ASSERT_EQ_INT(4, editorRowNextCharIdx(row, 3));
	return 0;
}

static int test_row_cluster_boundaries_combining(void) {
	const char text[] = "a\xCC\x81" "b";
	add_row_bytes(text, sizeof(text) - 1);

	struct erow *row = &E.rows[0];
	ASSERT_EQ_INT(3, editorRowNextClusterIdx(row, 0));
	ASSERT_EQ_INT(0, editorRowPrevClusterIdx(row, 3));
	ASSERT_EQ_INT(4, editorRowNextClusterIdx(row, 3));
	ASSERT_EQ_INT(0, editorRowClampCxToClusterBoundary(row, 2));
	ASSERT_EQ_INT(3, editorRowClampCxToClusterBoundary(row, 3));
	return 0;
}

static int test_row_cluster_boundaries_zwj_sequence(void) {
	const char woman_technologist[] = "\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB";
	add_row_bytes(woman_technologist, sizeof(woman_technologist) - 1);

	struct erow *row = &E.rows[0];
	ASSERT_EQ_INT((int)sizeof(woman_technologist) - 1, editorRowNextClusterIdx(row, 0));
	ASSERT_EQ_INT(0, editorRowPrevClusterIdx(row, (int)sizeof(woman_technologist) - 1));
	return 0;
}

static int test_row_cluster_boundaries_regional_indicators(void) {
	const char flag_sequence[] = "\xF0\x9F\x87\xAB\xF0\x9F\x87\xAE\xF0\x9F\x87\xA8";
	add_row_bytes(flag_sequence, sizeof(flag_sequence) - 1);

	struct erow *row = &E.rows[0];
	ASSERT_EQ_INT(8, editorRowNextClusterIdx(row, 0));
	ASSERT_EQ_INT((int)sizeof(flag_sequence) - 1, editorRowNextClusterIdx(row, 8));
	ASSERT_EQ_INT(8, editorRowPrevClusterIdx(row, (int)sizeof(flag_sequence) - 1));
	return 0;
}

static int test_row_cx_to_rx_with_tabs(void) {
	add_row("a\tb");
	struct erow *row = &E.rows[0];
	ASSERT_EQ_INT(8, editorRowCxToRx(row, 2));
	ASSERT_EQ_INT(9, editorRowCxToRx(row, 3));
	return 0;
}

static int test_row_rx_to_cx_with_tabs(void) {
	add_row("a\tb");
	struct erow *row = &E.rows[0];
	ASSERT_EQ_INT(0, editorRowRxToCx(row, 0));
	ASSERT_EQ_INT(1, editorRowRxToCx(row, 1));
	ASSERT_EQ_INT(1, editorRowRxToCx(row, 7));
	ASSERT_EQ_INT(2, editorRowRxToCx(row, 8));
	ASSERT_EQ_INT(3, editorRowRxToCx(row, 9));
	return 0;
}

static int test_editor_update_row_expands_tabs(void) {
	add_row("a\tb");
	struct erow *row = &E.rows[0];
	ASSERT_EQ_INT(9, row->rsize);
	ASSERT_EQ_STR("a       b", row->render);
	return 0;
}

static int test_editor_update_row_tab_alignment_after_multibyte(void) {
	const char text[] = "\xC3\xB6\tX";
	add_row_bytes(text, sizeof(text) - 1);
	struct erow *row = &E.rows[0];

	const char expected[] = "\xC3\xB6       X";
	ASSERT_EQ_INT((int)sizeof(expected) - 1, row->rsize);
	ASSERT_MEM_EQ(expected, row->render, (size_t)row->rsize);
	return 0;
}

static int test_editor_update_row_escapes_c0_and_esc_in_render(void) {
	const char text[] = "A\x1b\x7f";
	add_row_bytes(text, sizeof(text) - 1);
	struct erow *row = &E.rows[0];

	const char expected[] = "A^[^?";
	ASSERT_EQ_INT((int)sizeof(expected) - 1, row->rsize);
	ASSERT_MEM_EQ(expected, row->render, (size_t)row->rsize);
	return 0;
}

static int test_editor_update_row_escapes_c1_codepoints_in_render(void) {
	const char text[] = "\xC2\x9BZ";
	add_row_bytes(text, sizeof(text) - 1);
	struct erow *row = &E.rows[0];

	const char expected[] = "\\x9BZ";
	ASSERT_EQ_INT((int)sizeof(expected) - 1, row->rsize);
	ASSERT_MEM_EQ(expected, row->render, (size_t)row->rsize);
	return 0;
}

static int test_editor_update_row_preserves_printable_utf8_with_80_9f_continuations(void) {
	const char text[] = "\xC4\x80X";
	add_row_bytes(text, sizeof(text) - 1);
	struct erow *row = &E.rows[0];

	ASSERT_EQ_INT((int)sizeof(text) - 1, row->rsize);
	ASSERT_MEM_EQ(text, row->render, (size_t)row->rsize);
	return 0;
}

static int test_row_cx_to_rx_with_escaped_controls(void) {
	const char text[] = "A\x1b" "B";
	add_row_bytes(text, sizeof(text) - 1);
	struct erow *row = &E.rows[0];

	ASSERT_EQ_INT(0, editorRowCxToRx(row, 0));
	ASSERT_EQ_INT(1, editorRowCxToRx(row, 1));
	ASSERT_EQ_INT(3, editorRowCxToRx(row, 2));
	ASSERT_EQ_INT(4, editorRowCxToRx(row, 3));
	return 0;
}

static int test_row_rx_to_cx_with_escaped_controls(void) {
	const char text[] = "A\x1b" "B";
	add_row_bytes(text, sizeof(text) - 1);
	struct erow *row = &E.rows[0];

	ASSERT_EQ_INT(0, editorRowRxToCx(row, 0));
	ASSERT_EQ_INT(1, editorRowRxToCx(row, 1));
	ASSERT_EQ_INT(1, editorRowRxToCx(row, 2));
	ASSERT_EQ_INT(2, editorRowRxToCx(row, 3));
	ASSERT_EQ_INT(3, editorRowRxToCx(row, 4));
	return 0;
}

static int test_insert_and_delete_row_updates_dirty(void) {
	add_row("one");
	add_row("two");
	E.dirty = 0;
	ASSERT_EQ_INT(2, E.numrows);
	E.cy = 0;
	E.cx = 0;
	editorInsertNewline();
	ASSERT_EQ_INT(3, E.numrows);
	ASSERT_EQ_INT(1, E.dirty);

	struct editorSelectionRange range = {
		.start_cy = 0,
		.start_cx = 0,
		.end_cy = 1,
		.end_cx = 0
	};
	ASSERT_EQ_INT(1, editorDeleteRange(&range));
	ASSERT_EQ_INT(2, E.numrows);
	ASSERT_EQ_STR("one", E.rows[0].chars);
	ASSERT_EQ_STR("two", E.rows[1].chars);
	ASSERT_EQ_INT(3, E.dirty);
	return 0;
}

static int test_editor_delete_row_rejects_idx_at_numrows(void) {
	add_row("only");
	E.dirty = 0;

	struct editorSelectionRange range = {
		.start_cy = 0,
		.start_cx = 4,
		.end_cy = 0,
		.end_cx = 4
	};
	ASSERT_EQ_INT(0, editorDeleteRange(&range));
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("only", E.rows[0].chars);
	ASSERT_EQ_INT(0, E.dirty);
	return 0;
}

static int test_insert_and_delete_chars(void) {
	add_row("abc");
	E.dirty = 0;

	E.cy = 0;
	E.cx = 1;
	editorInsertChar('X');
	ASSERT_EQ_STR("aXbc", E.rows[0].chars);
	ASSERT_EQ_INT(1, E.dirty);

	struct editorSelectionRange delete_one = {
		.start_cy = 0,
		.start_cx = 2,
		.end_cy = 0,
		.end_cx = 3
	};
	ASSERT_EQ_INT(1, editorDeleteRange(&delete_one));
	ASSERT_EQ_STR("aXc", E.rows[0].chars);
	ASSERT_EQ_INT(2, E.dirty);

	struct editorSelectionRange delete_two = {
		.start_cy = 0,
		.start_cx = 1,
		.end_cy = 0,
		.end_cx = 3
	};
	ASSERT_EQ_INT(1, editorDeleteRange(&delete_two));
	ASSERT_EQ_STR("a", E.rows[0].chars);
	ASSERT_EQ_INT(3, E.dirty);

	struct editorSelectionRange noop = {
		.start_cy = 0,
		.start_cx = 1,
		.end_cy = 0,
		.end_cx = 1
	};
	ASSERT_EQ_INT(0, editorDeleteRange(&noop));
	ASSERT_EQ_STR("a", E.rows[0].chars);
	ASSERT_EQ_INT(3, E.dirty);
	return 0;
}

static int test_editor_del_char_at_rejects_idx_at_row_size(void) {
	add_row("abc");
	E.dirty = 0;

	struct editorSelectionRange noop = {
		.start_cy = 0,
		.start_cx = 3,
		.end_cy = 0,
		.end_cx = 3
	};
	ASSERT_EQ_INT(0, editorDeleteRange(&noop));
	ASSERT_EQ_INT(3, E.rows[0].size);
	ASSERT_EQ_STR("abc", E.rows[0].chars);
	ASSERT_EQ_INT(0, E.dirty);
	return 0;
}

static int test_editor_insert_char_creates_initial_row(void) {
	editorInsertChar('Q');
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("Q", E.rows[0].chars);
	ASSERT_EQ_INT(1, E.cx);
	ASSERT_EQ_INT(2, E.dirty);
	return 0;
}

static int test_editor_insert_newline_splits_row(void) {
	add_row("hello");
	E.dirty = 0;
	E.cy = 0;
	E.cx = 2;

	editorInsertNewline();
	ASSERT_EQ_INT(2, E.numrows);
	ASSERT_EQ_STR("he", E.rows[0].chars);
	ASSERT_EQ_STR("llo", E.rows[1].chars);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(0, E.cx);
	ASSERT_EQ_INT(1, E.dirty);
	return 0;
}

static int test_editor_insert_newline_at_row_start(void) {
	add_row("hello");
	E.dirty = 0;
	E.cy = 0;
	E.cx = 0;

	editorInsertNewline();
	ASSERT_EQ_INT(2, E.numrows);
	ASSERT_EQ_STR("", E.rows[0].chars);
	ASSERT_EQ_STR("hello", E.rows[1].chars);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(1, E.dirty);
	return 0;
}

static int test_editor_del_char_cluster_and_merge(void) {
	const char with_combining[] = "a\xCC\x81" "b";
	add_row_bytes(with_combining, sizeof(with_combining) - 1);
	E.cy = 0;
	E.cx = 3;
	E.dirty = 0;

	editorDelChar();
	ASSERT_EQ_STR("b", E.rows[0].chars);
	ASSERT_EQ_INT(0, E.cx);
	ASSERT_EQ_INT(1, E.dirty);

	reset_editor_state();
	add_row("abc");
	E.cy = 0;
	E.cx = 3;
	editorInsertNewline();
	ASSERT_EQ_INT(2, E.numrows);
	ASSERT_EQ_INT(1, editorInsertText("def", 3));
	E.dirty = 0;
	E.cy = 1;
	E.cx = 0;

	editorDelChar();
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("abcdef", E.rows[0].chars);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(3, E.cx);
	ASSERT_EQ_INT(2, E.dirty);
	return 0;
}

static int test_editor_rows_to_str(void) {
	add_row("a");
	add_row("bc");
	add_row("");

	size_t buflen = 0;
	char *joined = editorRowsToStr(&buflen);
	ASSERT_TRUE(joined != NULL);
	ASSERT_EQ_INT(6, buflen);
	ASSERT_MEM_EQ("a\nbc\n\n", joined, (size_t)buflen);
	free(joined);

	editorDocumentTestResetStats();
	E.cy = 1;
	E.cx = 1;
	editorInsertChar('X');
	joined = editorRowsToStr(&buflen);
	ASSERT_TRUE(joined != NULL);
	ASSERT_EQ_INT(7, buflen);
	ASSERT_MEM_EQ("a\nbXc\n\n", joined, (size_t)buflen);
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(1, editorDocumentTestIncrementalUpdateCount());
	free(joined);
	return 0;
}

static int test_editor_rows_to_str_uses_document_when_row_cache_corrupt(void) {
	add_row("abc");
	E.rows[0].size = INT_MAX;

	size_t buflen = 0;
	errno = 0;
	char *joined = editorRowsToStr(&buflen);
	ASSERT_TRUE(joined != NULL);
	ASSERT_EQ_INT(4, buflen);
	ASSERT_MEM_EQ("abc\n", joined, buflen);
	free(joined);
	return 0;
}

static int test_editor_open_reads_rows_and_clears_dirty(void) {
	char path[] = "/tmp/rotide-test-open-XXXXXX";
	int fd = mkstemp(path);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(write_all(fd, "alpha\r\nbeta\n\n", 13) == 0);
	ASSERT_TRUE(close(fd) == 0);

	editorOpen(path);

	ASSERT_EQ_STR(path, E.filename);
	ASSERT_EQ_INT(3, E.numrows);
	ASSERT_EQ_STR("alpha", E.rows[0].chars);
	ASSERT_EQ_STR("beta", E.rows[1].chars);
	ASSERT_EQ_STR("", E.rows[2].chars);
	ASSERT_EQ_INT(0, E.dirty);

	unlink(path);
	return 0;
}

static int test_editor_syntax_activation_for_c_and_h_files(void) {
	char c_path[] = "/tmp/rotide-test-syntax-c-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(c_path, 2,
			"tests/syntax/supported/c/activation.c"));

	editorOpen(c_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("translation_unit", editorSyntaxRootType());

	char h_path[] = "/tmp/rotide-test-syntax-h-XXXXXX.h";
	ASSERT_TRUE(write_fixture_to_temp_path(h_path, 2,
			"tests/syntax/supported/c/activation.h"));

	editorOpen(h_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("translation_unit", editorSyntaxRootType());

	char cpp_path[] = "/tmp/rotide-test-syntax-cpp-XXXXXX.cpp";
	ASSERT_TRUE(write_fixture_to_temp_path(cpp_path, 4,
			"tests/syntax/supported/c/activation.c"));

	editorOpen(cpp_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("translation_unit", editorSyntaxRootType());

	char hpp_path[] = "/tmp/rotide-test-syntax-hpp-XXXXXX.hpp";
	ASSERT_TRUE(write_fixture_to_temp_path(hpp_path, 4,
			"tests/syntax/supported/c/activation.h"));

	editorOpen(hpp_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("translation_unit", editorSyntaxRootType());

	ASSERT_TRUE(unlink(c_path) == 0);
	ASSERT_TRUE(unlink(h_path) == 0);
	ASSERT_TRUE(unlink(cpp_path) == 0);
	ASSERT_TRUE(unlink(hpp_path) == 0);
	return 0;
}

static int test_editor_syntax_activation_for_shell_files_and_shebang(void) {
	char sh_path[] = "/tmp/rotide-test-syntax-shell-XXXXXX.sh";
	ASSERT_TRUE(write_fixture_to_temp_path(sh_path, 3,
			"tests/syntax/supported/bash/activation.sh"));

	editorOpen(sh_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_SHELL, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);

	char rc_dir_template[] = "/tmp/rotide-test-syntax-shell-rc-XXXXXX";
	char *rc_dir = mkdtemp(rc_dir_template);
	ASSERT_TRUE(rc_dir != NULL);
	char rc_path[512];
	ASSERT_TRUE(path_join(rc_path, sizeof(rc_path), rc_dir, ".bashrc"));
	ASSERT_TRUE(copyTestFixtureToPath("tests/syntax/supported/bash/.bashrc", rc_path));

	ASSERT_TRUE(editorTabsInit());
	editorOpen(rc_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_SHELL, editorSyntaxLanguageActive());

	char shebang_path[] = "/tmp/rotide-test-syntax-shell-shebang-XXXXXX";
	ASSERT_TRUE(write_fixture_to_temp_path(shebang_path, 0,
			"tests/syntax/supported/bash/extensionless_shebang"));

	ASSERT_TRUE(editorTabsInit());
	editorOpen(shebang_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_SHELL, editorSyntaxLanguageActive());

	char plain_path[] = "/tmp/rotide-test-syntax-shell-plain-XXXXXX";
	int plain_fd = mkstemp(plain_path);
	ASSERT_TRUE(plain_fd != -1);
	const char *plain_source = "echo plain\n";
	ASSERT_TRUE(write_all(plain_fd, plain_source, strlen(plain_source)) == 0);
	ASSERT_TRUE(close(plain_fd) == 0);

	ASSERT_TRUE(editorTabsInit());
	editorOpen(plain_path);
	ASSERT_TRUE(!editorSyntaxEnabled());
	ASSERT_TRUE(!editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_NONE, editorSyntaxLanguageActive());

	ASSERT_TRUE(unlink(sh_path) == 0);
	ASSERT_TRUE(unlink(rc_path) == 0);
	ASSERT_TRUE(unlink(shebang_path) == 0);
	ASSERT_TRUE(unlink(plain_path) == 0);
	ASSERT_TRUE(rmdir(rc_dir) == 0);
	return 0;
}

static int test_editor_syntax_activation_for_html_js_and_css_files(void) {
	char html_path[] = "/tmp/rotide-test-syntax-html-XXXXXX.html";
	ASSERT_TRUE(write_fixture_to_temp_path(html_path, 5,
			"tests/syntax/supported/html/activation.html"));

	editorOpen(html_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_HTML, editorSyntaxLanguageActive());
	ASSERT_EQ_STR("document", editorSyntaxRootType());

	char htm_path[] = "/tmp/rotide-test-syntax-html2-XXXXXX.htm";
	ASSERT_TRUE(write_fixture_to_temp_path(htm_path, 4,
			"tests/syntax/supported/html/activation.htm"));
	editorOpen(htm_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_HTML, editorSyntaxLanguageActive());

	char xhtml_path[] = "/tmp/rotide-test-syntax-xhtml-XXXXXX.xhtml";
	ASSERT_TRUE(write_fixture_to_temp_path(xhtml_path, 6,
			"tests/syntax/supported/html/activation.xhtml"));
	editorOpen(xhtml_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_HTML, editorSyntaxLanguageActive());

	char js_path[] = "/tmp/rotide-test-syntax-js-XXXXXX.js";
	ASSERT_TRUE(write_fixture_to_temp_path(js_path, 3,
			"tests/syntax/supported/javascript/activation.js"));
	editorOpen(js_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_JAVASCRIPT, editorSyntaxLanguageActive());

	char mjs_path[] = "/tmp/rotide-test-syntax-mjs-XXXXXX.mjs";
	ASSERT_TRUE(write_fixture_to_temp_path(mjs_path, 4,
			"tests/syntax/supported/javascript/module.mjs"));
	editorOpen(mjs_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_JAVASCRIPT, editorSyntaxLanguageActive());

	char cjs_path[] = "/tmp/rotide-test-syntax-cjs-XXXXXX.cjs";
	ASSERT_TRUE(write_fixture_to_temp_path(cjs_path, 4,
			"tests/syntax/supported/javascript/commonjs.cjs"));
	editorOpen(cjs_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_JAVASCRIPT, editorSyntaxLanguageActive());

	char jsx_path[] = "/tmp/rotide-test-syntax-jsx-XXXXXX.jsx";
	ASSERT_TRUE(write_fixture_to_temp_path(jsx_path, 4,
			"tests/syntax/supported/javascript/component.jsx"));
	editorOpen(jsx_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_JAVASCRIPT, editorSyntaxLanguageActive());

	char css_path[] = "/tmp/rotide-test-syntax-css-XXXXXX.css";
	ASSERT_TRUE(write_fixture_to_temp_path(css_path, 4,
			"tests/syntax/supported/css/activation.css"));
	editorOpen(css_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_CSS, editorSyntaxLanguageActive());

	char scss_path[] = "/tmp/rotide-test-syntax-scss-XXXXXX.scss";
	ASSERT_TRUE(write_fixture_to_temp_path(scss_path, 5,
			"tests/syntax/supported/css/activation.scss"));
	editorOpen(scss_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_CSS, editorSyntaxLanguageActive());

	ASSERT_TRUE(unlink(html_path) == 0);
	ASSERT_TRUE(unlink(htm_path) == 0);
	ASSERT_TRUE(unlink(xhtml_path) == 0);
	ASSERT_TRUE(unlink(js_path) == 0);
	ASSERT_TRUE(unlink(mjs_path) == 0);
	ASSERT_TRUE(unlink(cjs_path) == 0);
	ASSERT_TRUE(unlink(jsx_path) == 0);
	ASSERT_TRUE(unlink(css_path) == 0);
	ASSERT_TRUE(unlink(scss_path) == 0);
	return 0;
}

static int test_editor_syntax_activation_for_go_and_mod_files(void) {
	char go_path[] = "/tmp/rotide-test-syntax-go-XXXXXX.go";
	ASSERT_TRUE(write_fixture_to_temp_path(go_path, 3,
			"tests/syntax/supported/go/activation.go"));

	editorOpen(go_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_GO, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("source_file", editorSyntaxRootType());

	char mod_dir_template[] = "/tmp/rotide-test-syntax-go-mod-XXXXXX";
	char *mod_dir = mkdtemp(mod_dir_template);
	ASSERT_TRUE(mod_dir != NULL);

	char mod_path[512];
	ASSERT_TRUE(path_join(mod_path, sizeof(mod_path), mod_dir, "go.mod"));
	ASSERT_TRUE(copyTestFixtureToPath("tests/syntax/supported/go/go.mod", mod_path));

	editorOpen(mod_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_GO, editorSyntaxLanguageActive());

	char sum_path[512];
	ASSERT_TRUE(path_join(sum_path, sizeof(sum_path), mod_dir, "go.sum"));
	ASSERT_TRUE(copyTestFixtureToPath("tests/syntax/supported/go/go.sum", sum_path));

	editorOpen(sum_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_GO, editorSyntaxLanguageActive());

	ASSERT_TRUE(unlink(go_path) == 0);
	ASSERT_TRUE(unlink(mod_path) == 0);
	ASSERT_TRUE(unlink(sum_path) == 0);
	ASSERT_TRUE(rmdir(mod_dir) == 0);
	return 0;
}

static int test_editor_syntax_disabled_for_non_c_or_shell_files(void) {
	char path[] = "/tmp/rotide-test-syntax-txt-XXXXXX.txt";
	int fd = mkstemps(path, 4);
	ASSERT_TRUE(fd != -1);
	const char *text_source = "#!/usr/bin/env bash\necho not-shell-because-extension\n";
	ASSERT_TRUE(write_all(fd, text_source, strlen(text_source)) == 0);
	ASSERT_TRUE(close(fd) == 0);

	editorOpen(path);
	ASSERT_TRUE(!editorSyntaxEnabled());
	ASSERT_TRUE(!editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_NONE, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() == NULL);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_save_as_shell_and_non_shell_updates_syntax(void) {
	char shell_path[] = "/tmp/rotide-test-syntax-saveas-shell-XXXXXX.sh";
	int shell_fd = mkstemps(shell_path, 3);
	ASSERT_TRUE(shell_fd != -1);
	ASSERT_TRUE(close(shell_fd) == 0);
	ASSERT_TRUE(unlink(shell_path) == 0);

	char txt_path[] = "/tmp/rotide-test-syntax-saveas-shell-XXXXXX.txt";
	int txt_fd = mkstemps(txt_path, 4);
	ASSERT_TRUE(txt_fd != -1);
	ASSERT_TRUE(close(txt_fd) == 0);
	ASSERT_TRUE(unlink(txt_path) == 0);

	add_row("#!/usr/bin/env bash");
	add_row("echo \"$HOME\"");
	E.dirty = 1;
	ASSERT_TRUE(E.filename == NULL);

	char shell_input[256];
	int shell_written = snprintf(shell_input, sizeof(shell_input), "%s\r", shell_path);
	ASSERT_TRUE(shell_written > 0 && (size_t)shell_written < sizeof(shell_input));

	int saved_stdin;
	int saved_stdout;
	ASSERT_TRUE(setup_stdin_bytes(shell_input, (size_t)shell_written, &saved_stdin) == 0);
	ASSERT_TRUE(redirect_stdout_to_devnull(&saved_stdout) == 0);
	editorSave();
	ASSERT_TRUE(restore_stdout(saved_stdout) == 0);
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);

	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_SHELL, editorSyntaxLanguageActive());

	E.dirty = 1;
	free(E.filename);
	E.filename = NULL;
	char txt_input[256];
	int txt_written = snprintf(txt_input, sizeof(txt_input), "%s\r", txt_path);
	ASSERT_TRUE(txt_written > 0 && (size_t)txt_written < sizeof(txt_input));

	ASSERT_TRUE(setup_stdin_bytes(txt_input, (size_t)txt_written, &saved_stdin) == 0);
	ASSERT_TRUE(redirect_stdout_to_devnull(&saved_stdout) == 0);
	editorSave();
	ASSERT_TRUE(restore_stdout(saved_stdout) == 0);
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);

	ASSERT_TRUE(!editorSyntaxEnabled());
	ASSERT_TRUE(!editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_NONE, editorSyntaxLanguageActive());

	ASSERT_TRUE(unlink(shell_path) == 0);
	ASSERT_TRUE(unlink(txt_path) == 0);
	return 0;
}

static int test_editor_save_as_web_and_plain_updates_syntax(void) {
	char html_path[] = "/tmp/rotide-test-syntax-saveas-web-XXXXXX.html";
	int html_fd = mkstemps(html_path, 5);
	ASSERT_TRUE(html_fd != -1);
	ASSERT_TRUE(close(html_fd) == 0);
	ASSERT_TRUE(unlink(html_path) == 0);

	char js_path[] = "/tmp/rotide-test-syntax-saveas-web-XXXXXX.js";
	int js_fd = mkstemps(js_path, 3);
	ASSERT_TRUE(js_fd != -1);
	ASSERT_TRUE(close(js_fd) == 0);
	ASSERT_TRUE(unlink(js_path) == 0);

	char css_path[] = "/tmp/rotide-test-syntax-saveas-web-XXXXXX.css";
	int css_fd = mkstemps(css_path, 4);
	ASSERT_TRUE(css_fd != -1);
	ASSERT_TRUE(close(css_fd) == 0);
	ASSERT_TRUE(unlink(css_path) == 0);

	char txt_path[] = "/tmp/rotide-test-syntax-saveas-web-XXXXXX.txt";
	int txt_fd = mkstemps(txt_path, 4);
	ASSERT_TRUE(txt_fd != -1);
	ASSERT_TRUE(close(txt_fd) == 0);
	ASSERT_TRUE(unlink(txt_path) == 0);

	add_row("<div class=\"x\">hi</div>");
	E.dirty = 1;
	ASSERT_TRUE(E.filename == NULL);

	int saved_stdin;
	int saved_stdout;

	char input_html[256];
	int written_html = snprintf(input_html, sizeof(input_html), "%s\r", html_path);
	ASSERT_TRUE(written_html > 0 && (size_t)written_html < sizeof(input_html));
	ASSERT_TRUE(setup_stdin_bytes(input_html, (size_t)written_html, &saved_stdin) == 0);
	ASSERT_TRUE(redirect_stdout_to_devnull(&saved_stdout) == 0);
	editorSave();
	ASSERT_TRUE(restore_stdout(saved_stdout) == 0);
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);
	ASSERT_EQ_INT(EDITOR_SYNTAX_HTML, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.dirty = 1;
	free(E.filename);
	E.filename = NULL;
	char input_js[256];
	int written_js = snprintf(input_js, sizeof(input_js), "%s\r", js_path);
	ASSERT_TRUE(written_js > 0 && (size_t)written_js < sizeof(input_js));
	ASSERT_TRUE(setup_stdin_bytes(input_js, (size_t)written_js, &saved_stdin) == 0);
	ASSERT_TRUE(redirect_stdout_to_devnull(&saved_stdout) == 0);
	editorSave();
	ASSERT_TRUE(restore_stdout(saved_stdout) == 0);
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);
	ASSERT_EQ_INT(EDITOR_SYNTAX_JAVASCRIPT, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.dirty = 1;
	free(E.filename);
	E.filename = NULL;
	char input_css[256];
	int written_css = snprintf(input_css, sizeof(input_css), "%s\r", css_path);
	ASSERT_TRUE(written_css > 0 && (size_t)written_css < sizeof(input_css));
	ASSERT_TRUE(setup_stdin_bytes(input_css, (size_t)written_css, &saved_stdin) == 0);
	ASSERT_TRUE(redirect_stdout_to_devnull(&saved_stdout) == 0);
	editorSave();
	ASSERT_TRUE(restore_stdout(saved_stdout) == 0);
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);
	ASSERT_EQ_INT(EDITOR_SYNTAX_CSS, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.dirty = 1;
	free(E.filename);
	E.filename = NULL;
	char input_txt[256];
	int written_txt = snprintf(input_txt, sizeof(input_txt), "%s\r", txt_path);
	ASSERT_TRUE(written_txt > 0 && (size_t)written_txt < sizeof(input_txt));
	ASSERT_TRUE(setup_stdin_bytes(input_txt, (size_t)written_txt, &saved_stdin) == 0);
	ASSERT_TRUE(redirect_stdout_to_devnull(&saved_stdout) == 0);
	editorSave();
	ASSERT_TRUE(restore_stdout(saved_stdout) == 0);
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);
	ASSERT_EQ_INT(EDITOR_SYNTAX_NONE, editorSyntaxLanguageActive());
	ASSERT_TRUE(!editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(html_path) == 0);
	ASSERT_TRUE(unlink(js_path) == 0);
	ASSERT_TRUE(unlink(css_path) == 0);
	ASSERT_TRUE(unlink(txt_path) == 0);
	return 0;
}

static int test_editor_save_as_c_file_enables_syntax(void) {
	char path[] = "/tmp/rotide-test-syntax-saveas-XXXXXX.c";
	int fd = mkstemps(path, 2);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(close(fd) == 0);
	ASSERT_TRUE(unlink(path) == 0);

	add_row("int main(void) { return 0; }");
	E.dirty = 1;
	ASSERT_TRUE(E.filename == NULL);

	char input[256];
	int written = snprintf(input, sizeof(input), "%s\r", path);
	ASSERT_TRUE(written > 0);
	ASSERT_TRUE((size_t)written < sizeof(input));

	int saved_stdin;
	int saved_stdout;
	ASSERT_TRUE(setup_stdin_bytes(input, (size_t)written, &saved_stdin) == 0);
	ASSERT_TRUE(redirect_stdout_to_devnull(&saved_stdout) == 0);

	editorSave();

	ASSERT_TRUE(restore_stdout(saved_stdout) == 0);
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);

	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_save_as_go_file_enables_syntax(void) {
	char path[] = "/tmp/rotide-test-syntax-saveas-go-XXXXXX.go";
	int fd = mkstemps(path, 3);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(close(fd) == 0);
	ASSERT_TRUE(unlink(path) == 0);

	add_row("package main");
	add_row("");
	add_row("func main() { var n int = 1 }");
	E.dirty = 1;
	ASSERT_TRUE(E.filename == NULL);

	char input[256];
	int written = snprintf(input, sizeof(input), "%s\r", path);
	ASSERT_TRUE(written > 0);
	ASSERT_TRUE((size_t)written < sizeof(input));

	int saved_stdin;
	int saved_stdout;
	ASSERT_TRUE(setup_stdin_bytes(input, (size_t)written, &saved_stdin) == 0);
	ASSERT_TRUE(redirect_stdout_to_devnull(&saved_stdout) == 0);

	editorSave();

	ASSERT_TRUE(restore_stdout(saved_stdout) == 0);
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);

	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_GO, editorSyntaxLanguageActive());
	ASSERT_TRUE(editorSyntaxRootType() != NULL);
	ASSERT_EQ_STR("source_file", editorSyntaxRootType());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 2,
			"tests/syntax/supported/c/incremental.c"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 1;
	E.cx = 1;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());

	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = E.rows[0].size;
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	struct editorSelectionRange delete_line = {
		.start_cy = 0,
		.start_cx = 0,
		.end_cy = 1,
		.end_cx = 0
	};
	ASSERT_EQ_INT(1, editorDeleteRange(&delete_line));
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_shell_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-shell-XXXXXX.sh";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/bash/incremental.sh"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_SHELL, editorSyntaxLanguageActive());

	E.cy = 2;
	E.cx = 1;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());

	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = E.rows[0].size;
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	struct editorSelectionRange delete_line = {
		.start_cy = 0,
		.start_cx = 0,
		.end_cy = 1,
		.end_cx = 0
	};
	ASSERT_EQ_INT(1, editorDeleteRange(&delete_line));
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_html_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-html-XXXXXX.html";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 5,
			"tests/syntax/supported/html/incremental.html"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_HTML, editorSyntaxLanguageActive());

	E.cy = 3;
	E.cx = 6;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 6;
	E.cx = 6;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	struct editorSelectionRange delete_line = {
		.start_cy = 1,
		.start_cx = 0,
		.end_cy = 2,
		.end_cx = 0
	};
	ASSERT_EQ_INT(1, editorDeleteRange(&delete_line));
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_javascript_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-js-XXXXXX.js";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/javascript/incremental.js"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_JAVASCRIPT, editorSyntaxLanguageActive());

	E.cy = 1;
	E.cx = 1;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = E.rows[0].size;
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_css_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-css-XXXXXX.css";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 4,
			"tests/syntax/supported/css/incremental.css"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_CSS, editorSyntaxLanguageActive());

	E.cy = 0;
	E.cx = 1;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = E.rows[0].size;
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_incremental_edits_keep_go_tree_valid(void) {
	char path[] = "/tmp/rotide-test-syntax-inc-go-XXXXXX.go";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/go/incremental.go"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_GO, editorSyntaxLanguageActive());

	E.cy = 3;
	E.cx = 1;
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxTreeExists());
	editorDelChar();
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 2;
	E.cx = E.rows[2].size;
	editorInsertNewline();
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_query_budget_match_limit_is_graceful(void) {
	size_t source_len = 0;
	char *source = build_repeated_text("const value = document + window;\n", 512, &source_len);
	ASSERT_TRUE(source != NULL);
	ASSERT_TRUE(source_len <= UINT32_MAX);
	struct editorTextSource source_view = {0};
	editorTextSourceInitString(&source_view, source, source_len);

	editorSyntaxTestSetBudgetOverrides(1, 1, 0, 2000000000ULL);
	struct editorSyntaxState *state = editorSyntaxStateCreate(EDITOR_SYNTAX_JAVASCRIPT);
	ASSERT_TRUE(state != NULL);
	ASSERT_TRUE(editorSyntaxStateParseFull(state, &source_view));

	int parse_budget = 0;
	int query_budget = 0;
	(void)editorSyntaxStateConsumeBudgetEvents(state, &parse_budget, &query_budget);

	struct editorSyntaxCapture captures[1024];
	int capture_count = 0;
	ASSERT_TRUE(editorSyntaxStateCollectCapturesForRange(state, &source_view, 0,
				(uint32_t)source_len, captures,
				(int)(sizeof(captures) / sizeof(captures[0])), &capture_count));
	ASSERT_TRUE(editorSyntaxStateConsumeBudgetEvents(state, &parse_budget, &query_budget));
	ASSERT_EQ_INT(0, parse_budget);
	ASSERT_EQ_INT(1, query_budget);

	editorSyntaxStateDestroy(state);
	editorSyntaxTestResetBudgetOverrides();
	free(source);
	return 0;
}

static int test_editor_syntax_parse_budget_is_graceful(void) {
	size_t source_len = 0;
	char *source = build_repeated_text("function item(){ return 1; }\n", 120000, &source_len);
	ASSERT_TRUE(source != NULL);
	ASSERT_TRUE(source_len <= UINT32_MAX);
	struct editorTextSource source_view = {0};
	editorTextSourceInitString(&source_view, source, source_len);

	editorSyntaxTestSetBudgetOverrides(1, 8192, 2000000000ULL, 1);
	struct editorSyntaxState *state = editorSyntaxStateCreate(EDITOR_SYNTAX_JAVASCRIPT);
	ASSERT_TRUE(state != NULL);
	ASSERT_TRUE(editorSyntaxStateParseFull(state, &source_view));

	int parse_budget = 0;
	int query_budget = 0;
	ASSERT_TRUE(editorSyntaxStateConsumeBudgetEvents(state, &parse_budget, &query_budget));
	ASSERT_EQ_INT(1, parse_budget);
	ASSERT_EQ_INT(0, query_budget);

	editorSyntaxStateDestroy(state);
	editorSyntaxTestResetBudgetOverrides();
	free(source);
	return 0;
}

static int test_editor_syntax_incremental_provider_parse_keeps_tree_valid(void) {
	const char *before = "int value = 1;\n";
	const char *after = "int xvalue = 1;\n";
	struct editorTextSource before_source = {0};
	struct editorTextSource after_source = {0};
	editorTextSourceInitString(&before_source, before, strlen(before));
	editorTextSourceInitString(&after_source, after, strlen(after));

	struct editorSyntaxState *state = editorSyntaxStateCreate(EDITOR_SYNTAX_C);
	ASSERT_TRUE(state != NULL);
	ASSERT_TRUE(editorSyntaxStateParseFull(state, &before_source));

	struct editorSyntaxEdit edit = {
		.start_byte = 4,
		.old_end_byte = 4,
		.new_end_byte = 5,
		.start_point = {.row = 0, .column = 4},
		.old_end_point = {.row = 0, .column = 4},
		.new_end_point = {.row = 0, .column = 5}
	};
	ASSERT_TRUE(editorSyntaxStateApplyEditAndParse(state, &edit, &after_source));
	ASSERT_TRUE(editorSyntaxStateHasTree(state));

	struct editorSyntaxCapture captures[32];
	int capture_count = 0;
	ASSERT_TRUE(editorSyntaxStateCollectCapturesForRange(state, &after_source, 0,
				(uint32_t)strlen(after), captures,
				(int)(sizeof(captures) / sizeof(captures[0])), &capture_count));
	ASSERT_TRUE(capture_count > 0);

	editorSyntaxStateDestroy(state);
	return 0;
}

static int test_editor_syntax_large_file_stays_enabled_in_degraded_mode(void) {
	size_t source_len = 0;
	char *source = build_repeated_text("int value = 1;\n", 600000, &source_len);
	ASSERT_TRUE(source != NULL);
	ASSERT_TRUE(source_len > (size_t)(8 * 1024 * 1024));
	struct editorTextSource source_view = {0};
	editorTextSourceInitString(&source_view, source, source_len);

	editorSyntaxTestSetBudgetOverrides(1, 8192, 0, 0);
	struct editorSyntaxState *state = editorSyntaxStateCreate(EDITOR_SYNTAX_C);
	ASSERT_TRUE(state != NULL);
	ASSERT_TRUE(editorSyntaxStateConfigureForSourceLength(state, source_len));
	ASSERT_TRUE(editorSyntaxStateParseFull(state, &source_view));
	ASSERT_TRUE(editorSyntaxStateHasTree(state));
	ASSERT_EQ_INT(EDITOR_SYNTAX_PERF_DEGRADED_INJECTIONS,
			editorSyntaxStatePerformanceMode(state));

	editorSyntaxStateDestroy(state);
	editorSyntaxTestResetBudgetOverrides();
	free(source);
	return 0;
}

static int test_editor_syntax_visible_cache_recomputes_only_changed_rows(void) {
	char path[] = "/tmp/rotide-test-syntax-visible-cache-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 2,
			"tests/syntax/supported/c/visible_cache.c"));

	editorOpen(path);
	E.window_rows = 6;
	E.window_cols = 100;
	E.rowoff = 0;
	E.coloff = 0;
	E.cy = 1;
	E.cx = 2;

	editorSyntaxTestResetVisibleRowRecomputeCount();
	ASSERT_TRUE(editorSyntaxPrepareVisibleRowSpans(E.rowoff, E.window_rows));
	int full_recompute = editorSyntaxTestVisibleRowRecomputeCount();
	ASSERT_TRUE(full_recompute > 0);

	editorSyntaxTestResetVisibleRowRecomputeCount();
	editorInsertChar('x');
	ASSERT_TRUE(editorSyntaxPrepareVisibleRowSpans(E.rowoff, E.window_rows));
	int incremental_recompute = editorSyntaxTestVisibleRowRecomputeCount();
	ASSERT_TRUE(incremental_recompute > 0);
	ASSERT_TRUE(incremental_recompute < full_recompute);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_undo_redo_preserves_tree(void) {
	char path[] = "/tmp/rotide-test-syntax-history-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 2,
			"tests/syntax/supported/c/history.c"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = E.rows[0].size;
	editorHistoryBeginEdit(EDITOR_EDIT_NEWLINE);
	int dirty_before = E.dirty;
	editorInsertNewline();
	editorHistoryCommitEdit(EDITOR_EDIT_NEWLINE, E.dirty != dirty_before);
	editorHistoryBreakGroup();

	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(1, editorUndo());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(1, editorRedo());
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_syntax_undo_redo_preserves_shell_tree(void) {
	char path[] = "/tmp/rotide-test-syntax-history-shell-XXXXXX.sh";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/bash/history.sh"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_SHELL, editorSyntaxLanguageActive());

	E.cy = 1;
	E.cx = E.rows[1].size;
	editorHistoryBeginEdit(EDITOR_EDIT_NEWLINE);
	int dirty_before = E.dirty;
	editorInsertNewline();
	editorHistoryCommitEdit(EDITOR_EDIT_NEWLINE, E.dirty != dirty_before);
	editorHistoryBreakGroup();

	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(1, editorUndo());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(1, editorRedo());
	ASSERT_TRUE(editorSyntaxTreeExists());

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_tabs_keep_independent_syntax_states(void) {
	ASSERT_TRUE(editorTabsInit());

	char c_path[] = "/tmp/rotide-test-syntax-tabs-c-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(c_path, 2,
			"tests/syntax/supported/c/activation.c"));

	char txt_path[] = "/tmp/rotide-test-syntax-tabs-txt-XXXXXX.txt";
	int txt_fd = mkstemps(txt_path, 4);
	ASSERT_TRUE(txt_fd != -1);
	const char *txt_source = "notes\n";
	ASSERT_TRUE(write_all(txt_fd, txt_source, strlen(txt_source)) == 0);
	ASSERT_TRUE(close(txt_fd) == 0);

	editorOpen(c_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());

	ASSERT_TRUE(editorTabOpenFileAsNew(txt_path));
	ASSERT_TRUE(!editorSyntaxEnabled());
	ASSERT_EQ_INT(EDITOR_SYNTAX_NONE, editorSyntaxLanguageActive());

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());

	ASSERT_TRUE(editorTabSwitchToIndex(1));
	ASSERT_TRUE(!editorSyntaxEnabled());
	ASSERT_EQ_INT(EDITOR_SYNTAX_NONE, editorSyntaxLanguageActive());

	ASSERT_TRUE(unlink(c_path) == 0);
	ASSERT_TRUE(unlink(txt_path) == 0);
	return 0;
}

static int test_editor_tabs_keep_shell_and_c_syntax_states(void) {
	ASSERT_TRUE(editorTabsInit());

	char sh_path[] = "/tmp/rotide-test-syntax-tabs-shell-XXXXXX.sh";
	ASSERT_TRUE(write_fixture_to_temp_path(sh_path, 3,
			"tests/syntax/supported/bash/tab.sh"));

	char c_path[] = "/tmp/rotide-test-syntax-tabs-c2-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(c_path, 2,
			"tests/syntax/supported/c/activation.c"));

	editorOpen(sh_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_EQ_INT(EDITOR_SYNTAX_SHELL, editorSyntaxLanguageActive());

	ASSERT_TRUE(editorTabOpenFileAsNew(c_path));
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_SHELL, editorSyntaxLanguageActive());

	ASSERT_TRUE(editorTabSwitchToIndex(1));
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());

	ASSERT_TRUE(unlink(sh_path) == 0);
	ASSERT_TRUE(unlink(c_path) == 0);
	return 0;
}

static int test_editor_tabs_keep_web_and_c_syntax_states(void) {
	ASSERT_TRUE(editorTabsInit());

	char html_path[] = "/tmp/rotide-test-syntax-tabs-html-XXXXXX.html";
	ASSERT_TRUE(write_fixture_to_temp_path(html_path, 5,
			"tests/syntax/supported/html/tab.html"));

	char c_path[] = "/tmp/rotide-test-syntax-tabs-c3-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(c_path, 2,
			"tests/syntax/supported/c/activation.c"));

	editorOpen(html_path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_EQ_INT(EDITOR_SYNTAX_HTML, editorSyntaxLanguageActive());

	ASSERT_TRUE(editorTabOpenFileAsNew(c_path));
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_HTML, editorSyntaxLanguageActive());

	ASSERT_TRUE(editorTabSwitchToIndex(1));
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());

	ASSERT_TRUE(unlink(html_path) == 0);
	ASSERT_TRUE(unlink(c_path) == 0);
	return 0;
}

static int test_editor_recovery_restore_rebuilds_c_syntax_tree(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorTabsInit());

	add_row("int recovered = 1;");
	E.dirty = 1;
	E.filename = strdup("recovered.c");
	ASSERT_TRUE(E.filename != NULL);

	editorRecoveryMaybeAutosaveOnActivity();
	ASSERT_TRUE(editorRecoveryHasSnapshot());

	ASSERT_TRUE(editorTabsInit());
	ASSERT_EQ_INT(0, E.numrows);

	ASSERT_TRUE(editorRecoveryRestoreSnapshot());
	ASSERT_EQ_STR("recovered.c", E.filename);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_recovery_restore_rebuilds_shell_syntax_tree(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorTabsInit());

	add_row("#!/usr/bin/env bash");
	add_row("echo restored");
	E.dirty = 1;
	E.filename = strdup("recovered.sh");
	ASSERT_TRUE(E.filename != NULL);

	editorRecoveryMaybeAutosaveOnActivity();
	ASSERT_TRUE(editorRecoveryHasSnapshot());

	ASSERT_TRUE(editorTabsInit());
	ASSERT_EQ_INT(0, E.numrows);

	ASSERT_TRUE(editorRecoveryRestoreSnapshot());
	ASSERT_EQ_STR("recovered.sh", E.filename);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());
	ASSERT_EQ_INT(EDITOR_SYNTAX_SHELL, editorSyntaxLanguageActive());

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_save_writes_file_and_clears_dirty(void) {
	char path[] = "/tmp/rotide-test-save-XXXXXX";
	int fd = mkstemp(path);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(close(fd) == 0);

	add_row("foo");
	add_row("bar");
	E.dirty = 42;
	E.filename = strdup(path);
	ASSERT_TRUE(E.filename != NULL);

	editorSave();

	size_t content_len = 0;
	char *contents = read_file_contents(path, &content_len);
	ASSERT_TRUE(contents != NULL);
	ASSERT_EQ_INT(8, content_len);
	ASSERT_MEM_EQ("foo\nbar\n", contents, content_len);
	ASSERT_EQ_INT(0, E.dirty);
	ASSERT_TRUE(strstr(E.statusmsg, "bytes written to disk") != NULL);

	free(contents);
	unlink(path);
	return 0;
}

static int test_editor_save_prompts_for_filename(void) {
	char path[] = "/tmp/rotide-test-save-prompt-XXXXXX";
	int fd = mkstemp(path);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(close(fd) == 0);
	ASSERT_TRUE(unlink(path) == 0);

	add_row("foo");
	add_row("bar");
	E.dirty = 1;

	char input[256];
	int written = snprintf(input, sizeof(input), "%s\r", path);
	ASSERT_TRUE(written > 0);
	ASSERT_TRUE((size_t)written < sizeof(input));

	int saved_stdin;
	int saved_stdout;
	ASSERT_TRUE(setup_stdin_bytes(input, (size_t)written, &saved_stdin) == 0);
	ASSERT_TRUE(redirect_stdout_to_devnull(&saved_stdout) == 0);

	editorSave();

	ASSERT_TRUE(restore_stdout(saved_stdout) == 0);
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);

	ASSERT_EQ_STR(path, E.filename);
	ASSERT_EQ_INT(0, E.dirty);

	size_t content_len = 0;
	char *contents = read_file_contents(path, &content_len);
	ASSERT_TRUE(contents != NULL);
	ASSERT_MEM_EQ("foo\nbar\n", contents, content_len);

	free(contents);
	unlink(path);
	return 0;
}

static int test_editor_save_aborts_when_prompt_cancelled(void) {
	add_row("foo");
	E.dirty = 5;
	ASSERT_TRUE(E.filename == NULL);

	char esc_input[] = "\x1b[x";
	int saved_stdin;
	int saved_stdout;
	ASSERT_TRUE(setup_stdin_bytes(esc_input, sizeof(esc_input) - 1, &saved_stdin) == 0);
	ASSERT_TRUE(redirect_stdout_to_devnull(&saved_stdout) == 0);

	editorSave();

	ASSERT_TRUE(restore_stdout(saved_stdout) == 0);
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);

	ASSERT_TRUE(E.filename == NULL);
	ASSERT_EQ_INT(5, E.dirty);
	ASSERT_EQ_STR("Save aborted", E.statusmsg);
	return 0;
}

static int test_editor_save_rejects_empty_filename_prompt(void) {
	add_row("foo");
	E.dirty = 5;
	ASSERT_TRUE(E.filename == NULL);

	char input[] = "\r\x1b[x";
	int saved_stdin;
	int saved_stdout;
	ASSERT_TRUE(setup_stdin_bytes(input, sizeof(input) - 1, &saved_stdin) == 0);
	ASSERT_TRUE(redirect_stdout_to_devnull(&saved_stdout) == 0);

	editorSave();

	ASSERT_TRUE(restore_stdout(saved_stdout) == 0);
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);

	ASSERT_TRUE(E.filename == NULL);
	ASSERT_EQ_INT(5, E.dirty);
	ASSERT_EQ_STR("Save aborted", E.statusmsg);
	return 0;
}

static int test_editor_save_truncates_existing_file_with_empty_buffer(void) {
	char path[] = "/tmp/rotide-test-save-empty-XXXXXX";
	int fd = mkstemp(path);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(write_all(fd, "stale-data", 10) == 0);
	ASSERT_TRUE(close(fd) == 0);

	E.filename = strdup(path);
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 1;

	editorSave();

	size_t content_len = 0;
	char *contents = read_file_contents(path, &content_len);
	ASSERT_TRUE(contents != NULL);
	ASSERT_EQ_INT(0, content_len);
	ASSERT_EQ_INT(0, E.dirty);

	free(contents);
	unlink(path);
	return 0;
}

static int test_editor_save_preserves_existing_file_mode(void) {
	char path[] = "/tmp/rotide-test-save-mode-XXXXXX";
	int fd = mkstemp(path);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(close(fd) == 0);
	ASSERT_TRUE(chmod(path, 0600) == 0);

	add_row("content");
	E.filename = strdup(path);
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 1;

	editorSave();

	struct stat st;
	ASSERT_TRUE(stat(path, &st) == 0);
	ASSERT_EQ_INT(0600, st.st_mode & 0777);

	char *unlink_path = strdup(path);
	ASSERT_TRUE(unlink_path != NULL);
	unlink(unlink_path);
	free(unlink_path);
	return 0;
}

static int test_editor_save_removes_temp_file_on_success(void) {
	char path[] = "/tmp/rotide-test-save-atomic-success-XXXXXX";
	int fd = mkstemp(path);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(close(fd) == 0);

	ASSERT_EQ_INT(0, count_tmp_save_artifacts(path));

	add_row("alpha");
	add_row("beta");
	E.dirty = 1;
	E.filename = strdup(path);
	ASSERT_TRUE(E.filename != NULL);

	editorSave();

	ASSERT_EQ_INT(0, E.dirty);
	ASSERT_EQ_INT(0, count_tmp_save_artifacts(path));

	unlink(path);
	return 0;
}

static int test_editor_save_failure_cleans_temp_file(void) {
	char dir_template[] = "/tmp/rotide-test-save-target-dir-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	ASSERT_EQ_INT(0, count_tmp_save_artifacts(dir_path));

	add_row("alpha");
	E.dirty = 1;
	E.filename = strdup(dir_path);
	ASSERT_TRUE(E.filename != NULL);

	editorSave();

	ASSERT_EQ_INT(1, E.dirty);
	ASSERT_TRUE(strstr(E.statusmsg, "Save failed:") != NULL);
	ASSERT_EQ_INT(0, count_tmp_save_artifacts(dir_path));

	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_save_temp_fsync_failure_cleans_temp_file(void) {
	char path[] = "/tmp/rotide-test-save-temp-fsync-fail-XXXXXX";
	int fd = mkstemp(path);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(close(fd) == 0);

	ASSERT_EQ_INT(0, count_tmp_save_artifacts(path));

	add_row("alpha");
	E.dirty = 1;
	E.filename = strdup(path);
	ASSERT_TRUE(E.filename != NULL);

	editorTestSaveSyscallsFailFsyncOnCall(1);
	editorSave();

	ASSERT_EQ_INT(1, E.dirty);
	ASSERT_TRUE(strstr(E.statusmsg, "Save failed:") != NULL);
	ASSERT_EQ_INT(0, count_tmp_save_artifacts(path));

	size_t content_len = 0;
	char *contents = read_file_contents(path, &content_len);
	ASSERT_TRUE(contents != NULL);
	ASSERT_EQ_INT(0, content_len);

	free(contents);
	unlink(path);
	return 0;
}

static int test_editor_save_temp_close_failure_cleans_temp_file(void) {
	char path[] = "/tmp/rotide-test-save-temp-close-fail-XXXXXX";
	int fd = mkstemp(path);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(close(fd) == 0);

	ASSERT_EQ_INT(0, count_tmp_save_artifacts(path));

	add_row("alpha");
	E.dirty = 1;
	E.filename = strdup(path);
	ASSERT_TRUE(E.filename != NULL);

	editorTestSaveSyscallsFailCloseOnCall(1);
	editorSave();

	ASSERT_EQ_INT(1, E.dirty);
	ASSERT_TRUE(strstr(E.statusmsg, "Save failed:") != NULL);
	ASSERT_EQ_INT(0, count_tmp_save_artifacts(path));

	size_t content_len = 0;
	char *contents = read_file_contents(path, &content_len);
	ASSERT_TRUE(contents != NULL);
	ASSERT_EQ_INT(0, content_len);

	free(contents);
	unlink(path);
	return 0;
}

static int test_editor_save_rename_failure_cleans_temp_file_deterministic(void) {
	char path[] = "/tmp/rotide-test-save-rename-fail-XXXXXX";
	int fd = mkstemp(path);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(close(fd) == 0);

	ASSERT_EQ_INT(0, count_tmp_save_artifacts(path));

	add_row("alpha");
	E.dirty = 1;
	E.filename = strdup(path);
	ASSERT_TRUE(E.filename != NULL);

	editorTestSaveSyscallsFailRenameOnCall(1);
	editorSave();

	ASSERT_EQ_INT(1, E.dirty);
	ASSERT_TRUE(strstr(E.statusmsg, "Save failed: system error") != NULL);
	ASSERT_EQ_INT(0, count_tmp_save_artifacts(path));

	size_t content_len = 0;
	char *contents = read_file_contents(path, &content_len);
	ASSERT_TRUE(contents != NULL);
	ASSERT_EQ_INT(0, content_len);

	free(contents);
	unlink(path);
	return 0;
}

static int test_editor_save_rename_failure_reports_permission_denied_class(void) {
	char path[] = "/tmp/rotide-test-save-rename-eacces-XXXXXX";
	int fd = mkstemp(path);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(close(fd) == 0);

	ASSERT_EQ_INT(0, count_tmp_save_artifacts(path));

	add_row("alpha");
	E.dirty = 1;
	E.filename = strdup(path);
	ASSERT_TRUE(E.filename != NULL);

	editorTestSaveSyscallsInstallNoFail();
	editorTestSaveSyscallsFailRenameOnCallWithErrno(1, EACCES);
	editorSave();

	ASSERT_EQ_INT(1, E.dirty);
	ASSERT_TRUE(strstr(E.statusmsg, "Save failed: permission denied") != NULL);
	ASSERT_EQ_INT(0, count_tmp_save_artifacts(path));

	size_t content_len = 0;
	char *contents = read_file_contents(path, &content_len);
	ASSERT_TRUE(contents != NULL);
	ASSERT_EQ_INT(0, content_len);

	free(contents);
	unlink(path);
	return 0;
}

static int test_editor_save_rename_failure_reports_missing_path_class(void) {
	char path[] = "/tmp/rotide-test-save-rename-enoent-XXXXXX";
	int fd = mkstemp(path);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(close(fd) == 0);

	ASSERT_EQ_INT(0, count_tmp_save_artifacts(path));

	add_row("alpha");
	E.dirty = 1;
	E.filename = strdup(path);
	ASSERT_TRUE(E.filename != NULL);

	editorTestSaveSyscallsInstallNoFail();
	editorTestSaveSyscallsFailRenameOnCallWithErrno(1, ENOENT);
	editorSave();

	ASSERT_EQ_INT(1, E.dirty);
	ASSERT_TRUE(strstr(E.statusmsg, "Save failed: missing path") != NULL);
	ASSERT_EQ_INT(0, count_tmp_save_artifacts(path));

	size_t content_len = 0;
	char *contents = read_file_contents(path, &content_len);
	ASSERT_TRUE(contents != NULL);
	ASSERT_EQ_INT(0, content_len);

	free(contents);
	unlink(path);
	return 0;
}

static int test_editor_save_rename_failure_reports_read_only_fs_class(void) {
	char path[] = "/tmp/rotide-test-save-rename-erofs-XXXXXX";
	int fd = mkstemp(path);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(close(fd) == 0);

	ASSERT_EQ_INT(0, count_tmp_save_artifacts(path));

	add_row("alpha");
	E.dirty = 1;
	E.filename = strdup(path);
	ASSERT_TRUE(E.filename != NULL);

	editorTestSaveSyscallsInstallNoFail();
	editorTestSaveSyscallsFailRenameOnCallWithErrno(1, EROFS);
	editorSave();

	ASSERT_EQ_INT(1, E.dirty);
	ASSERT_TRUE(strstr(E.statusmsg, "Save failed: read-only filesystem") != NULL);
	ASSERT_EQ_INT(0, count_tmp_save_artifacts(path));

	size_t content_len = 0;
	char *contents = read_file_contents(path, &content_len);
	ASSERT_TRUE(contents != NULL);
	ASSERT_EQ_INT(0, content_len);

	free(contents);
	unlink(path);
	return 0;
}

static int test_editor_save_reports_temp_cleanup_failure(void) {
	char dir_template[] = "/tmp/rotide-test-save-cleanup-fail-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	ASSERT_EQ_INT(0, count_tmp_save_artifacts(dir_path));

	add_row("alpha");
	E.dirty = 1;
	E.filename = strdup(dir_path);
	ASSERT_TRUE(E.filename != NULL);

	editorTestSaveSyscallsInstallNoFail();
	editorTestSaveSyscallsFailRenameOnCallWithErrno(1, EACCES);
	editorTestSaveSyscallsFailUnlinkOnCallWithErrno(1, EIO);
	editorSave();

	ASSERT_EQ_INT(1, E.dirty);
	ASSERT_TRUE(strstr(E.statusmsg, "Save failed: permission denied") != NULL);
	ASSERT_TRUE(strstr(E.statusmsg, "cleanup failed (") != NULL);
	ASSERT_TRUE(count_tmp_save_artifacts(dir_path) > 0);

	ASSERT_EQ_INT(0, remove_tmp_save_artifacts(dir_path));
	ASSERT_EQ_INT(0, count_tmp_save_artifacts(dir_path));
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_prompt_accept_and_cancel(void) {
	char ok_input[] = "name\r";
	char esc_input[] = "\x1b[x";

	char *answer = editor_prompt_with_input(ok_input, sizeof(ok_input) - 1, "Name: %s");
	ASSERT_TRUE(answer != NULL);
	ASSERT_EQ_STR("name", answer);
	free(answer);

	answer = editor_prompt_with_input(esc_input, sizeof(esc_input) - 1, "Name: %s");
	ASSERT_TRUE(answer == NULL);
	ASSERT_EQ_STR("", E.statusmsg);
	return 0;
}

static int test_editor_prompt_accepts_utf8_input(void) {
	const char input[] = "t\xC3\xB6st.txt\r";
	char *answer = editor_prompt_with_input(input, sizeof(input) - 1, "Name: %s");
	ASSERT_TRUE(answer != NULL);
	ASSERT_EQ_STR("t\xC3\xB6st.txt", answer);
	free(answer);
	return 0;
}

static int test_editor_prompt_filters_ascii_controls_but_keeps_non_ascii_bytes(void) {
	const unsigned char input[] = {'a', 'b', 0x01, 0x02, 0xC3, 0xB6, 0x80, '\r'};
	const unsigned char expected[] = {'a', 'b', 0xC3, 0xB6, 0x80, '\0'};

	char *answer = editor_prompt_with_input((const char *)input, sizeof(input), "Name: %s");
	ASSERT_TRUE(answer != NULL);
	ASSERT_MEM_EQ(expected, answer, sizeof(expected));
	free(answer);
	return 0;
}

static int test_editor_prompt_backspace_removes_entire_utf8_codepoint(void) {
	const unsigned char input[] = {'a', 0xC3, 0xB6, BACKSPACE, '\r'};

	char *answer = editor_prompt_with_input((const char *)input, sizeof(input), "Name: %s");
	ASSERT_TRUE(answer != NULL);
	ASSERT_EQ_STR("a", answer);
	free(answer);
	return 0;
}

static int test_editor_prompt_delete_key_removes_entire_utf8_codepoint(void) {
	const unsigned char input[] = {'a', 0xC3, 0xB6, 0x1b, '[', '3', '~', '\r'};

	char *answer = editor_prompt_with_input((const char *)input, sizeof(input), "Name: %s");
	ASSERT_TRUE(answer != NULL);
	ASSERT_EQ_STR("a", answer);
	free(answer);
	return 0;
}

static int test_editor_prompt_fails_on_initial_alloc(void) {
	char *answer;
	char input[] = "name\r";

	editorTestAllocFailAfter(0);
	answer = editor_prompt_with_input(input, sizeof(input) - 1, "Name: %s");
	ASSERT_TRUE(answer == NULL);
	ASSERT_EQ_STR("Out of memory", E.statusmsg);
	return 0;
}

static int test_editor_prompt_fails_on_growth_alloc(void) {
	char *answer;
	char input[129];

	memset(input, 'a', sizeof(input) - 1);
	input[sizeof(input) - 1] = '\r';

	editorTestAllocFailAfter(1);
	answer = editor_prompt_with_input(input, sizeof(input), "Name: %s");
	ASSERT_TRUE(answer == NULL);
	ASSERT_EQ_STR("Out of memory", E.statusmsg);
	return 0;
}

static int test_editor_insert_row_render_alloc_failure_preserves_state(void) {
	add_row("abc");
	E.dirty = 0;
	E.cy = 0;
	E.cx = 0;

	editorTestAllocFailAfter(0);
	ASSERT_EQ_INT(0, editorInsertText("xyz\n", 4));

	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_INT(3, E.rows[0].size);
	ASSERT_EQ_STR("abc", E.rows[0].chars);
	ASSERT_EQ_STR("abc", E.rows[0].render);
	ASSERT_EQ_INT(0, E.dirty);
	ASSERT_EQ_STR("Out of memory", E.statusmsg);
	return 0;
}

static int test_editor_insert_char_render_alloc_failure_preserves_state(void) {
	add_row("ab");
	E.dirty = 0;
	E.cy = 0;
	E.cx = 1;

	editorTestAllocFailAfter(0);
	editorInsertChar('X');

	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_INT(2, E.rows[0].size);
	ASSERT_EQ_STR("ab", E.rows[0].chars);
	ASSERT_EQ_STR("ab", E.rows[0].render);
	ASSERT_EQ_INT(0, E.dirty);
	ASSERT_EQ_STR("Out of memory", E.statusmsg);
	return 0;
}

static int test_editor_insert_char_uses_document_when_row_cache_corrupt(void) {
	add_row("ab");
	E.rows[0].size = INT_MAX;
	E.cy = 0;
	E.cx = 0;
	E.dirty = 0;

	editorInsertChar('X');

	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("Xab", E.rows[0].chars);
	ASSERT_EQ_INT(1, E.dirty);
	return 0;
}

static int test_editor_insert_row_rejects_size_overflow(void) {
	char c = 'x';
	E.dirty = 0;

	ASSERT_EQ_INT(0, editorInsertText(&c, (size_t)INT_MAX + 1));

	ASSERT_EQ_INT(0, E.numrows);
	ASSERT_EQ_INT(0, E.dirty);
	ASSERT_EQ_STR("Operation too large", E.statusmsg);
	return 0;
}

static int test_editor_del_char_merge_alloc_failure_preserves_state(void) {
	add_row("abc");
	add_row("def");
	E.cy = 1;
	E.cx = 0;
	E.dirty = 0;

	editorTestAllocFailAfter(1);
	editorDelChar();

	ASSERT_EQ_INT(2, E.numrows);
	ASSERT_EQ_STR("abc", E.rows[0].chars);
	ASSERT_EQ_STR("def", E.rows[1].chars);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(0, E.cx);
	ASSERT_EQ_INT(0, E.dirty);
	ASSERT_EQ_STR("Out of memory", E.statusmsg);
	return 0;
}

static int test_editor_insert_newline_alloc_failure_preserves_state(void) {
	add_row("hello");
	E.cy = 0;
	E.cx = 2;
	E.dirty = 0;

	editorTestAllocFailAfter(3);
	editorInsertNewline();

	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("hello", E.rows[0].chars);
	ASSERT_EQ_STR("hello", E.rows[0].render);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_EQ_INT(0, E.dirty);
	ASSERT_EQ_STR("Out of memory", E.statusmsg);
	return 0;
}

static int test_editor_save_preserves_prompt_oom_status(void) {
	add_row("foo");
	E.dirty = 5;
	ASSERT_TRUE(E.filename == NULL);

	editorTestAllocFailAfter(0);
	editorSave();

	ASSERT_TRUE(E.filename == NULL);
	ASSERT_EQ_INT(5, E.dirty);
	ASSERT_EQ_STR("Out of memory", E.statusmsg);
	return 0;
}

static int test_editor_save_rows_to_str_alloc_failure_preserves_state(void) {
	char path[] = "/tmp/rotide-test-save-oom-rows-XXXXXX";
	int fd = mkstemp(path);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(close(fd) == 0);

	add_row("alpha");
	E.filename = strdup(path);
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 1;

	editorTestAllocFailAfter(0);
	editorSave();

	ASSERT_EQ_INT(1, E.dirty);
	ASSERT_EQ_STR("Out of memory", E.statusmsg);

	size_t content_len = 0;
	char *contents = read_file_contents(path, &content_len);
	ASSERT_TRUE(contents != NULL);
	ASSERT_EQ_INT(0, content_len);

	free(contents);
	unlink(path);
	return 0;
}

static int test_editor_save_uses_document_when_row_cache_corrupt(void) {
	char path[] = "/tmp/rotide-test-save-too-large-XXXXXX";
	int fd = mkstemp(path);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(close(fd) == 0);

	add_row("alpha");
	E.rows[0].size = INT_MAX;
	E.filename = strdup(path);
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 1;

	editorSave();

	ASSERT_EQ_INT(0, E.dirty);
	ASSERT_TRUE(strstr(E.statusmsg, "bytes written to disk") != NULL);

	size_t content_len = 0;
	char *contents = read_file_contents(path, &content_len);
	ASSERT_TRUE(contents != NULL);
	ASSERT_EQ_INT(6, content_len);
	ASSERT_MEM_EQ("alpha\n", contents, content_len);

	free(contents);
	unlink(path);
	return 0;
}

static int test_editor_save_tmp_path_alloc_failure_preserves_state(void) {
	char path[] = "/tmp/rotide-test-save-oom-path-XXXXXX";
	int fd = mkstemp(path);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(close(fd) == 0);

	add_row("alpha");
	E.filename = strdup(path);
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 1;

	editorTestAllocFailAfter(1);
	editorSave();

	ASSERT_EQ_INT(1, E.dirty);
	ASSERT_EQ_STR("Out of memory", E.statusmsg);

	size_t content_len = 0;
	char *contents = read_file_contents(path, &content_len);
	ASSERT_TRUE(contents != NULL);
	ASSERT_EQ_INT(0, content_len);

	free(contents);
	unlink(path);
	return 0;
}

static int test_editor_save_parent_dir_fsync_failure_after_rename_reports_failure(void) {
	char path[] = "/tmp/rotide-test-save-dir-fsync-fail-XXXXXX";
	int fd = mkstemp(path);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(close(fd) == 0);

	add_row("alpha");
	E.filename = strdup(path);
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 1;
	ASSERT_EQ_INT(0, count_tmp_save_artifacts(path));

	editorTestSaveSyscallsFailFsyncOnCall(2);
	editorSave();

	ASSERT_EQ_INT(1, E.dirty);
	ASSERT_TRUE(strstr(E.statusmsg, "Save failed:") != NULL);

	size_t content_len = 0;
	char *contents = read_file_contents(path, &content_len);
	ASSERT_TRUE(contents != NULL);
	ASSERT_EQ_INT(6, content_len);
	ASSERT_MEM_EQ("alpha\n", contents, content_len);
	ASSERT_EQ_INT(0, count_tmp_save_artifacts(path));

	free(contents);
	unlink(path);
	return 0;
}

static int test_editor_save_parent_dir_open_failure_reports_failure(void) {
	char path[] = "/tmp/rotide-test-save-dir-open-fail-XXXXXX";
	int fd = mkstemp(path);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(close(fd) == 0);

	add_row("alpha");
	E.filename = strdup(path);
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 1;
	ASSERT_EQ_INT(0, count_tmp_save_artifacts(path));

	editorTestSaveSyscallsFailOpenDirOnCall(1);
	editorSave();

	ASSERT_EQ_INT(1, E.dirty);
	ASSERT_TRUE(strstr(E.statusmsg, "Save failed:") != NULL);

	size_t content_len = 0;
	char *contents = read_file_contents(path, &content_len);
	ASSERT_TRUE(contents != NULL);
	ASSERT_EQ_INT(6, content_len);
	ASSERT_MEM_EQ("alpha\n", contents, content_len);
	ASSERT_EQ_INT(0, count_tmp_save_artifacts(path));

	free(contents);
	unlink(path);
	return 0;
}

static int test_editor_save_parent_dir_close_failure_reports_failure(void) {
	char path[] = "/tmp/rotide-test-save-dir-close-fail-XXXXXX";
	int fd = mkstemp(path);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(close(fd) == 0);

	add_row("alpha");
	E.filename = strdup(path);
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 1;
	ASSERT_EQ_INT(0, count_tmp_save_artifacts(path));

	editorTestSaveSyscallsFailCloseOnCall(2);
	editorSave();

	ASSERT_EQ_INT(1, E.dirty);
	ASSERT_TRUE(strstr(E.statusmsg, "Save failed:") != NULL);

	size_t content_len = 0;
	char *contents = read_file_contents(path, &content_len);
	ASSERT_TRUE(contents != NULL);
	ASSERT_EQ_INT(6, content_len);
	ASSERT_MEM_EQ("alpha\n", contents, content_len);
	ASSERT_EQ_INT(0, count_tmp_save_artifacts(path));

	free(contents);
	unlink(path);
	return 0;
}

static int test_editor_save_parent_dir_fsync_success_clears_dirty(void) {
	char path[] = "/tmp/rotide-test-save-dir-fsync-success-XXXXXX";
	int fd = mkstemp(path);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(close(fd) == 0);

	add_row("alpha");
	E.filename = strdup(path);
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 1;

	editorTestSaveSyscallsInstallNoFail();
	editorSave();

	ASSERT_EQ_INT(0, E.dirty);
	ASSERT_TRUE(strstr(E.statusmsg, "bytes written to disk") != NULL);

	size_t content_len = 0;
	char *contents = read_file_contents(path, &content_len);
	ASSERT_TRUE(contents != NULL);
	ASSERT_EQ_INT(6, content_len);
	ASSERT_MEM_EQ("alpha\n", contents, content_len);

	free(contents);
	unlink(path);
	return 0;
}

static int test_editor_recovery_autosave_activity_debounce(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorTabsInit());

	add_row("a");
	E.cy = 0;
	E.cx = 1;
	E.dirty = 1;
	E.recovery_last_autosave_time = 0;
	editorRecoveryMaybeAutosaveOnActivity();

	const char *recovery_path = editorRecoveryPath();
	ASSERT_TRUE(recovery_path != NULL);
	struct stat first_stat;
	ASSERT_TRUE(stat(recovery_path, &first_stat) == 0);

	editorInsertChar('b');
	E.recovery_last_autosave_time = time(NULL);
	editorRecoveryMaybeAutosaveOnActivity();
	struct stat second_stat;
	ASSERT_TRUE(stat(recovery_path, &second_stat) == 0);
	ASSERT_EQ_INT(first_stat.st_size, second_stat.st_size);

	E.recovery_last_autosave_time = time(NULL) - 5;
	editorRecoveryMaybeAutosaveOnActivity();
	struct stat third_stat;
	ASSERT_TRUE(stat(recovery_path, &third_stat) == 0);
	ASSERT_TRUE(third_stat.st_size > second_stat.st_size);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_recovery_cleans_snapshot_when_all_tabs_clean(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorTabsInit());

	add_row("dirty");
	E.dirty = 1;
	E.recovery_last_autosave_time = 0;
	editorRecoveryMaybeAutosaveOnActivity();

	const char *recovery_path = editorRecoveryPath();
	ASSERT_TRUE(recovery_path != NULL);
	ASSERT_TRUE(access(recovery_path, F_OK) == 0);

	E.dirty = 0;
	editorRecoveryMaybeAutosaveOnActivity();
	ASSERT_TRUE(access(recovery_path, F_OK) == -1);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_recovery_roundtrip_restores_tabs_and_cursor_state(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorTabsInit());

	add_row("alpha");
	E.filename = strdup("one.c");
	ASSERT_TRUE(E.filename != NULL);
	E.cy = 0;
	E.cx = 3;
	E.rowoff = 0;
	E.coloff = 2;
	E.dirty = 1;

	ASSERT_TRUE(editorTabNewEmpty());
	add_row("beta");
	add_row("gamma");
	E.filename = strdup("two.c");
	ASSERT_TRUE(E.filename != NULL);
	E.cy = 1;
	E.cx = 2;
	E.rowoff = 1;
	E.coloff = 4;
	E.dirty = 1;

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	E.recovery_last_autosave_time = 0;
	editorRecoveryMaybeAutosaveOnActivity();

	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorRecoveryRestoreSnapshot());

	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(0, editorTabActiveIndex());
	ASSERT_TRUE(editorTabDirtyAt(0));
	ASSERT_TRUE(editorTabDirtyAt(1));
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR("one.c", E.filename);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("alpha", E.rows[0].chars);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(3, E.cx);
	ASSERT_EQ_INT(0, E.rowoff);
	ASSERT_EQ_INT(2, E.coloff);

	ASSERT_TRUE(editorTabSwitchToIndex(1));
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR("two.c", E.filename);
	ASSERT_EQ_INT(2, E.numrows);
	ASSERT_EQ_STR("beta", E.rows[0].chars);
	ASSERT_EQ_STR("gamma", E.rows[1].chars);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_EQ_INT(1, E.rowoff);
	ASSERT_EQ_INT(4, E.coloff);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_recovery_rejects_unsupported_snapshot_version(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorTabsInit());

	const char *recovery_path = editorRecoveryPath();
	ASSERT_TRUE(recovery_path != NULL);
	int fd = open(recovery_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	ASSERT_TRUE(fd != -1);
	const char header[] = "RTRECOV1";
	ASSERT_TRUE(write_all(fd, header, sizeof(header) - 1) == 0);
	unsigned char words[12] = {
		1, 0, 0, 0, // Unsupported version
		1, 0, 0, 0, // tab_count
		0, 0, 0, 0  // active_tab
	};
	ASSERT_TRUE(write_all(fd, (const char *)words, sizeof(words)) == 0);
	ASSERT_TRUE(close(fd) == 0);

	ASSERT_EQ_INT(0, editorRecoveryRestoreSnapshot());
	ASSERT_EQ_STR("Recovery data was invalid and was discarded", E.statusmsg);
	ASSERT_TRUE(access(recovery_path, F_OK) == -1);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_startup_restore_choice_ignores_cli_args(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorTabsInit());

	add_row("recovered");
	E.filename = strdup("recovered.txt");
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 1;
	E.recovery_last_autosave_time = 0;
	editorRecoveryMaybeAutosaveOnActivity();

	char cli_path[512];
	ASSERT_TRUE(path_join(cli_path, sizeof(cli_path), env.project_dir, "cli.txt"));
	ASSERT_TRUE(write_text_file(cli_path, "cli-line\n"));

	ASSERT_TRUE(editorTabsInit());

	int saved_stdin;
	int saved_stdout;
	const char input[] = {'y'};
	ASSERT_TRUE(setup_stdin_bytes(input, sizeof(input), &saved_stdin) == 0);
	ASSERT_TRUE(redirect_stdout_to_devnull(&saved_stdout) == 0);
	char *argv[] = {"rotide", cli_path, NULL};
	int restored = editorStartupLoadRecoveryOrOpenArgs(2, argv);
	ASSERT_TRUE(restore_stdout(saved_stdout) == 0);
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);

	ASSERT_EQ_INT(1, restored);
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR("recovered.txt", E.filename);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("recovered", E.rows[0].chars);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_startup_discard_choice_opens_cli_args(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorTabsInit());

	add_row("recovered");
	E.filename = strdup("recovered.txt");
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 1;
	E.recovery_last_autosave_time = 0;
	editorRecoveryMaybeAutosaveOnActivity();

	const char *recovery_path = editorRecoveryPath();
	ASSERT_TRUE(recovery_path != NULL);
	ASSERT_TRUE(access(recovery_path, F_OK) == 0);

	char cli_path[512];
	ASSERT_TRUE(path_join(cli_path, sizeof(cli_path), env.project_dir, "cli.txt"));
	ASSERT_TRUE(write_text_file(cli_path, "cli-line\n"));

	ASSERT_TRUE(editorTabsInit());

	int saved_stdin;
	int saved_stdout;
	const char input[] = {'n'};
	ASSERT_TRUE(setup_stdin_bytes(input, sizeof(input), &saved_stdin) == 0);
	ASSERT_TRUE(redirect_stdout_to_devnull(&saved_stdout) == 0);
	char *argv[] = {"rotide", cli_path, NULL};
	int restored = editorStartupLoadRecoveryOrOpenArgs(2, argv);
	ASSERT_TRUE(restore_stdout(saved_stdout) == 0);
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);

	ASSERT_EQ_INT(0, restored);
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR(cli_path, E.filename);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("cli-line", E.rows[0].chars);
	ASSERT_TRUE(access(recovery_path, F_OK) == -1);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_startup_invalid_recovery_discards_and_opens_cli_args(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	const char *recovery_path = editorRecoveryPath();
	ASSERT_TRUE(recovery_path != NULL);
	ASSERT_TRUE(write_text_file(recovery_path, "bad-recovery-data"));

	char cli_path[512];
	ASSERT_TRUE(path_join(cli_path, sizeof(cli_path), env.project_dir, "cli.txt"));
	ASSERT_TRUE(write_text_file(cli_path, "cli-line\n"));

	ASSERT_TRUE(editorTabsInit());

	int saved_stdin;
	int saved_stdout;
	const char input[] = {'y'};
	ASSERT_TRUE(setup_stdin_bytes(input, sizeof(input), &saved_stdin) == 0);
	ASSERT_TRUE(redirect_stdout_to_devnull(&saved_stdout) == 0);
	char *argv[] = {"rotide", cli_path, NULL};
	int restored = editorStartupLoadRecoveryOrOpenArgs(2, argv);
	ASSERT_TRUE(restore_stdout(saved_stdout) == 0);
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);

	ASSERT_EQ_INT(0, restored);
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR(cli_path, E.filename);
	ASSERT_TRUE(strstr(E.statusmsg, "invalid") != NULL);
	ASSERT_TRUE(access(recovery_path, F_OK) == -1);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int find_drawer_entry(const char *name, int *idx_out, struct editorDrawerEntryView *view_out) {
	int visible = editorDrawerVisibleCount();
	for (int i = 0; i < visible; i++) {
		struct editorDrawerEntryView view;
		if (!editorDrawerGetVisibleEntry(i, &view)) {
			continue;
		}
		if (strcmp(view.name, name) == 0) {
			if (idx_out != NULL) {
				*idx_out = i;
			}
			if (view_out != NULL) {
				*view_out = view;
			}
			return 1;
		}
	}
	return 0;
}

static int test_editor_drawer_root_selection_modes(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	const char *root_path = editorDrawerRootPath();
	ASSERT_TRUE(root_path != NULL);
	ASSERT_EQ_STR(env.project_dir, root_path);

	char src_dir[512];
	char cli_file[512];
	ASSERT_TRUE(path_join(src_dir, sizeof(src_dir), env.project_dir, "src"));
	ASSERT_TRUE(make_dir(src_dir));
	ASSERT_TRUE(path_join(cli_file, sizeof(cli_file), src_dir, "main.c"));
	ASSERT_TRUE(write_text_file(cli_file, "int main(void) { return 0; }\n"));

	char *argv[] = {"rotide", cli_file, NULL};
	ASSERT_TRUE(editorDrawerInitForStartup(2, argv, 0));
	root_path = editorDrawerRootPath();
	ASSERT_TRUE(root_path != NULL);
	ASSERT_EQ_STR(src_dir, root_path);

	ASSERT_TRUE(editorDrawerInitForStartup(2, argv, 1));
	root_path = editorDrawerRootPath();
	ASSERT_TRUE(root_path != NULL);
	ASSERT_EQ_STR(env.project_dir, root_path);

	ASSERT_TRUE(unlink(cli_file) == 0);
	ASSERT_TRUE(rmdir(src_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_drawer_tree_lists_dotfiles_sorted_and_symlink_as_file(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char src_dir[512];
	char env_file[512];
	char ignore_file[512];
	char main_file[512];
	char link_path[512];
	ASSERT_TRUE(path_join(src_dir, sizeof(src_dir), env.project_dir, "src"));
	ASSERT_TRUE(path_join(env_file, sizeof(env_file), env.project_dir, ".env"));
	ASSERT_TRUE(path_join(ignore_file, sizeof(ignore_file), env.project_dir, ".gitignore"));
	ASSERT_TRUE(path_join(main_file, sizeof(main_file), env.project_dir, "main.c"));
	ASSERT_TRUE(path_join(link_path, sizeof(link_path), env.project_dir, "link_to_src"));

	ASSERT_TRUE(make_dir(src_dir));
	ASSERT_TRUE(write_text_file(env_file, "A=1\n"));
	ASSERT_TRUE(write_text_file(ignore_file, "*.o\n"));
	ASSERT_TRUE(write_text_file(main_file, "int x;\n"));
	ASSERT_TRUE(symlink("src", link_path) == 0);

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows));

	int src_idx = -1;
	int env_idx = -1;
	int ignore_idx = -1;
	int main_idx = -1;
	int link_idx = -1;
	struct editorDrawerEntryView link_view;
	ASSERT_TRUE(find_drawer_entry("src", &src_idx, NULL));
	ASSERT_TRUE(find_drawer_entry(".env", &env_idx, NULL));
	ASSERT_TRUE(find_drawer_entry(".gitignore", &ignore_idx, NULL));
	ASSERT_TRUE(find_drawer_entry("main.c", &main_idx, NULL));
	ASSERT_TRUE(find_drawer_entry("link_to_src", &link_idx, &link_view));

	ASSERT_TRUE(src_idx >= 0);
	ASSERT_TRUE(src_idx < env_idx);
	ASSERT_TRUE(env_idx < ignore_idx);
	ASSERT_TRUE(ignore_idx < link_idx);
	ASSERT_TRUE(link_idx < main_idx);
	ASSERT_EQ_INT(0, link_view.is_dir);

	ASSERT_TRUE(unlink(link_path) == 0);
	ASSERT_TRUE(unlink(main_file) == 0);
	ASSERT_TRUE(unlink(ignore_file) == 0);
	ASSERT_TRUE(unlink(env_file) == 0);
	ASSERT_TRUE(rmdir(src_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_drawer_expand_collapse_reuses_cached_children(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char src_dir[512];
	char child_file[512];
	ASSERT_TRUE(path_join(src_dir, sizeof(src_dir), env.project_dir, "src"));
	ASSERT_TRUE(path_join(child_file, sizeof(child_file), src_dir, "child.txt"));
	ASSERT_TRUE(make_dir(src_dir));
	ASSERT_TRUE(write_text_file(child_file, "child\n"));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows));

	int src_idx = -1;
	ASSERT_TRUE(find_drawer_entry("src", &src_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(src_idx, E.window_rows));
	ASSERT_EQ_INT(src_idx, E.drawer_selected_index);

	int collapsed_count = editorDrawerVisibleCount();
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows));
	int expanded_count = editorDrawerVisibleCount();
	ASSERT_TRUE(expanded_count > collapsed_count);
	ASSERT_TRUE(find_drawer_entry("child.txt", NULL, NULL));

	ASSERT_TRUE(editorDrawerCollapseSelection(E.window_rows));
	ASSERT_EQ_INT(collapsed_count, editorDrawerVisibleCount());
	ASSERT_EQ_INT(src_idx, E.drawer_selected_index);

	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows));
	ASSERT_EQ_INT(expanded_count, editorDrawerVisibleCount());
	ASSERT_EQ_INT(src_idx, E.drawer_selected_index);

	ASSERT_TRUE(unlink(child_file) == 0);
	ASSERT_TRUE(rmdir(src_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_drawer_root_is_not_collapsible(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char src_dir[512];
	ASSERT_TRUE(path_join(src_dir, sizeof(src_dir), env.project_dir, "src"));
	ASSERT_TRUE(make_dir(src_dir));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_EQ_INT(0, E.drawer_selected_index);

	int visible_before = editorDrawerVisibleCount();
	ASSERT_TRUE(visible_before >= 2);

	ASSERT_EQ_INT(0, editorDrawerCollapseSelection(E.window_rows));
	ASSERT_EQ_INT(visible_before, editorDrawerVisibleCount());
	ASSERT_EQ_INT(0, E.drawer_selected_index);

	ASSERT_EQ_INT(0, editorDrawerToggleSelectionExpanded(E.window_rows));
	ASSERT_EQ_INT(visible_before, editorDrawerVisibleCount());
	ASSERT_EQ_INT(0, E.drawer_selected_index);

	ASSERT_EQ_INT(1, editorDrawerExpandSelection(E.window_rows));
	ASSERT_EQ_INT(visible_before, editorDrawerVisibleCount());
	ASSERT_EQ_INT(0, E.drawer_selected_index);

	ASSERT_TRUE(rmdir(src_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_drawer_open_selected_file_in_new_tab(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char open_file[512];
	ASSERT_TRUE(path_join(open_file, sizeof(open_file), env.project_dir, "open.txt"));
	ASSERT_TRUE(write_text_file(open_file, "opened\n"));

	ASSERT_TRUE(editorTabsInit());
	add_row("keep");

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));

	int file_idx = -1;
	ASSERT_TRUE(find_drawer_entry("open.txt", &file_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(file_idx, E.window_rows + 1));
	ASSERT_TRUE(editorDrawerOpenSelectedFileInTab());

	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR(open_file, E.filename);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("opened", E.rows[0].chars);

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("keep", E.rows[0].chars);

	ASSERT_TRUE(unlink(open_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_drawer_open_selected_file_switches_existing_relative_path_tab(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char abs_file[512];
	ASSERT_TRUE(path_join(abs_file, sizeof(abs_file), env.project_dir, "dup.txt"));
	ASSERT_TRUE(write_text_file(abs_file, "dup\n"));

	ASSERT_TRUE(editorTabsInit());
	add_row("base");

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));
	int file_idx = -1;
	ASSERT_TRUE(find_drawer_entry("dup.txt", &file_idx, NULL));

	ASSERT_TRUE(editorTabOpenFileAsNew("dup.txt"));
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR("dup.txt", E.filename);

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(file_idx, E.window_rows + 1));
	ASSERT_TRUE(editorDrawerOpenSelectedFileInTab());

	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR("dup.txt", E.filename);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("dup", E.rows[0].chars);

	ASSERT_TRUE(unlink(abs_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_path_absolute_dup_makes_relative_paths_absolute(void) {
	int failed = 1;
	char *original_cwd = getcwd(NULL, 0);
	char root_template[] = "/tmp/rotide-test-abs-path-XXXXXX";
	char *root_path = mkdtemp(root_template);
	char nested_dir[512] = "";
	char nested_file[512] = "";
	char *absolute = NULL;

	if (original_cwd == NULL || root_path == NULL ||
			!path_join(nested_dir, sizeof(nested_dir), root_path, "nested") ||
			!make_dir(nested_dir) ||
			!path_join(nested_file, sizeof(nested_file), nested_dir, "file.c") ||
			!write_text_file(nested_file, "int main(void) { return 0; }\n") ||
			chdir(root_path) != 0) {
		goto cleanup;
	}

	absolute = editorPathAbsoluteDup("nested/file.c");
	if (absolute == NULL || strcmp(nested_file, absolute) != 0) {
		goto cleanup;
	}

	failed = 0;

cleanup:
	if (original_cwd != NULL) {
		if (chdir(original_cwd) != 0) {
			failed = 1;
		}
	}
	free(absolute);
	if (nested_file[0] != '\0') {
		(void)unlink(nested_file);
	}
	if (nested_dir[0] != '\0') {
		(void)rmdir(nested_dir);
	}
	if (root_path != NULL) {
		(void)rmdir(root_path);
	}
	free(original_cwd);
	return failed;
}

static int test_editor_path_find_marker_upward_returns_project_root(void) {
	char root_template[] = "/tmp/rotide-test-path-marker-XXXXXX";
	char *root_path = mkdtemp(root_template);
	ASSERT_TRUE(root_path != NULL);

	char src_dir[512];
	char nested_dir[512];
	char marker_path[512];
	ASSERT_TRUE(path_join(src_dir, sizeof(src_dir), root_path, "src"));
	ASSERT_TRUE(path_join(nested_dir, sizeof(nested_dir), src_dir, "inner"));
	ASSERT_TRUE(path_join(marker_path, sizeof(marker_path), root_path, "compile_commands.json"));
	ASSERT_TRUE(make_dir(src_dir));
	ASSERT_TRUE(make_dir(nested_dir));
	ASSERT_TRUE(write_text_file(marker_path, "[]\n"));

	static const char *const markers[] = {"compile_commands.json", ".git"};
	char *workspace_root = editorPathFindMarkerUpward(nested_dir, markers,
			sizeof(markers) / sizeof(markers[0]));
	ASSERT_TRUE(workspace_root != NULL);
	ASSERT_EQ_STR(root_path, workspace_root);

	free(workspace_root);
	ASSERT_TRUE(unlink(marker_path) == 0);
	ASSERT_TRUE(rmdir(nested_dir) == 0);
	ASSERT_TRUE(rmdir(src_dir) == 0);
	ASSERT_TRUE(rmdir(root_path) == 0);
	return 0;
}

static int test_editor_drawer_open_selected_file_respects_tab_limit(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char open_file[512];
	ASSERT_TRUE(path_join(open_file, sizeof(open_file), env.project_dir, "limit.txt"));
	ASSERT_TRUE(write_text_file(open_file, "limit\n"));

	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));

	int file_idx = -1;
	ASSERT_TRUE(find_drawer_entry("limit.txt", &file_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(file_idx, E.window_rows + 1));

	for (int i = 1; i < ROTIDE_MAX_TABS; i++) {
		ASSERT_TRUE(editorTabNewEmpty());
	}
	ASSERT_EQ_INT(ROTIDE_MAX_TABS, editorTabCount());
	add_row("stay");
	int active_before = editorTabActiveIndex();
	int numrows_before = E.numrows;

	ASSERT_TRUE(!editorDrawerOpenSelectedFileInTab());
	ASSERT_EQ_INT(ROTIDE_MAX_TABS, editorTabCount());
	ASSERT_EQ_INT(active_before, editorTabActiveIndex());
	ASSERT_EQ_INT(numrows_before, E.numrows);
	ASSERT_EQ_STR("stay", E.rows[0].chars);
	ASSERT_TRUE(strstr(E.statusmsg, "Tab limit reached") != NULL);

	ASSERT_TRUE(unlink(open_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_recovery_snapshot_permissions_are_0600(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorTabsInit());

	add_row("perm-check");
	E.dirty = 1;
	E.recovery_last_autosave_time = 0;
	editorRecoveryMaybeAutosaveOnActivity();

	const char *recovery_path = editorRecoveryPath();
	ASSERT_TRUE(recovery_path != NULL);
	struct stat st;
	ASSERT_TRUE(stat(recovery_path, &st) == 0);
	ASSERT_EQ_INT(0600, st.st_mode & 0777);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_recovery_clean_quit_removes_snapshot(void) {
	struct recoveryTestEnv env;
	const char *recovery_path = NULL;
	pid_t pid = -1;
	int status = 0;
	int result = 1;

	if (!setup_recovery_test_env(&env)) {
		fprintf(stderr, "Assertion failed in %s:%d: %s\n", __func__, __LINE__,
				"setup_recovery_test_env(&env)");
		goto cleanup;
	}
	if (!editorTabsInit()) {
		fprintf(stderr, "Assertion failed in %s:%d: %s\n", __func__, __LINE__,
				"editorTabsInit()");
		goto cleanup;
	}

	add_row("quit-cleanup");
	E.dirty = 1;
	E.recovery_last_autosave_time = 0;
	editorRecoveryMaybeAutosaveOnActivity();

	recovery_path = editorRecoveryPath();
	if (recovery_path == NULL) {
		fprintf(stderr, "Assertion failed in %s:%d: %s\n", __func__, __LINE__,
				"recovery_path != NULL");
		goto cleanup;
	}
	if (access(recovery_path, F_OK) != 0) {
		fprintf(stderr, "Assertion failed in %s:%d: %s\n", __func__, __LINE__,
				"access(recovery_path, F_OK) == 0");
		goto cleanup;
	}

	E.dirty = 0;
	pid = fork();
	if (pid == -1) {
		fprintf(stderr, "Assertion failed in %s:%d: %s\n", __func__, __LINE__, "pid != -1");
		goto cleanup;
	}
	if (pid == 0) {
		int saved_stdout;
		if (redirect_stdout_to_devnull(&saved_stdout) == -1) {
			_exit(151);
		}
		char ctrl_q[] = {CTRL_KEY('q')};
		if (editor_process_keypress_with_input(ctrl_q, sizeof(ctrl_q)) == -1) {
			_exit(152);
		}
		_exit(153);
	}

	if (wait_for_child_exit_with_timeout(pid, 1500, &status) != 0) {
		fprintf(stderr, "Assertion failed in %s:%d: %s\n", __func__, __LINE__,
				"wait_for_child_exit_with_timeout(pid, 1500, &status) == 0");
		goto cleanup;
	}
	pid = -1;
	if (!WIFEXITED(status)) {
		fprintf(stderr, "Assertion failed in %s:%d: %s\n", __func__, __LINE__, "WIFEXITED(status)");
		goto cleanup;
	}
	if (WEXITSTATUS(status) != EXIT_SUCCESS) {
		fprintf(stderr, "Assertion failed in %s:%d: expected %d, got %d\n", __func__, __LINE__,
				EXIT_SUCCESS, WEXITSTATUS(status));
		goto cleanup;
	}
	if (access(recovery_path, F_OK) != -1) {
		fprintf(stderr, "Assertion failed in %s:%d: %s\n", __func__, __LINE__,
				"access(recovery_path, F_OK) == -1");
		goto cleanup;
	}

	result = 0;

cleanup:
	if (pid > 0) {
		(void)kill(pid, SIGKILL);
		(void)waitpid(pid, &status, 0);
	}
	cleanup_recovery_test_env(&env);
	return result;
}

static int test_editor_recovery_failure_exit_keeps_snapshot(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorTabsInit());

	add_row("keep-on-failure");
	E.dirty = 1;
	E.recovery_last_autosave_time = 0;
	editorRecoveryMaybeAutosaveOnActivity();

	const char *recovery_path = editorRecoveryPath();
	ASSERT_TRUE(recovery_path != NULL);
	ASSERT_TRUE(access(recovery_path, F_OK) == 0);

	pid_t pid = fork();
	ASSERT_TRUE(pid != -1);
	if (pid == 0) {
		int saved_stdout;
		if (redirect_stdout_to_devnull(&saved_stdout) == -1) {
			_exit(161);
		}
		if (editor_process_keypress_with_input("", 0) == -1) {
			_exit(162);
		}
		_exit(163);
	}

	int status = 0;
	ASSERT_TRUE(wait_for_child_exit_with_timeout(pid, 1500, &status) == 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ_INT(EXIT_FAILURE, WEXITSTATUS(status));
	ASSERT_TRUE(access(recovery_path, F_OK) == 0);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_reports_oom_without_crash(void) {
	add_row("line");
	E.window_rows = 3;
	E.window_cols = 20;

	editorTestAllocFailAfter(0);
	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);

	ASSERT_TRUE(output != NULL);
	ASSERT_EQ_INT(0, output_len);
	ASSERT_EQ_STR("Out of memory", E.statusmsg);

	free(output);
	return 0;
}

static int test_editor_read_key_sequences(void) {
	int key = 0;
	char plain[] = "x";
	char up[] = "\x1b[A";
	char pgup[] = "\x1b[5~";
	char end_key[] = "\x1bOF";
	char plain_escape[] = "\x1b[x";

	ASSERT_TRUE(editor_read_key_with_input(plain, sizeof(plain) - 1, &key) == 0);
	ASSERT_EQ_INT('x', key);

	ASSERT_TRUE(editor_read_key_with_input(up, sizeof(up) - 1, &key) == 0);
	ASSERT_EQ_INT(ARROW_UP, key);

	ASSERT_TRUE(editor_read_key_with_input(pgup, sizeof(pgup) - 1, &key) == 0);
	ASSERT_EQ_INT(PAGE_UP, key);

	ASSERT_TRUE(editor_read_key_with_input(end_key, sizeof(end_key) - 1, &key) == 0);
	ASSERT_EQ_INT(END_KEY, key);

	ASSERT_TRUE(editor_read_key_with_input(plain_escape, sizeof(plain_escape) - 1, &key) == 0);
	ASSERT_EQ_INT('\x1b', key);
	return 0;
}

static int test_editor_read_key_alt_arrow_sequences(void) {
	int key = 0;
	const char csi_alt_left[] = "\x1b[1;3D";
	const char csi_alt_right[] = "\x1b[1;3C";
	const char csi_alt_down[] = "\x1b[1;3B";
	const char csi_alt_up[] = "\x1b[1;3A";
	const char csi_alt_shift_left[] = "\x1b[1;4D";
	const char csi_alt_shift_right[] = "\x1b[1;4C";
	const char csi_alt_shift_down[] = "\x1b[1;4B";
	const char csi_alt_shift_up[] = "\x1b[1;4A";
	const char csi_ctrl_left[] = "\x1b[1;5D";
	const char csi_ctrl_right[] = "\x1b[1;5C";
	const char csi_ctrl_down[] = "\x1b[1;5B";
	const char csi_ctrl_up[] = "\x1b[1;5A";
	const char csi_ctrl_alt_left[] = "\x1b[1;7D";
	const char csi_ctrl_alt_right[] = "\x1b[1;7C";
	const char csi_ctrl_alt_down[] = "\x1b[1;7B";
	const char csi_ctrl_alt_up[] = "\x1b[1;7A";
	const char fallback_alt_left[] = "\x1b\x1b[D";
	const char fallback_alt_right[] = "\x1b\x1b[C";
	const char fallback_alt_down[] = "\x1b\x1b[B";
	const char fallback_alt_up[] = "\x1b\x1b[A";
	const char alt_letter_lower[] = "\x1b" "a";
	const char alt_letter_upper[] = "\x1b" "A";
	const char ctrl_alt_letter[] = {'\x1b', CTRL_KEY('b')};

	ASSERT_TRUE(editor_read_key_with_input(csi_alt_left, sizeof(csi_alt_left) - 1, &key) == 0);
	ASSERT_EQ_INT(ALT_ARROW_LEFT, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_alt_right, sizeof(csi_alt_right) - 1, &key) == 0);
	ASSERT_EQ_INT(ALT_ARROW_RIGHT, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_alt_down, sizeof(csi_alt_down) - 1, &key) == 0);
	ASSERT_EQ_INT(ALT_ARROW_DOWN, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_alt_up, sizeof(csi_alt_up) - 1, &key) == 0);
	ASSERT_EQ_INT(ALT_ARROW_UP, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_alt_shift_left,
				sizeof(csi_alt_shift_left) - 1, &key) == 0);
	ASSERT_EQ_INT(ALT_SHIFT_ARROW_LEFT, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_alt_shift_right,
				sizeof(csi_alt_shift_right) - 1, &key) == 0);
	ASSERT_EQ_INT(ALT_SHIFT_ARROW_RIGHT, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_alt_shift_down,
				sizeof(csi_alt_shift_down) - 1, &key) == 0);
	ASSERT_EQ_INT(ALT_SHIFT_ARROW_DOWN, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_alt_shift_up,
				sizeof(csi_alt_shift_up) - 1, &key) == 0);
	ASSERT_EQ_INT(ALT_SHIFT_ARROW_UP, key);

	ASSERT_TRUE(editor_read_key_with_input(csi_ctrl_left, sizeof(csi_ctrl_left) - 1, &key) == 0);
	ASSERT_EQ_INT(CTRL_ARROW_LEFT, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_ctrl_right, sizeof(csi_ctrl_right) - 1, &key) == 0);
	ASSERT_EQ_INT(CTRL_ARROW_RIGHT, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_ctrl_down, sizeof(csi_ctrl_down) - 1, &key) == 0);
	ASSERT_EQ_INT(CTRL_ARROW_DOWN, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_ctrl_up, sizeof(csi_ctrl_up) - 1, &key) == 0);
	ASSERT_EQ_INT(CTRL_ARROW_UP, key);

	ASSERT_TRUE(editor_read_key_with_input(csi_ctrl_alt_left, sizeof(csi_ctrl_alt_left) - 1, &key) == 0);
	ASSERT_EQ_INT(CTRL_ALT_ARROW_LEFT, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_ctrl_alt_right, sizeof(csi_ctrl_alt_right) - 1, &key) == 0);
	ASSERT_EQ_INT(CTRL_ALT_ARROW_RIGHT, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_ctrl_alt_down, sizeof(csi_ctrl_alt_down) - 1, &key) == 0);
	ASSERT_EQ_INT(CTRL_ALT_ARROW_DOWN, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_ctrl_alt_up, sizeof(csi_ctrl_alt_up) - 1, &key) == 0);
	ASSERT_EQ_INT(CTRL_ALT_ARROW_UP, key);

	ASSERT_TRUE(editor_read_key_with_input(fallback_alt_left, sizeof(fallback_alt_left) - 1, &key) == 0);
	ASSERT_EQ_INT(ALT_ARROW_LEFT, key);
	ASSERT_TRUE(editor_read_key_with_input(fallback_alt_right,
				sizeof(fallback_alt_right) - 1, &key) == 0);
	ASSERT_EQ_INT(ALT_ARROW_RIGHT, key);
	ASSERT_TRUE(editor_read_key_with_input(fallback_alt_down, sizeof(fallback_alt_down) - 1, &key) == 0);
	ASSERT_EQ_INT(ALT_ARROW_DOWN, key);
	ASSERT_TRUE(editor_read_key_with_input(fallback_alt_up, sizeof(fallback_alt_up) - 1, &key) == 0);
	ASSERT_EQ_INT(ALT_ARROW_UP, key);

	ASSERT_TRUE(editor_read_key_with_input(alt_letter_lower, sizeof(alt_letter_lower) - 1, &key) == 0);
	ASSERT_EQ_INT(EDITOR_ALT_LETTER_KEY('a'), key);
	ASSERT_TRUE(editor_read_key_with_input(alt_letter_upper, sizeof(alt_letter_upper) - 1, &key) == 0);
	ASSERT_EQ_INT(EDITOR_ALT_LETTER_KEY('a'), key);
	ASSERT_TRUE(editor_read_key_with_input(ctrl_alt_letter, sizeof(ctrl_alt_letter), &key) == 0);
	ASSERT_EQ_INT(EDITOR_CTRL_ALT_LETTER_KEY('b'), key);
	return 0;
}

static int test_editor_read_key_sgr_mouse_events(void) {
	int key = 0;
	struct editorMouseEvent event;
	char left_click[] = "\x1b[<0;5;3M";
	char ctrl_left_click[] = "\x1b[<16;12;6M";
	char left_drag[] = "\x1b[<32;6;4M";
	char left_release[] = "\x1b[<0;6;4m";
	char left_release_alt_cb[] = "\x1b[<3;7;4m";
	char wheel_up[] = "\x1b[<64;7;2M";
	char wheel_down[] = "\x1b[<65;4;9M";
	char wheel_left[] = "\x1b[<66;8;3M";
	char wheel_right[] = "\x1b[<67;9;3M";
	char shift_wheel_up[] = "\x1b[<68;10;5M";
	char shift_wheel_down[] = "\x1b[<69;11;5M";
	char modifier_drag_then_plain[] = "\x1b[<36;1;1MZ";
	char unsupported_then_plain[] = "\x1b[<2;1;1MY";

	ASSERT_TRUE(editor_read_key_with_input(left_click, sizeof(left_click) - 1, &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_LEFT_PRESS, event.kind);
	ASSERT_EQ_INT(5, event.x);
	ASSERT_EQ_INT(3, event.y);
	ASSERT_EQ_INT(EDITOR_MOUSE_MOD_NONE, event.modifiers);
	ASSERT_EQ_INT(0, editorConsumeMouseEvent(&event));

	ASSERT_TRUE(editor_read_key_with_input(ctrl_left_click, sizeof(ctrl_left_click) - 1, &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_LEFT_PRESS, event.kind);
	ASSERT_EQ_INT(12, event.x);
	ASSERT_EQ_INT(6, event.y);
	ASSERT_EQ_INT(EDITOR_MOUSE_MOD_CTRL, event.modifiers);

	ASSERT_TRUE(editor_read_key_with_input(left_drag, sizeof(left_drag) - 1, &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_LEFT_DRAG, event.kind);
	ASSERT_EQ_INT(6, event.x);
	ASSERT_EQ_INT(4, event.y);
	ASSERT_EQ_INT(EDITOR_MOUSE_MOD_NONE, event.modifiers);

	ASSERT_TRUE(editor_read_key_with_input(left_release, sizeof(left_release) - 1, &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_LEFT_RELEASE, event.kind);
	ASSERT_EQ_INT(6, event.x);
	ASSERT_EQ_INT(4, event.y);
	ASSERT_EQ_INT(EDITOR_MOUSE_MOD_NONE, event.modifiers);

	ASSERT_TRUE(editor_read_key_with_input(left_release_alt_cb, sizeof(left_release_alt_cb) - 1, &key) ==
			0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_LEFT_RELEASE, event.kind);
	ASSERT_EQ_INT(7, event.x);
	ASSERT_EQ_INT(4, event.y);

	ASSERT_TRUE(editor_read_key_with_input(wheel_up, sizeof(wheel_up) - 1, &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_WHEEL_UP, event.kind);
	ASSERT_EQ_INT(7, event.x);
	ASSERT_EQ_INT(2, event.y);

	ASSERT_TRUE(editor_read_key_with_input(wheel_down, sizeof(wheel_down) - 1, &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_WHEEL_DOWN, event.kind);
	ASSERT_EQ_INT(4, event.x);
	ASSERT_EQ_INT(9, event.y);

	ASSERT_TRUE(editor_read_key_with_input(wheel_left, sizeof(wheel_left) - 1, &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_WHEEL_LEFT, event.kind);
	ASSERT_EQ_INT(8, event.x);
	ASSERT_EQ_INT(3, event.y);

	ASSERT_TRUE(editor_read_key_with_input(wheel_right, sizeof(wheel_right) - 1, &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_WHEEL_RIGHT, event.kind);
	ASSERT_EQ_INT(9, event.x);
	ASSERT_EQ_INT(3, event.y);

	ASSERT_TRUE(editor_read_key_with_input(shift_wheel_up, sizeof(shift_wheel_up) - 1, &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_WHEEL_LEFT, event.kind);
	ASSERT_EQ_INT(10, event.x);
	ASSERT_EQ_INT(5, event.y);

	ASSERT_TRUE(editor_read_key_with_input(shift_wheel_down, sizeof(shift_wheel_down) - 1, &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_WHEEL_RIGHT, event.kind);
	ASSERT_EQ_INT(11, event.x);
	ASSERT_EQ_INT(5, event.y);

	ASSERT_TRUE(editor_read_key_with_input(modifier_drag_then_plain,
				sizeof(modifier_drag_then_plain) - 1, &key) == 0);
	ASSERT_EQ_INT('Z', key);
	ASSERT_EQ_INT(0, editorConsumeMouseEvent(&event));

	ASSERT_TRUE(editor_read_key_with_input(unsupported_then_plain,
				sizeof(unsupported_then_plain) - 1, &key) == 0);
	ASSERT_EQ_INT('Y', key);
	ASSERT_EQ_INT(0, editorConsumeMouseEvent(&event));
	return 0;
}

static int test_editor_read_key_returns_input_eof_event_on_closed_stdin(void) {
	int key = 0;
	ASSERT_TRUE(editor_read_key_with_input("", 0, &key) == 0);
	ASSERT_EQ_INT(INPUT_EOF_EVENT, key);
	return 0;
}

static int test_editor_read_key_escape_parse_eof_returns_input_eof_event(void) {
	int key = 0;
	char incomplete[] = "\x1b[";
	ASSERT_TRUE(editor_read_key_with_input(incomplete, sizeof(incomplete) - 1, &key) == 0);
	ASSERT_EQ_INT(INPUT_EOF_EVENT, key);
	return 0;
}

static int test_editor_read_key_returns_resize_event_when_queued(void) {
	int key = 0;
	char plain[] = "x";

	editorQueueResizeEvent();
	ASSERT_TRUE(editor_read_key_with_input(plain, sizeof(plain) - 1, &key) == 0);
	ASSERT_EQ_INT(RESIZE_EVENT, key);

	ASSERT_TRUE(editor_read_key_with_input(plain, sizeof(plain) - 1, &key) == 0);
	ASSERT_EQ_INT('x', key);
	return 0;
}

static int test_read_cursor_position_and_window_size_fallback(void) {
	char response[] = "\x1b[24;80R";
	int rows = 0;
	int cols = 0;
	int saved_stdin;
	size_t stdout_len = 0;

	struct stdoutCapture capture;
	ASSERT_TRUE(start_stdout_capture(&capture) == 0);
	ASSERT_TRUE(setup_stdin_bytes(response, sizeof(response) - 1, &saved_stdin) == 0);

	ASSERT_EQ_INT(0, readWindowSize(&rows, &cols));

	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);
	char *stdout_bytes = stop_stdout_capture(&capture, &stdout_len);
	ASSERT_TRUE(stdout_bytes != NULL);

	ASSERT_EQ_INT(24, rows);
	ASSERT_EQ_INT(80, cols);
	ASSERT_TRUE(strstr(stdout_bytes, "\x1b[999C\x1b[999B") != NULL);
	ASSERT_TRUE(strstr(stdout_bytes, "\x1b[6n") != NULL);

	free(stdout_bytes);
	return 0;
}

static int test_read_cursor_position_rejects_malformed_responses(void) {
	struct {
		const char *response;
		size_t len;
	} cases[] = {
		{"\x1b[", sizeof("\x1b[") - 1},
		{"\x1b[R", sizeof("\x1b[R") - 1},
		{"\x1b[24;80", sizeof("\x1b[24;80") - 1},
		{"\x1b[24;R", sizeof("\x1b[24;R") - 1},
		{"24;80R", sizeof("24;80R") - 1},
		{"\x1b[24R", sizeof("\x1b[24R") - 1},
		{"\x1b[24;xxR", sizeof("\x1b[24;xxR") - 1},
		{"\x1b[;80R", sizeof("\x1b[;80R") - 1},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		int rows = -1;
		int cols = -1;
		int saved_stdin;
		size_t stdout_len = 0;
		struct stdoutCapture capture;

		ASSERT_TRUE(start_stdout_capture(&capture) == 0);
		ASSERT_TRUE(setup_stdin_bytes(cases[i].response, cases[i].len, &saved_stdin) == 0);
		ASSERT_EQ_INT(-1, readCursorPosition(&rows, &cols));
		ASSERT_TRUE(restore_stdin(saved_stdin) == 0);
		char *stdout_bytes = stop_stdout_capture(&capture, &stdout_len);
		ASSERT_TRUE(stdout_bytes != NULL);
		ASSERT_TRUE(strstr(stdout_bytes, "\x1b[6n") != NULL);
		free(stdout_bytes);
	}

	return 0;
}

static int test_editor_refresh_window_size_clamps_tiny_terminal(void) {
	char response[] = "\x1b[1;5R";
	int saved_stdin;
	size_t stdout_len = 0;
	struct stdoutCapture capture;

	E.window_rows = 8;
	E.window_cols = 40;

	ASSERT_TRUE(start_stdout_capture(&capture) == 0);
	ASSERT_TRUE(setup_stdin_bytes(response, sizeof(response) - 1, &saved_stdin) == 0);
	ASSERT_EQ_INT(1, editorRefreshWindowSize());
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);
	char *stdout_bytes = stop_stdout_capture(&capture, &stdout_len);
	ASSERT_TRUE(stdout_bytes != NULL);

	ASSERT_EQ_INT(1, E.window_rows);
	ASSERT_EQ_INT(5, E.window_cols);
	ASSERT_TRUE(strstr(stdout_bytes, "\x1b[6n") != NULL);
	free(stdout_bytes);
	return 0;
}

static int test_editor_refresh_window_size_failure_keeps_previous_dimensions(void) {
	char malformed[] = "\x1b[";
	int saved_stdin;
	size_t stdout_len = 0;
	struct stdoutCapture capture;

	E.window_rows = 7;
	E.window_cols = 22;

	ASSERT_TRUE(start_stdout_capture(&capture) == 0);
	ASSERT_TRUE(setup_stdin_bytes(malformed, sizeof(malformed) - 1, &saved_stdin) == 0);
	ASSERT_EQ_INT(0, editorRefreshWindowSize());
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);
	char *stdout_bytes = stop_stdout_capture(&capture, &stdout_len);
	ASSERT_TRUE(stdout_bytes != NULL);

	ASSERT_EQ_INT(7, E.window_rows);
	ASSERT_EQ_INT(22, E.window_cols);
	free(stdout_bytes);
	return 0;
}

static int test_editor_refresh_window_size_reserves_tab_status_and_message_rows(void) {
	char response[] = "\x1b[9;33R";
	int saved_stdin;
	size_t stdout_len = 0;
	struct stdoutCapture capture;

	E.window_rows = 8;
	E.window_cols = 40;

	ASSERT_TRUE(start_stdout_capture(&capture) == 0);
	ASSERT_TRUE(setup_stdin_bytes(response, sizeof(response) - 1, &saved_stdin) == 0);
	ASSERT_EQ_INT(1, editorRefreshWindowSize());
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);
	char *stdout_bytes = stop_stdout_capture(&capture, &stdout_len);
	ASSERT_TRUE(stdout_bytes != NULL);

	ASSERT_EQ_INT(6, E.window_rows);
	ASSERT_EQ_INT(33, E.window_cols);
	free(stdout_bytes);
	return 0;
}

static int test_editor_keymap_load_valid_project_overrides_defaults(void) {
	char dir_template[] = "/tmp/rotide-test-keymap-valid-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path,
				"[keymap]\n"
				"save = \"ctrl+a\"\n"
				"redraw = \"ctrl+s\"\n"));

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus status = editorKeymapLoadFromPaths(&keymap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_OK, status);

	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('a'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_SAVE, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('s'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_REDRAW, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('q'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_QUIT, action);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_load_unknown_action_falls_back_to_defaults(void) {
	char dir_template[] = "/tmp/rotide-test-keymap-bad-action-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path,
				"[keymap]\n"
				"not_a_real_action = \"ctrl+a\"\n"));

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus status = editorKeymapLoadFromPaths(&keymap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_INVALID_PROJECT, status);

	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('s'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_SAVE, action);
	ASSERT_TRUE(!editorKeymapLookupAction(&keymap, CTRL_KEY('a'), &action));

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_load_unknown_keyspec_falls_back_to_defaults(void) {
	char dir_template[] = "/tmp/rotide-test-keymap-bad-key-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path,
				"[keymap]\n"
				"save = \"meta+s\"\n"));

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus status = editorKeymapLoadFromPaths(&keymap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_INVALID_PROJECT, status);

	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('s'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_SAVE, action);
	ASSERT_TRUE(!editorKeymapLookupAction(&keymap, CTRL_KEY('a'), &action));

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_load_duplicate_binding_falls_back_to_defaults(void) {
	char dir_template[] = "/tmp/rotide-test-keymap-dup-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path,
				"[keymap]\n"
				"save = \"ctrl+a\"\n"
				"quit = \"ctrl+a\"\n"));

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus status = editorKeymapLoadFromPaths(&keymap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_INVALID_PROJECT, status);

	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('s'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_SAVE, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('q'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_QUIT, action);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_load_malformed_toml_falls_back_to_defaults(void) {
	char dir_template[] = "/tmp/rotide-test-keymap-malformed-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path,
				"[keymap\n"
				"save = \"ctrl+a\"\n"));

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus status = editorKeymapLoadFromPaths(&keymap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_INVALID_PROJECT, status);

	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('s'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_SAVE, action);
	ASSERT_TRUE(!editorKeymapLookupAction(&keymap, CTRL_KEY('a'), &action));

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_global_then_project_precedence(void) {
	char dir_template[] = "/tmp/rotide-test-keymap-precedence-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char global_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));

	ASSERT_TRUE(write_text_file(global_path,
				"[keymap]\n"
				"save = \"ctrl+a\"\n"));
	ASSERT_TRUE(write_text_file(project_path,
				"[keymap]\n"
				"save = \"ctrl+t\"\n"));

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus status =
			editorKeymapLoadFromPaths(&keymap, global_path, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_OK, status);

	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('t'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_SAVE, action);
	if (editorKeymapLookupAction(&keymap, CTRL_KEY('a'), &action)) {
		ASSERT_TRUE(action != EDITOR_ACTION_SAVE);
	}

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(unlink(global_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_invalid_global_ignored_when_project_valid(void) {
	char dir_template[] = "/tmp/rotide-test-keymap-invalid-global-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char global_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));

	ASSERT_TRUE(write_text_file(global_path,
				"[keymap\n"
				"save = \"ctrl+a\"\n"));
	ASSERT_TRUE(write_text_file(project_path,
				"[keymap]\n"
				"save = \"ctrl+t\"\n"));

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus status =
			editorKeymapLoadFromPaths(&keymap, global_path, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_INVALID_GLOBAL, status);

	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('t'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_SAVE, action);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(unlink(global_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_load_configured_prefers_project_over_global(void) {
	int failed = 1;
	struct envVarBackup home_backup;
	char *original_cwd = NULL;
	char home_dir[512] = "";
	char dot_rotide_dir[512] = "";
	char global_path[512] = "";
	char project_path[512] = "";
	char root_template[] = "/tmp/rotide-test-keymap-configured-XXXXXX";

	if (!backup_env_var(&home_backup, "HOME")) {
		return 1;
	}

	original_cwd = getcwd(NULL, 0);
	if (original_cwd == NULL) {
		(void)restore_env_var(&home_backup);
		return 1;
	}

	char *root_path = mkdtemp(root_template);
	if (root_path == NULL) {
		goto cleanup;
	}

	if (!path_join(home_dir, sizeof(home_dir), root_path, "home")) {
		goto cleanup;
	}
	if (mkdir(home_dir, 0700) == -1) {
		goto cleanup;
	}
	if (!path_join(dot_rotide_dir, sizeof(dot_rotide_dir), home_dir, ".rotide")) {
		goto cleanup;
	}
	if (mkdir(dot_rotide_dir, 0700) == -1) {
		goto cleanup;
	}
	if (!path_join(global_path, sizeof(global_path), dot_rotide_dir, "config.toml")) {
		goto cleanup;
	}
	if (!path_join(project_path, sizeof(project_path), root_path, ".rotide.toml")) {
		goto cleanup;
	}
	if (!write_text_file(global_path,
				"[keymap]\n"
				"save = \"ctrl+t\"\n")) {
		goto cleanup;
	}
	if (!write_text_file(project_path,
				"[keymap]\n"
				"save = \"ctrl+a\"\n")) {
		goto cleanup;
	}
	if (setenv("HOME", home_dir, 1) != 0) {
		goto cleanup;
	}
	if (chdir(root_path) != 0) {
		goto cleanup;
	}

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus status = editorKeymapLoadConfigured(&keymap);
	if (status != EDITOR_KEYMAP_LOAD_OK) {
		goto cleanup;
	}

	enum editorAction action = EDITOR_ACTION_COUNT;
	if (!editorKeymapLookupAction(&keymap, CTRL_KEY('a'), &action) ||
			action != EDITOR_ACTION_SAVE) {
		goto cleanup;
	}
	if (editorKeymapLookupAction(&keymap, CTRL_KEY('t'), &action) &&
			action == EDITOR_ACTION_SAVE) {
		goto cleanup;
	}

	failed = 0;

cleanup:
	if (original_cwd != NULL) {
		if (chdir(original_cwd) != 0) {
			failed = 1;
		}
	}
	if (!restore_env_var(&home_backup)) {
		failed = 1;
	}
	if (project_path[0] != '\0') {
		(void)unlink(project_path);
	}
	if (global_path[0] != '\0') {
		(void)unlink(global_path);
	}
	if (dot_rotide_dir[0] != '\0') {
		(void)rmdir(dot_rotide_dir);
	}
	if (home_dir[0] != '\0') {
		(void)rmdir(home_dir);
	}
	(void)rmdir(root_template);
	free(original_cwd);
	return failed;
}

static int test_editor_cursor_style_load_valid_values_case_insensitive(void) {
	char dir_template[] = "/tmp/rotide-test-cursor-style-valid-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));

	struct {
		const char *value;
		enum editorCursorStyle expected;
	} cases[] = {
		{"BLOCK", EDITOR_CURSOR_STYLE_BLOCK},
		{"bar", EDITOR_CURSOR_STYLE_BAR},
		{"UnderLine", EDITOR_CURSOR_STYLE_UNDERLINE},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		char content[128];
		int written = snprintf(content, sizeof(content), "[editor]\ncursor_style = \"%s\"\n",
				cases[i].value);
		ASSERT_TRUE(written > 0 && (size_t)written < sizeof(content));
		ASSERT_TRUE(write_text_file(project_path, content));

		enum editorCursorStyle style = EDITOR_CURSOR_STYLE_BAR;
		enum editorCursorStyleLoadStatus status =
				editorCursorStyleLoadFromPaths(&style, NULL, project_path);
		ASSERT_EQ_INT(EDITOR_CURSOR_STYLE_LOAD_OK, status);
		ASSERT_EQ_INT(cases[i].expected, style);
	}

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_cursor_style_global_then_project_precedence(void) {
	char dir_template[] = "/tmp/rotide-test-cursor-style-precedence-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char global_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));

	ASSERT_TRUE(write_text_file(global_path,
				"[editor]\n"
				"cursor_style = \"block\"\n"));
	ASSERT_TRUE(write_text_file(project_path,
				"[editor]\n"
				"cursor_style = \"underline\"\n"));

	enum editorCursorStyle style = EDITOR_CURSOR_STYLE_BAR;
	enum editorCursorStyleLoadStatus status =
			editorCursorStyleLoadFromPaths(&style, global_path, project_path);
	ASSERT_EQ_INT(EDITOR_CURSOR_STYLE_LOAD_OK, status);
	ASSERT_EQ_INT(EDITOR_CURSOR_STYLE_UNDERLINE, style);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(unlink(global_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_cursor_style_invalid_values_fallback_to_bar(void) {
	char dir_template[] = "/tmp/rotide-test-cursor-style-invalid-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char global_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));

	ASSERT_TRUE(write_text_file(global_path,
				"[editor]\n"
				"cursor_style = \"invalid\"\n"));
	enum editorCursorStyle style = EDITOR_CURSOR_STYLE_UNDERLINE;
	enum editorCursorStyleLoadStatus status =
			editorCursorStyleLoadFromPaths(&style, global_path, NULL);
	ASSERT_EQ_INT(EDITOR_CURSOR_STYLE_LOAD_INVALID_GLOBAL, status);
	ASSERT_EQ_INT(EDITOR_CURSOR_STYLE_BAR, style);

	ASSERT_TRUE(write_text_file(global_path,
				"[editor]\n"
				"cursor_style = \"block\"\n"));
	ASSERT_TRUE(write_text_file(project_path,
				"[editor]\n"
				"cursor_style = \"not-real\"\n"));
	style = EDITOR_CURSOR_STYLE_UNDERLINE;
	status = editorCursorStyleLoadFromPaths(&style, global_path, project_path);
	ASSERT_EQ_INT(EDITOR_CURSOR_STYLE_LOAD_INVALID_PROJECT, status);
	ASSERT_EQ_INT(EDITOR_CURSOR_STYLE_BAR, style);

	ASSERT_TRUE(write_text_file(global_path,
				"[editor]\n"
				"cursor_style = \"bad-global\"\n"));
	ASSERT_TRUE(write_text_file(project_path,
				"[editor]\n"
				"cursor_style = \"bad-project\"\n"));
	style = EDITOR_CURSOR_STYLE_UNDERLINE;
	status = editorCursorStyleLoadFromPaths(&style, global_path, project_path);
	ASSERT_EQ_INT(
			EDITOR_CURSOR_STYLE_LOAD_INVALID_GLOBAL | EDITOR_CURSOR_STYLE_LOAD_INVALID_PROJECT,
			status);
	ASSERT_EQ_INT(EDITOR_CURSOR_STYLE_BAR, style);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(unlink(global_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_cursor_style_load_configured_prefers_project_over_global(void) {
	int failed = 1;
	struct envVarBackup home_backup;
	char *original_cwd = NULL;
	char home_dir[512] = "";
	char dot_rotide_dir[512] = "";
	char global_path[512] = "";
	char project_path[512] = "";
	char root_template[] = "/tmp/rotide-test-cursor-style-configured-XXXXXX";

	if (!backup_env_var(&home_backup, "HOME")) {
		return 1;
	}

	original_cwd = getcwd(NULL, 0);
	if (original_cwd == NULL) {
		(void)restore_env_var(&home_backup);
		return 1;
	}

	char *root_path = mkdtemp(root_template);
	if (root_path == NULL) {
		goto cleanup;
	}

	if (!path_join(home_dir, sizeof(home_dir), root_path, "home")) {
		goto cleanup;
	}
	if (mkdir(home_dir, 0700) == -1) {
		goto cleanup;
	}
	if (!path_join(dot_rotide_dir, sizeof(dot_rotide_dir), home_dir, ".rotide")) {
		goto cleanup;
	}
	if (mkdir(dot_rotide_dir, 0700) == -1) {
		goto cleanup;
	}
	if (!path_join(global_path, sizeof(global_path), dot_rotide_dir, "config.toml")) {
		goto cleanup;
	}
	if (!path_join(project_path, sizeof(project_path), root_path, ".rotide.toml")) {
		goto cleanup;
	}
	if (!write_text_file(global_path,
				"[editor]\n"
				"cursor_style = \"block\"\n")) {
		goto cleanup;
	}
	if (!write_text_file(project_path,
				"[editor]\n"
				"cursor_style = \"underline\"\n")) {
		goto cleanup;
	}
	if (setenv("HOME", home_dir, 1) != 0) {
		goto cleanup;
	}
	if (chdir(root_path) != 0) {
		goto cleanup;
	}

	enum editorCursorStyle style = EDITOR_CURSOR_STYLE_BAR;
	enum editorCursorStyleLoadStatus status = editorCursorStyleLoadConfigured(&style);
	if (status != EDITOR_CURSOR_STYLE_LOAD_OK) {
		goto cleanup;
	}
	if (style != EDITOR_CURSOR_STYLE_UNDERLINE) {
		goto cleanup;
	}

	failed = 0;

cleanup:
	if (original_cwd != NULL) {
		if (chdir(original_cwd) != 0) {
			failed = 1;
		}
	}
	if (!restore_env_var(&home_backup)) {
		failed = 1;
	}
	if (project_path[0] != '\0') {
		(void)unlink(project_path);
	}
	if (global_path[0] != '\0') {
		(void)unlink(global_path);
	}
	if (dot_rotide_dir[0] != '\0') {
		(void)rmdir(dot_rotide_dir);
	}
	if (home_dir[0] != '\0') {
		(void)rmdir(home_dir);
	}
	(void)rmdir(root_template);
	free(original_cwd);
	return failed;
}

static int test_editor_cursor_style_invalid_setting_does_not_break_keymap_loading(void) {
	char dir_template[] = "/tmp/rotide-test-cursor-style-keymap-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path,
				"[editor]\n"
				"cursor_style = \"nope\"\n"
				"[keymap]\n"
				"save = \"ctrl+a\"\n"));

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus keymap_status =
			editorKeymapLoadFromPaths(&keymap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_OK, keymap_status);

	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('a'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_SAVE, action);

	enum editorCursorStyle style = EDITOR_CURSOR_STYLE_UNDERLINE;
	enum editorCursorStyleLoadStatus style_status =
			editorCursorStyleLoadFromPaths(&style, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_CURSOR_STYLE_LOAD_INVALID_PROJECT, style_status);
	ASSERT_EQ_INT(EDITOR_CURSOR_STYLE_BAR, style);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_syntax_theme_load_global_project_precedence(void) {
	char dir_template[] = "/tmp/rotide-test-syntax-theme-precedence-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char global_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));

	ASSERT_TRUE(write_text_file(global_path,
				"[theme.syntax]\n"
				"comment = \"red\"\n"
				"keyword = \"blue\"\n"));
	ASSERT_TRUE(write_text_file(project_path,
				"[theme.syntax]\n"
				"keyword = \"bright_yellow\"\n"
				"string = \"green\"\n"));

	enum editorThemeColor theme[EDITOR_SYNTAX_HL_CLASS_COUNT];
	enum editorSyntaxThemeLoadStatus status =
			editorSyntaxThemeLoadFromPaths(theme, global_path, project_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_THEME_LOAD_OK, status);
	ASSERT_EQ_INT(EDITOR_THEME_COLOR_RED, theme[EDITOR_SYNTAX_HL_COMMENT]);
	ASSERT_EQ_INT(EDITOR_THEME_COLOR_BRIGHT_YELLOW, theme[EDITOR_SYNTAX_HL_KEYWORD]);
	ASSERT_EQ_INT(EDITOR_THEME_COLOR_GREEN, theme[EDITOR_SYNTAX_HL_STRING]);
	ASSERT_EQ_INT(EDITOR_THEME_COLOR_BRIGHT_CYAN, theme[EDITOR_SYNTAX_HL_TYPE]);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(unlink(global_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_syntax_theme_invalid_entries_nonfatal_and_keymap_still_loads(void) {
	char dir_template[] = "/tmp/rotide-test-syntax-theme-invalid-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char global_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));

	ASSERT_TRUE(write_text_file(global_path,
				"[theme.syntax]\n"
				"comment = \"not-a-color\"\n"
				"keyword = \"bright_blue\"\n"));
	ASSERT_TRUE(write_text_file(project_path,
				"[theme.syntax]\n"
				"unknown_scope = \"red\"\n"
				"string = \"green\"\n"
				"[keymap]\n"
				"save = \"ctrl+a\"\n"));

	enum editorThemeColor theme[EDITOR_SYNTAX_HL_CLASS_COUNT];
	enum editorSyntaxThemeLoadStatus theme_status =
			editorSyntaxThemeLoadFromPaths(theme, global_path, project_path);
	ASSERT_EQ_INT(
			EDITOR_SYNTAX_THEME_LOAD_INVALID_GLOBAL | EDITOR_SYNTAX_THEME_LOAD_INVALID_PROJECT,
			theme_status);
	ASSERT_EQ_INT(EDITOR_THEME_COLOR_BRIGHT_BLACK, theme[EDITOR_SYNTAX_HL_COMMENT]);
	ASSERT_EQ_INT(EDITOR_THEME_COLOR_BRIGHT_BLUE, theme[EDITOR_SYNTAX_HL_KEYWORD]);
	ASSERT_EQ_INT(EDITOR_THEME_COLOR_GREEN, theme[EDITOR_SYNTAX_HL_STRING]);

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus keymap_status =
			editorKeymapLoadFromPaths(&keymap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_OK, keymap_status);
	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('a'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_SAVE, action);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(unlink(global_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_load_modifier_combo_specs_case_insensitive(void) {
	char dir_template[] = "/tmp/rotide-test-keymap-combos-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path,
				"[keymap]\n"
				"next_tab = \"CTRL+ALT+RIGHT\"\n"
				"prev_tab = \"ctrl+UP\"\n"
				"toggle_drawer = \"ctrl+alt+e\"\n"
				"resize_drawer_narrow = \"SHIFT+ALT+LEFT\"\n"
				"resize_drawer_widen = \"aLt+ShIfT+RiGhT\"\n"
				"move_left = \"AlT+b\"\n"
				"move_right = \"cTrL+aLt+z\"\n"));

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus status = editorKeymapLoadFromPaths(&keymap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_OK, status);

	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(!editorKeymapLookupAction(&keymap, ALT_ARROW_RIGHT, &action));
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_ALT_ARROW_RIGHT, &action));
	ASSERT_EQ_INT(EDITOR_ACTION_NEXT_TAB, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_ARROW_UP, &action));
	ASSERT_EQ_INT(EDITOR_ACTION_PREV_TAB, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, EDITOR_CTRL_ALT_LETTER_KEY('e'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_TOGGLE_DRAWER, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, EDITOR_ALT_LETTER_KEY('b'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_MOVE_LEFT, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, EDITOR_CTRL_ALT_LETTER_KEY('z'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_MOVE_RIGHT, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, ALT_SHIFT_ARROW_LEFT, &action));
	ASSERT_EQ_INT(EDITOR_ACTION_RESIZE_DRAWER_NARROW, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, ALT_SHIFT_ARROW_RIGHT, &action));
	ASSERT_EQ_INT(EDITOR_ACTION_RESIZE_DRAWER_WIDEN, action);

	char binding[24];
	ASSERT_TRUE(editorKeymapFormatBinding(&keymap, EDITOR_ACTION_MOVE_LEFT, binding, sizeof(binding)));
	ASSERT_EQ_STR("Alt-B", binding);
	ASSERT_TRUE(editorKeymapFormatBinding(&keymap, EDITOR_ACTION_MOVE_RIGHT, binding, sizeof(binding)));
	ASSERT_EQ_STR("Ctrl-Alt-Z", binding);
	ASSERT_TRUE(editorKeymapFormatBinding(&keymap, EDITOR_ACTION_PREV_TAB, binding, sizeof(binding)));
	ASSERT_EQ_STR("Ctrl-Up", binding);
	ASSERT_TRUE(editorKeymapFormatBinding(&keymap, EDITOR_ACTION_TOGGLE_DRAWER, binding,
				sizeof(binding)));
	ASSERT_EQ_STR("Ctrl-Alt-E", binding);
	ASSERT_TRUE(editorKeymapFormatBinding(&keymap, EDITOR_ACTION_NEXT_TAB, binding, sizeof(binding)));
	ASSERT_EQ_STR("Ctrl-Alt-Right", binding);
	ASSERT_TRUE(editorKeymapFormatBinding(&keymap, EDITOR_ACTION_RESIZE_DRAWER_NARROW, binding,
				sizeof(binding)));
	ASSERT_EQ_STR("Alt-Shift-Left", binding);
	ASSERT_TRUE(editorKeymapFormatBinding(&keymap, EDITOR_ACTION_RESIZE_DRAWER_WIDEN, binding,
				sizeof(binding)));
	ASSERT_EQ_STR("Alt-Shift-Right", binding);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_load_invalid_modifier_combos_fall_back_to_defaults(void) {
	char dir_template[] = "/tmp/rotide-test-keymap-bad-combos-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));

	const char *invalid_lines[] = {
		"next_tab = \"ctrl+ctrl+right\"\n",
		"next_tab = \"shift+right\"\n",
		"next_tab = \"ctrl+shift+right\"\n",
		"next_tab = \"alt+home\"\n",
		"next_tab = \"ctrl+alt+a+b\"\n",
	};
	for (size_t i = 0; i < sizeof(invalid_lines) / sizeof(invalid_lines[0]); i++) {
		char content[256];
		int written = snprintf(content, sizeof(content), "[keymap]\n%s", invalid_lines[i]);
		ASSERT_TRUE(written > 0 && (size_t)written < sizeof(content));
		ASSERT_TRUE(write_text_file(project_path, content));

		struct editorKeymap keymap;
		enum editorKeymapLoadStatus status =
				editorKeymapLoadFromPaths(&keymap, NULL, project_path);
		ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_INVALID_PROJECT, status);

		enum editorAction action = EDITOR_ACTION_COUNT;
		ASSERT_TRUE(editorKeymapLookupAction(&keymap, ALT_ARROW_RIGHT, &action));
		ASSERT_EQ_INT(EDITOR_ACTION_NEXT_TAB, action);
	}

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_defaults_include_tab_actions(void) {
	struct editorKeymap keymap;
	editorKeymapInitDefaults(&keymap);

	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('n'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_NEW_TAB, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('w'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_CLOSE_TAB, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, ALT_ARROW_RIGHT, &action));
	ASSERT_EQ_INT(EDITOR_ACTION_NEXT_TAB, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, ALT_ARROW_LEFT, &action));
	ASSERT_EQ_INT(EDITOR_ACTION_PREV_TAB, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('e'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_FOCUS_DRAWER, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, EDITOR_CTRL_ALT_LETTER_KEY('e'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_TOGGLE_DRAWER, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, ALT_SHIFT_ARROW_LEFT, &action));
	ASSERT_EQ_INT(EDITOR_ACTION_RESIZE_DRAWER_NARROW, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, ALT_SHIFT_ARROW_RIGHT, &action));
	ASSERT_EQ_INT(EDITOR_ACTION_RESIZE_DRAWER_WIDEN, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_ARROW_LEFT, &action));
	ASSERT_EQ_INT(EDITOR_ACTION_SCROLL_LEFT, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_ARROW_RIGHT, &action));
	ASSERT_EQ_INT(EDITOR_ACTION_SCROLL_RIGHT, action);
	return 0;
}

static int test_editor_keymap_defaults_include_goto_definition_action(void) {
	struct editorKeymap keymap;
	editorKeymapInitDefaults(&keymap);

	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('o'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_GOTO_DEFINITION, action);

	char binding[24];
	ASSERT_TRUE(editorKeymapFormatBinding(&keymap, EDITOR_ACTION_GOTO_DEFINITION, binding,
			sizeof(binding)));
	ASSERT_EQ_STR("Ctrl-O", binding);
	return 0;
}

static int test_editor_keymap_load_accepts_goto_definition_ctrl_o(void) {
	char dir_template[] = "/tmp/rotide-test-keymap-gotodef-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path,
				"[keymap]\n"
				"goto_definition = \"ctrl+o\"\n"));

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus status = editorKeymapLoadFromPaths(&keymap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_OK, status);

	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('o'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_GOTO_DEFINITION, action);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_load_rejects_ctrl_i_binding_that_conflicts_with_tab_input(void) {
	char dir_template[] = "/tmp/rotide-test-keymap-tab-conflict-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path,
				"[keymap]\n"
				"goto_definition = \"ctrl+i\"\n"));

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus status = editorKeymapLoadFromPaths(&keymap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_INVALID_PROJECT, status);

	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('o'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_GOTO_DEFINITION, action);
	ASSERT_TRUE(!editorKeymapLookupAction(&keymap, '\t', &action));

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_load_rejects_reserved_terminal_aliases_for_other_actions(void) {
	char dir_template[] = "/tmp/rotide-test-keymap-reserved-alias-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));

	struct {
		const char *line;
		int default_key;
		enum editorAction default_action;
	} cases[] = {
		{"save = \"ctrl+m\"\n", CTRL_KEY('s'), EDITOR_ACTION_SAVE},
		{"save = \"ctrl+[\"\n", CTRL_KEY('s'), EDITOR_ACTION_SAVE},
		{"save = \"ctrl+h\"\n", CTRL_KEY('s'), EDITOR_ACTION_SAVE},
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		char content[128];
		int written = snprintf(content, sizeof(content), "[keymap]\n%s", cases[i].line);
		ASSERT_TRUE(written > 0 && (size_t)written < sizeof(content));
		ASSERT_TRUE(write_text_file(project_path, content));

		struct editorKeymap keymap;
		enum editorKeymapLoadStatus status =
				editorKeymapLoadFromPaths(&keymap, NULL, project_path);
		ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_INVALID_PROJECT, status);

		enum editorAction action = EDITOR_ACTION_COUNT;
		ASSERT_TRUE(editorKeymapLookupAction(&keymap, cases[i].default_key, &action));
		ASSERT_EQ_INT(cases[i].default_action, action);
	}

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_keymap_load_accepts_reserved_terminal_aliases_for_matching_actions(void) {
	char dir_template[] = "/tmp/rotide-test-keymap-reserved-allowed-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path,
				"[keymap]\n"
				"newline = \"ctrl+m\"\n"
				"escape = \"ctrl+[\"\n"
				"backspace = \"ctrl+h\"\n"));

	struct editorKeymap keymap;
	enum editorKeymapLoadStatus status = editorKeymapLoadFromPaths(&keymap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_OK, status);

	enum editorAction action = EDITOR_ACTION_COUNT;
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, '\r', &action));
	ASSERT_EQ_INT(EDITOR_ACTION_NEWLINE, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, '\x1b', &action));
	ASSERT_EQ_INT(EDITOR_ACTION_ESCAPE, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, CTRL_KEY('h'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_BACKSPACE, action);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_lsp_config_defaults_and_precedence(void) {
	int gopls_enabled = 0;
	int clangd_enabled = 0;
	int html_enabled = 0;
	int css_enabled = 0;
	int json_enabled = 0;
	int eslint_enabled = 0;
	char gopls_command[PATH_MAX];
	char gopls_install_command[PATH_MAX];
	char clangd_command[PATH_MAX];
	char html_command[PATH_MAX];
	char css_command[PATH_MAX];
	char json_command[PATH_MAX];
	char eslint_command[PATH_MAX];
	char vscode_langservers_install_command[PATH_MAX];

	enum editorLspConfigLoadStatus defaults_status =
			editorLspConfigLoadFromPaths(&gopls_enabled, &clangd_enabled, &html_enabled,
					&css_enabled, &json_enabled, &eslint_enabled,
					gopls_command, sizeof(gopls_command), gopls_install_command,
					sizeof(gopls_install_command), clangd_command, sizeof(clangd_command),
					html_command, sizeof(html_command), css_command, sizeof(css_command),
					json_command, sizeof(json_command), eslint_command, sizeof(eslint_command),
					vscode_langservers_install_command,
					sizeof(vscode_langservers_install_command), NULL, NULL);
	ASSERT_EQ_INT(EDITOR_LSP_CONFIG_LOAD_OK, defaults_status);
	ASSERT_EQ_INT(1, gopls_enabled);
	ASSERT_EQ_INT(1, clangd_enabled);
	ASSERT_EQ_INT(1, html_enabled);
	ASSERT_EQ_INT(1, css_enabled);
	ASSERT_EQ_INT(1, json_enabled);
	ASSERT_EQ_INT(1, eslint_enabled);
	ASSERT_EQ_STR("gopls", gopls_command);
	ASSERT_EQ_STR("go install golang.org/x/tools/gopls@latest", gopls_install_command);
	ASSERT_EQ_STR("clangd", clangd_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-html-language-server --stdio", html_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-css-language-server --stdio", css_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-json-language-server --stdio", json_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-eslint-language-server --stdio", eslint_command);
	ASSERT_EQ_STR("npm install --global --prefix ~/.local vscode-langservers-extracted",
			vscode_langservers_install_command);

	char dir_template[] = "/tmp/rotide-test-lsp-config-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char global_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));
	ASSERT_TRUE(write_text_file(global_path,
				"[lsp]\n"
				"gopls_enabled = false\n"
				"clangd_enabled = false\n"
				"html_enabled = false\n"
				"css_enabled = false\n"
				"json_enabled = false\n"
				"eslint_enabled = false\n"
				"gopls_command = \"gopls-global\"\n"
				"clangd_command = \"clangd-global\"\n"
				"html_command = \"html-global --stdio\"\n"
				"css_command = \"css-global --stdio\"\n"
				"json_command = \"json-global --stdio\"\n"
				"eslint_command = \"eslint-global --stdio\"\n"
				"gopls_install_command = \"global-install\"\n"
				"vscode_langservers_install_command = \"global-vscode-install\"\n"));
	ASSERT_TRUE(write_text_file(project_path,
				"[lsp]\n"
				"gopls_enabled = true\n"
				"clangd_enabled = false\n"
				"html_enabled = true\n"
				"css_enabled = true\n"
				"json_enabled = false\n"
				"eslint_enabled = true\n"
				"gopls_command = \"gopls-project\"\n"
				"clangd_command = \"clangd-project\"\n"
				"html_command = \"html-project --stdio\"\n"
				"css_command = \"css-project --stdio\"\n"
				"json_command = \"json-project --stdio\"\n"
				"eslint_command = \"eslint-project --stdio\"\n"
				"gopls_install_command = \"project-install\"\n"
				"vscode_langservers_install_command = \"project-vscode-install\"\n"));

	enum editorLspConfigLoadStatus status =
			editorLspConfigLoadFromPaths(&gopls_enabled, &clangd_enabled, &html_enabled,
					&css_enabled, &json_enabled, &eslint_enabled,
					gopls_command, sizeof(gopls_command), gopls_install_command,
					sizeof(gopls_install_command), clangd_command, sizeof(clangd_command),
					html_command, sizeof(html_command), css_command, sizeof(css_command),
					json_command, sizeof(json_command), eslint_command, sizeof(eslint_command),
					vscode_langservers_install_command,
					sizeof(vscode_langservers_install_command), global_path,
					project_path);
	ASSERT_EQ_INT(EDITOR_LSP_CONFIG_LOAD_OK, status);
	ASSERT_EQ_INT(1, gopls_enabled);
	ASSERT_EQ_INT(0, clangd_enabled);
	ASSERT_EQ_INT(1, html_enabled);
	ASSERT_EQ_INT(1, css_enabled);
	ASSERT_EQ_INT(0, json_enabled);
	ASSERT_EQ_INT(1, eslint_enabled);
	ASSERT_EQ_STR("gopls-project", gopls_command);
	ASSERT_EQ_STR("global-install", gopls_install_command);
	ASSERT_EQ_STR("clangd-project", clangd_command);
	ASSERT_EQ_STR("html-project --stdio", html_command);
	ASSERT_EQ_STR("css-project --stdio", css_command);
	ASSERT_EQ_STR("json-project --stdio", json_command);
	ASSERT_EQ_STR("eslint-project --stdio", eslint_command);
	ASSERT_EQ_STR("global-vscode-install", vscode_langservers_install_command);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(unlink(global_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_lsp_config_invalid_values_fallback_defaults(void) {
	int gopls_enabled = 0;
	int clangd_enabled = 0;
	int html_enabled = 0;
	int css_enabled = 0;
	int json_enabled = 0;
	int eslint_enabled = 0;
	char gopls_command[PATH_MAX];
	char gopls_install_command[PATH_MAX];
	char clangd_command[PATH_MAX];
	char html_command[PATH_MAX];
	char css_command[PATH_MAX];
	char json_command[PATH_MAX];
	char eslint_command[PATH_MAX];
	char vscode_langservers_install_command[PATH_MAX];

	char dir_template[] = "/tmp/rotide-test-lsp-config-invalid-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char global_path[512];
	char project_path[512];
	ASSERT_TRUE(path_join(global_path, sizeof(global_path), dir_path, "global.toml"));
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, "project.toml"));

	ASSERT_TRUE(write_text_file(global_path,
				"[lsp]\n"
				"enabled = \"yes\"\n"));
	enum editorLspConfigLoadStatus status =
			editorLspConfigLoadFromPaths(&gopls_enabled, &clangd_enabled, &html_enabled,
					&css_enabled, &json_enabled, &eslint_enabled,
					gopls_command, sizeof(gopls_command), gopls_install_command,
					sizeof(gopls_install_command), clangd_command, sizeof(clangd_command),
					html_command, sizeof(html_command), css_command, sizeof(css_command),
					json_command, sizeof(json_command), eslint_command, sizeof(eslint_command),
					vscode_langservers_install_command,
					sizeof(vscode_langservers_install_command), global_path, NULL);
	ASSERT_EQ_INT(EDITOR_LSP_CONFIG_LOAD_INVALID_GLOBAL, status);
	ASSERT_EQ_INT(1, gopls_enabled);
	ASSERT_EQ_INT(1, clangd_enabled);
	ASSERT_EQ_INT(1, html_enabled);
	ASSERT_EQ_INT(1, css_enabled);
	ASSERT_EQ_INT(1, json_enabled);
	ASSERT_EQ_INT(1, eslint_enabled);
	ASSERT_EQ_STR("gopls", gopls_command);
	ASSERT_EQ_STR("go install golang.org/x/tools/gopls@latest", gopls_install_command);
	ASSERT_EQ_STR("clangd", clangd_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-html-language-server --stdio", html_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-css-language-server --stdio", css_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-json-language-server --stdio", json_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-eslint-language-server --stdio", eslint_command);
	ASSERT_EQ_STR("npm install --global --prefix ~/.local vscode-langservers-extracted",
			vscode_langservers_install_command);

	ASSERT_TRUE(write_text_file(global_path,
				"[lsp]\n"
				"clangd_enabled = false\n"
				"html_enabled = false\n"
				"css_enabled = false\n"));
	ASSERT_TRUE(write_text_file(project_path,
				"[lsp]\n"
				"html_command = \"\"\n"));
	status = editorLspConfigLoadFromPaths(&gopls_enabled, &clangd_enabled, &html_enabled,
			&css_enabled, &json_enabled, &eslint_enabled,
			gopls_command, sizeof(gopls_command), gopls_install_command,
			sizeof(gopls_install_command), clangd_command, sizeof(clangd_command),
			html_command, sizeof(html_command), css_command, sizeof(css_command),
			json_command, sizeof(json_command), eslint_command, sizeof(eslint_command),
			vscode_langservers_install_command,
			sizeof(vscode_langservers_install_command), global_path, project_path);
	ASSERT_EQ_INT(EDITOR_LSP_CONFIG_LOAD_INVALID_PROJECT, status);
	ASSERT_EQ_INT(1, gopls_enabled);
	ASSERT_EQ_INT(1, clangd_enabled);
	ASSERT_EQ_INT(1, html_enabled);
	ASSERT_EQ_INT(1, css_enabled);
	ASSERT_EQ_INT(1, json_enabled);
	ASSERT_EQ_INT(1, eslint_enabled);
	ASSERT_EQ_STR("gopls", gopls_command);
	ASSERT_EQ_STR("go install golang.org/x/tools/gopls@latest", gopls_install_command);
	ASSERT_EQ_STR("clangd", clangd_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-html-language-server --stdio", html_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-css-language-server --stdio", css_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-json-language-server --stdio", json_command);
	ASSERT_EQ_STR("~/.local/bin/vscode-eslint-language-server --stdio", eslint_command);
	ASSERT_EQ_STR("npm install --global --prefix ~/.local vscode-langservers-extracted",
			vscode_langservers_install_command);

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(unlink(global_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_lsp_parse_definition_response_handles_clangd_field_order(void) {
	const char *location_response =
			"{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":[{\"range\":{\"start\":{\"character\":5,"
			"\"line\":14},\"end\":{\"character\":23,\"line\":14}},\"uri\":"
			"\"file:///home/mk/Development/rotide/src/editing/edit.h\"}]}";
	struct editorLspLocation *locations = NULL;
	int count = 0;
	ASSERT_TRUE(editorLspTestParseDefinitionResponse(location_response, &locations, &count));
	ASSERT_EQ_INT(1, count);
	ASSERT_TRUE(locations != NULL);
	ASSERT_EQ_STR("/home/mk/Development/rotide/src/editing/edit.h", locations[0].path);
	ASSERT_EQ_INT(14, locations[0].line);
	ASSERT_EQ_INT(5, locations[0].character);
	editorLspFreeLocations(locations, count);

	const char *location_link_response =
			"{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":[{\"targetSelectionRange\":{\"start\":"
			"{\"character\":5,\"line\":6},\"end\":{\"character\":26,\"line\":6}},"
			"\"targetUri\":\"file:///home/mk/Development/rotide/src/input/dispatch.h\","
			"\"targetRange\":{\"start\":{\"character\":0,\"line\":6},\"end\":{\"character\":26,"
			"\"line\":6}}}]}";
	locations = NULL;
	count = 0;
	ASSERT_TRUE(editorLspTestParseDefinitionResponse(location_link_response, &locations, &count));
	ASSERT_EQ_INT(1, count);
	ASSERT_TRUE(locations != NULL);
	ASSERT_EQ_STR("/home/mk/Development/rotide/src/input/dispatch.h", locations[0].path);
	ASSERT_EQ_INT(6, locations[0].line);
	ASSERT_EQ_INT(5, locations[0].character);
	editorLspFreeLocations(locations, count);
	return 0;
}

static int test_editor_lsp_build_initialize_request_json_is_complete(void) {
	char *request = editorLspTestBuildInitializeRequestJson(7, "file:///tmp/project", 1234);
	ASSERT_TRUE(request != NULL);
	ASSERT_TRUE(strstr(request, "\"id\":7") != NULL);
	ASSERT_TRUE(strstr(request, "\"processId\":1234") != NULL);
	ASSERT_TRUE(strstr(request, "\"rootUri\":\"file:///tmp/project\"") != NULL);
	ASSERT_TRUE(strstr(request, "\"source.fixAll.eslint\"") != NULL);
	ASSERT_EQ_INT('}', request[strlen(request) - 1]);
	free(request);
	return 0;
}

static int test_editor_lsp_lifecycle_lazy_start_and_non_go_buffers(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 0;

	char txt_path[64];
	ASSERT_TRUE(write_temp_text_file(txt_path, sizeof(txt_path), "plain text\n"));
	editorOpen(txt_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_NONE, editorSyntaxLanguageActive());

	char goto_def_txt[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def_txt,
			sizeof(goto_def_txt)) == 0);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(0, stats.start_count);
	ASSERT_EQ_INT(0, stats.definition_count);

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
			"package main\n\nfunc main() {\n\tmain()\n}\n"));
	editorOpen(go_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_GO, editorSyntaxLanguageActive());

	editorLspTestSetMockDefinitionResponse(1, NULL, 0);
	char goto_def_go[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def_go,
			sizeof(goto_def_go)) == 0);

	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.start_count);
	ASSERT_EQ_INT(1, stats.definition_count);
	ASSERT_EQ_INT(1, stats.did_open_count);

	ASSERT_TRUE(unlink(go_path) == 0);
	ASSERT_TRUE(unlink(txt_path) == 0);
	return 0;
}

static int test_editor_lsp_lifecycle_restart_after_mock_crash(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 1;

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
			"package main\n\nfunc main() {}\n"));
	editorOpen(go_path);

	editorLspTestSetMockDefinitionResponse(1, NULL, 0);
	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.start_count);
	ASSERT_EQ_INT(1, stats.definition_count);

	editorLspTestSetMockServerAlive(0);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(2, stats.start_count);
	ASSERT_EQ_INT(2, stats.definition_count);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_lsp_lifecycle_restarts_when_switching_between_go_clangd_and_html(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 1;
	E.lsp_html_enabled = 1;

	char go_path[64];
	char c_path[64];
	char html_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
			"package main\n\nfunc helper() {}\nfunc main() { helper() }\n"));
	ASSERT_TRUE(write_temp_c_file(c_path, sizeof(c_path),
			"int helper(void) { return 1; }\nint main(void) { return helper(); }\n"));
	ASSERT_TRUE(write_temp_html_file(html_path, sizeof(html_path),
			"<div id=\"app\"></div>\n<a href=\"#app\">jump</a>\n"));

	editorOpen(go_path);
	E.cy = 3;
	E.cx = 15;
	struct editorLspLocation go_target = {
		.path = go_path,
		.line = 2,
		.character = 5
	};
	editorLspTestSetMockDefinitionResponse(1, &go_target, 1);
	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);

	editorOpen(c_path);
	E.cy = 1;
	E.cx = 24;
	struct editorLspLocation c_target = {
		.path = c_path,
		.line = 0,
		.character = 4
	};
	editorLspTestSetMockDefinitionResponse(1, &c_target, 1);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);

	editorOpen(html_path);
	E.cy = 1;
	E.cx = 11;
	struct editorLspLocation html_target = {
		.path = html_path,
		.line = 0,
		.character = 9
	};
	editorLspTestSetMockDefinitionResponse(1, &html_target, 1);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);

	editorOpen(go_path);
	E.cy = 3;
	E.cx = 15;
	editorLspTestSetMockDefinitionResponse(1, &go_target, 1);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(4, stats.start_count);
	ASSERT_EQ_INT(4, stats.definition_count);

	ASSERT_TRUE(unlink(go_path) == 0);
	ASSERT_TRUE(unlink(c_path) == 0);
	ASSERT_TRUE(unlink(html_path) == 0);
	return 0;
}

static int test_editor_lsp_lifecycle_restarts_when_clangd_workspace_root_changes(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 1;

	char root_template[] = "/tmp/rotide-test-clangd-root-XXXXXX";
	char *root_path = mkdtemp(root_template);
	ASSERT_TRUE(root_path != NULL);

	char project_a[512];
	char project_b[512];
	char marker_a[512];
	char marker_b[512];
	char file_a[512];
	char file_b[512];
	ASSERT_TRUE(path_join(project_a, sizeof(project_a), root_path, "project-a"));
	ASSERT_TRUE(path_join(project_b, sizeof(project_b), root_path, "project-b"));
	ASSERT_TRUE(path_join(marker_a, sizeof(marker_a), project_a, "compile_commands.json"));
	ASSERT_TRUE(path_join(marker_b, sizeof(marker_b), project_b, "compile_commands.json"));
	ASSERT_TRUE(path_join(file_a, sizeof(file_a), project_a, "main.c"));
	ASSERT_TRUE(path_join(file_b, sizeof(file_b), project_b, "main.c"));
	ASSERT_TRUE(make_dir(project_a));
	ASSERT_TRUE(make_dir(project_b));
	ASSERT_TRUE(write_text_file(marker_a, "[]\n"));
	ASSERT_TRUE(write_text_file(marker_b, "[]\n"));
	ASSERT_TRUE(write_text_file(file_a,
				"int helper(void) { return 1; }\nint main(void) { return helper(); }\n"));
	ASSERT_TRUE(write_text_file(file_b,
				"int helper(void) { return 2; }\nint main(void) { return helper(); }\n"));

	struct editorLspLocation target = {
		.path = file_a,
		.line = 0,
		.character = 4
	};
	editorLspTestSetMockDefinitionResponse(1, &target, 1);
	editorOpen(file_a);
	E.cy = 1;
	E.cx = 24;
	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);

	target.path = file_b;
	editorLspTestSetMockDefinitionResponse(1, &target, 1);
	editorOpen(file_b);
	E.cy = 1;
	E.cx = 24;
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(2, stats.start_count);
	ASSERT_EQ_INT(2, stats.definition_count);

	ASSERT_TRUE(unlink(file_a) == 0);
	ASSERT_TRUE(unlink(file_b) == 0);
	ASSERT_TRUE(unlink(marker_a) == 0);
	ASSERT_TRUE(unlink(marker_b) == 0);
	ASSERT_TRUE(rmdir(project_a) == 0);
	ASSERT_TRUE(rmdir(project_b) == 0);
	ASSERT_TRUE(rmdir(root_path) == 0);
	return 0;
}

static int test_editor_lsp_lifecycle_restarts_when_html_workspace_root_changes(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 1;

	char root_template[] = "/tmp/rotide-test-html-root-XXXXXX";
	char *root_path = mkdtemp(root_template);
	ASSERT_TRUE(root_path != NULL);

	char project_a[512];
	char project_b[512];
	char marker_a[512];
	char marker_b[512];
	char file_a[512];
	char file_b[512];
	ASSERT_TRUE(path_join(project_a, sizeof(project_a), root_path, "project-a"));
	ASSERT_TRUE(path_join(project_b, sizeof(project_b), root_path, "project-b"));
	ASSERT_TRUE(path_join(marker_a, sizeof(marker_a), project_a, "package.json"));
	ASSERT_TRUE(path_join(marker_b, sizeof(marker_b), project_b, "package.json"));
	ASSERT_TRUE(path_join(file_a, sizeof(file_a), project_a, "index.html"));
	ASSERT_TRUE(path_join(file_b, sizeof(file_b), project_b, "index.html"));
	ASSERT_TRUE(make_dir(project_a));
	ASSERT_TRUE(make_dir(project_b));
	ASSERT_TRUE(write_text_file(marker_a, "{ }\n"));
	ASSERT_TRUE(write_text_file(marker_b, "{ }\n"));
	ASSERT_TRUE(write_text_file(file_a, "<div id=\"a\"></div>\n<a href=\"#a\">jump</a>\n"));
	ASSERT_TRUE(write_text_file(file_b, "<div id=\"b\"></div>\n<a href=\"#b\">jump</a>\n"));

	struct editorLspLocation target = {
		.path = file_a,
		.line = 0,
		.character = 9
	};
	editorLspTestSetMockDefinitionResponse(1, &target, 1);
	editorOpen(file_a);
	E.cy = 1;
	E.cx = 11;
	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);

	target.path = file_b;
	editorLspTestSetMockDefinitionResponse(1, &target, 1);
	editorOpen(file_b);
	E.cy = 1;
	E.cx = 11;
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(2, stats.start_count);
	ASSERT_EQ_INT(2, stats.definition_count);

	ASSERT_TRUE(unlink(file_a) == 0);
	ASSERT_TRUE(unlink(file_b) == 0);
	ASSERT_TRUE(unlink(marker_a) == 0);
	ASSERT_TRUE(unlink(marker_b) == 0);
	ASSERT_TRUE(rmdir(project_a) == 0);
	ASSERT_TRUE(rmdir(project_b) == 0);
	ASSERT_TRUE(rmdir(root_path) == 0);
	return 0;
}

static int test_editor_lsp_document_sync_for_go_edit_save_close(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 0;
	ASSERT_TRUE(editorTabsInit());

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
			"package main\n\nfunc main() {\n\tprintln(\"ok\")\n}\n"));
	editorOpen(go_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_GO, editorSyntaxLanguageActive());

	E.cy = 0;
	E.cx = 0;
	editorInsertChar('/');

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.did_open_count);
	ASSERT_EQ_INT(1, stats.did_change_count);

	struct editorLspTestLastChange change = {0};
	editorLspTestGetLastChange(&change);
	ASSERT_EQ_INT(1, change.had_range);
	ASSERT_EQ_INT(0, change.start_line);
	ASSERT_EQ_INT(0, change.start_character);
	ASSERT_EQ_INT(2, change.version);

	editorSave();
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.did_save_count);

	ASSERT_TRUE(editorTabCloseActive());
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.did_close_count);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_lsp_document_sync_for_c_edit_save_close(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	char c_path[64];
	ASSERT_TRUE(write_temp_c_file(c_path, sizeof(c_path),
			"int main(void) {\n\treturn 0;\n}\n"));
	editorOpen(c_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());

	E.cy = 0;
	E.cx = 0;
	editorInsertChar('/');

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.did_open_count);
	ASSERT_EQ_INT(1, stats.did_change_count);

	struct editorLspTestLastChange change = {0};
	editorLspTestGetLastChange(&change);
	ASSERT_EQ_INT(1, change.had_range);
	ASSERT_EQ_INT(0, change.start_line);
	ASSERT_EQ_INT(0, change.start_character);
	ASSERT_EQ_INT(2, change.version);

	editorSave();
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.did_save_count);

	ASSERT_TRUE(editorTabCloseActive());
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.did_close_count);

	ASSERT_TRUE(unlink(c_path) == 0);
	return 0;
}

static int test_editor_lsp_document_sync_for_html_edit_save_close(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	char html_path[64];
	ASSERT_TRUE(write_temp_html_file(html_path, sizeof(html_path),
			"<div id=\"app\"></div>\n<a href=\"#app\">jump</a>\n"));
	editorOpen(html_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_HTML, editorSyntaxLanguageActive());

	E.cy = 0;
	E.cx = 0;
	editorInsertChar(' ');

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.did_open_count);
	ASSERT_EQ_INT(1, stats.did_change_count);

	char language_id[32];
	editorLspTestGetLastDidOpenLanguageId(language_id, sizeof(language_id));
	ASSERT_EQ_STR("html", language_id);

	editorSave();
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.did_save_count);

	ASSERT_TRUE(editorTabCloseActive());
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.did_close_count);

	ASSERT_TRUE(unlink(html_path) == 0);
	return 0;
}

static int test_editor_lsp_html_language_id_routing_for_supported_extensions(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 1;

	char html_path[64];
	char htm_path[64];
	char xhtml_path[64];
	ASSERT_TRUE(write_temp_file_with_suffix(html_path, sizeof(html_path),
			"rotide-test-html-route-", ".html", "<div id=\"a\"></div>\n<a href=\"#a\">jump</a>\n"));
	ASSERT_TRUE(write_temp_file_with_suffix(htm_path, sizeof(htm_path),
			"rotide-test-html-route-", ".htm", "<div id=\"b\"></div>\n<a href=\"#b\">jump</a>\n"));
	ASSERT_TRUE(write_temp_file_with_suffix(xhtml_path, sizeof(xhtml_path),
			"rotide-test-html-route-", ".xhtml",
			"<div id=\"c\"></div>\n<a href=\"#c\">jump</a>\n"));

	struct editorLspLocation target = {
		.path = html_path,
		.line = 0,
		.character = 9
	};
	char language_id[32];
	char goto_def[] = {CTRL_KEY('o')};

	editorOpen(html_path);
	E.cy = 1;
	E.cx = 11;
	editorLspTestSetMockDefinitionResponse(1, &target, 1);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	editorLspTestGetLastDidOpenLanguageId(language_id, sizeof(language_id));
	ASSERT_EQ_STR("html", language_id);

	target.path = htm_path;
	editorOpen(htm_path);
	E.cy = 1;
	E.cx = 11;
	editorLspTestSetMockDefinitionResponse(1, &target, 1);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	editorLspTestGetLastDidOpenLanguageId(language_id, sizeof(language_id));
	ASSERT_EQ_STR("html", language_id);

	target.path = xhtml_path;
	editorOpen(xhtml_path);
	E.cy = 1;
	E.cx = 11;
	editorLspTestSetMockDefinitionResponse(1, &target, 1);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	editorLspTestGetLastDidOpenLanguageId(language_id, sizeof(language_id));
	ASSERT_EQ_STR("html", language_id);

	ASSERT_TRUE(unlink(html_path) == 0);
	ASSERT_TRUE(unlink(htm_path) == 0);
	ASSERT_TRUE(unlink(xhtml_path) == 0);
	return 0;
}

static int test_editor_lsp_document_sync_for_css_edit_save_close(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 0;
	E.lsp_css_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	char css_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(css_path, sizeof(css_path),
			"rotide-test-css-lsp-fixture-", ".css",
			"tests/lsp/supported/css/single_file_definition.css"));
	editorOpen(css_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_CSS, editorSyntaxLanguageActive());

	E.cy = 0;
	E.cx = 0;
	editorInsertChar(' ');

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.did_open_count);
	ASSERT_EQ_INT(1, stats.did_change_count);

	char language_id[32];
	editorLspTestGetLastDidOpenLanguageId(language_id, sizeof(language_id));
	ASSERT_EQ_STR("css", language_id);

	editorSave();
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.did_save_count);

	ASSERT_TRUE(editorTabCloseActive());
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.did_close_count);

	ASSERT_TRUE(unlink(css_path) == 0);
	return 0;
}

static int test_editor_lsp_language_id_routing_for_css_scss_and_json(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 0;
	E.lsp_css_enabled = 1;
	E.lsp_json_enabled = 1;

	char css_path[64];
	char scss_path[64];
	char json_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(css_path, sizeof(css_path),
			"rotide-test-css-route-", ".css",
			"tests/lsp/supported/css/single_file_definition.css"));
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(scss_path, sizeof(scss_path),
			"rotide-test-scss-route-", ".scss",
			"tests/lsp/supported/css/single_file_definition.scss"));
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(json_path, sizeof(json_path),
			"rotide-test-json-route-", ".json",
			"tests/lsp/supported/json/single_file_definition.json"));

	struct editorLspLocation target = {
		.path = css_path,
		.line = 0,
		.character = 8
	};
	char language_id[32];
	char goto_def[] = {CTRL_KEY('o')};

	editorOpen(css_path);
	E.cy = 1;
	E.cx = 18;
	editorLspTestSetMockDefinitionResponse(1, &target, 1);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	editorLspTestGetLastDidOpenLanguageId(language_id, sizeof(language_id));
	ASSERT_EQ_STR("css", language_id);

	target.path = scss_path;
	editorOpen(scss_path);
	E.cy = 1;
	E.cx = 18;
	editorLspTestSetMockDefinitionResponse(1, &target, 1);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	editorLspTestGetLastDidOpenLanguageId(language_id, sizeof(language_id));
	ASSERT_EQ_STR("scss", language_id);

	target.path = json_path;
	target.line = 1;
	target.character = 3;
	editorOpen(json_path);
	E.cy = 2;
	E.cx = 11;
	editorLspTestSetMockDefinitionResponse(1, &target, 1);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	editorLspTestGetLastDidOpenLanguageId(language_id, sizeof(language_id));
	ASSERT_EQ_STR("json", language_id);

	ASSERT_TRUE(unlink(css_path) == 0);
	ASSERT_TRUE(unlink(scss_path) == 0);
	ASSERT_TRUE(unlink(json_path) == 0);
	return 0;
}

static int test_editor_process_keypress_ctrl_o_goto_definition_single_location_css_buffer(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 0;
	E.lsp_css_enabled = 1;

	char css_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(css_path, sizeof(css_path),
			"rotide-test-css-lsp-fixture-", ".css",
			"tests/lsp/supported/css/single_file_definition.css"));
	editorOpen(css_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_CSS, editorSyntaxLanguageActive());

	E.cy = 1;
	E.cx = 26;

	struct editorLspLocation target = {
		.path = css_path,
		.line = 0,
		.character = 8
	};
	editorLspTestSetMockDefinitionResponse(1, &target, 1);

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(8, E.cx);

	ASSERT_TRUE(unlink(css_path) == 0);
	return 0;
}

static int test_editor_process_keypress_ctrl_o_goto_definition_single_location_json_buffer(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 0;
	E.lsp_json_enabled = 1;

	char json_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(json_path, sizeof(json_path),
			"rotide-test-json-lsp-fixture-", ".json",
			"tests/lsp/supported/json/single_file_definition.json"));
	editorOpen(json_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_NONE, editorSyntaxLanguageActive());

	E.cy = 2;
	E.cx = 11;

	struct editorLspLocation target = {
		.path = json_path,
		.line = 1,
		.character = 3
	};
	editorLspTestSetMockDefinitionResponse(1, &target, 1);

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(3, E.cx);

	ASSERT_TRUE(unlink(json_path) == 0);
	return 0;
}

static int test_editor_lsp_eslint_diagnostics_update_and_status_summary(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 0;
	E.lsp_eslint_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	char js_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(js_path, sizeof(js_path),
			"rotide-test-js-lsp-fixture-", ".js",
			"tests/lsp/supported/javascript/eslint_buffer.js"));
	editorOpen(js_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_JAVASCRIPT, editorSyntaxLanguageActive());

	E.cy = 0;
	E.cx = 0;
	editorInsertChar(' ');

	struct editorLspDiagnostic diagnostics[2] = {
		{.start_line = 0, .start_character = 0, .end_line = 0, .end_character = 5,
				.severity = 1, .message = "Unexpected space"},
		{.start_line = 1, .start_character = 0, .end_line = 1, .end_character = 11,
				.severity = 2, .message = "Missing semicolon"},
	};
	editorLspTestSetMockDiagnostics(js_path, diagnostics, 2);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_EQ_INT(2, E.lsp_diagnostic_count);
	ASSERT_EQ_INT(1, E.lsp_diagnostic_error_count);
	ASSERT_EQ_INT(1, E.lsp_diagnostic_warning_count);
	ASSERT_TRUE(strstr(E.statusmsg, "ESLint: 1 error, 1 warning") != NULL);
	ASSERT_TRUE(strstr(output, "[E:1 W:1]") != NULL);
	free(output);

	editorLspTestSetMockDiagnostics(js_path, NULL, 0);
	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_EQ_INT(0, E.lsp_diagnostic_count);
	ASSERT_TRUE(strstr(E.statusmsg, "diagnostics cleared") != NULL);
	free(output);

	ASSERT_TRUE(unlink(js_path) == 0);
	return 0;
}

static int test_editor_lsp_eslint_diagnostics_persist_across_tab_switches(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 0;
	E.lsp_eslint_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	char js_path[64];
	char txt_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(js_path, sizeof(js_path),
			"rotide-test-js-lsp-fixture-", ".js",
			"tests/lsp/supported/javascript/eslint_buffer.js"));
	ASSERT_TRUE(write_temp_text_file(txt_path, sizeof(txt_path), "plain text\n"));
	editorOpen(js_path);
	E.cy = 0;
	E.cx = 0;
	editorInsertChar(' ');

	struct editorLspDiagnostic diagnostics[1] = {
		{.start_line = 1, .start_character = 0, .end_line = 1, .end_character = 11,
				.severity = 2, .message = "Missing semicolon"},
	};
	editorLspTestSetMockDiagnostics(js_path, diagnostics, 1);
	editorLspPumpNotifications();
	ASSERT_EQ_INT(1, E.lsp_diagnostic_count);

	ASSERT_TRUE(editorTabOpenFileAsNew(txt_path));
	ASSERT_EQ_STR(txt_path, E.filename);
	ASSERT_EQ_INT(0, E.lsp_diagnostic_count);

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_EQ_STR(js_path, E.filename);
	ASSERT_EQ_INT(1, E.lsp_diagnostic_count);
	ASSERT_EQ_INT(1, E.lsp_diagnostic_warning_count);

	ASSERT_TRUE(unlink(js_path) == 0);
	ASSERT_TRUE(unlink(txt_path) == 0);
	return 0;
}

static int test_editor_process_keypress_eslint_fix_action_applies_mock_edits(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 0;
	E.lsp_eslint_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	E.keymap.bindings[E.keymap.len].key = CTRL_KEY('t');
	E.keymap.bindings[E.keymap.len].action = EDITOR_ACTION_ESLINT_FIX;
	E.keymap.len++;

	char js_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(js_path, sizeof(js_path),
			"rotide-test-js-lsp-fixture-", ".js",
			"tests/lsp/supported/javascript/eslint_buffer.js"));
	editorOpen(js_path);

	struct editorLspDiagnostic edits[1] = {
		{.start_line = 1, .start_character = 16, .end_line = 1, .end_character = 16,
				.message = ";"},
	};
	editorLspTestSetMockCodeActionResult(1, edits, 1);

	char input[] = {CTRL_KEY('t')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);
	ASSERT_TRUE(strstr(E.statusmsg, "ESLint fixes applied") != NULL);

	size_t textlen = 0;
	char *text = editorRowsToStr(&textlen);
	ASSERT_TRUE(text != NULL);
	ASSERT_TRUE(strstr(text, "console.log(foo);") != NULL);
	free(text);

	ASSERT_TRUE(unlink(js_path) == 0);
	return 0;
}

static int test_editor_process_keypress_eslint_fix_missing_vscode_langservers_starts_install_task(void) {
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 0;
	E.lsp_eslint_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	E.keymap.bindings[E.keymap.len].key = CTRL_KEY('t');
	E.keymap.bindings[E.keymap.len].action = EDITOR_ACTION_ESLINT_FIX;
	E.keymap.len++;

	strncpy(E.lsp_eslint_command,
			"exec >/dev/null; sleep 0.05; rotide_missing_vscode_langservers_install_command",
			sizeof(E.lsp_eslint_command) - 1);
	E.lsp_eslint_command[sizeof(E.lsp_eslint_command) - 1] = '\0';
	strncpy(E.lsp_vscode_langservers_install_command, "printf 'install ok\\n'",
			sizeof(E.lsp_vscode_langservers_install_command) - 1);
	E.lsp_vscode_langservers_install_command[
			sizeof(E.lsp_vscode_langservers_install_command) - 1] = '\0';

	char js_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(js_path, sizeof(js_path),
			"rotide-test-js-lsp-fixture-", ".js",
			"tests/lsp/supported/javascript/eslint_buffer.js"));
	editorOpen(js_path);

	char input[] = {CTRL_KEY('t'), 'y', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);
	ASSERT_TRUE(editorTaskIsRunning());
	ASSERT_TRUE(editorActiveTabIsTaskLog());
	ASSERT_EQ_STR("Task: Install vscode-langservers-extracted", editorActiveBufferDisplayName());
	ASSERT_TRUE(wait_for_task_completion_with_timeout(1500));
	ASSERT_EQ_STR("vscode-langservers-extracted installed. Retry Ctrl-O", E.statusmsg);

	ASSERT_TRUE(unlink(js_path) == 0);
	return 0;
}

static int test_editor_lsp_full_document_change_uses_active_source(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 1;

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
			"package main\n\nfunc main() {}\n"));
	editorOpen(go_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_GO, editorSyntaxLanguageActive());

	editorLspTestSetMockDefinitionResponse(1, NULL, 0);
	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);

	ASSERT_TRUE(editorLspNotifyDidChange(E.filename, E.syntax_language,
				&E.lsp_doc_open, &E.lsp_doc_version, NULL, NULL, 0, NULL, 0));

	struct editorLspTestLastChange change = {0};
	editorLspTestGetLastChange(&change);
	ASSERT_EQ_INT(0, change.had_range);
	ASSERT_TRUE(strncmp(change.text, "package main", strlen("package main")) == 0);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_lsp_document_sync_ignores_non_go_buffers(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	char txt_path[64];
	ASSERT_TRUE(write_temp_text_file(txt_path, sizeof(txt_path), "plain text\n"));
	editorOpen(txt_path);

	E.cy = 0;
	E.cx = 0;
	editorInsertChar('x');
	editorSave();
	ASSERT_TRUE(editorTabCloseActive());

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(0, stats.did_open_count);
	ASSERT_EQ_INT(0, stats.did_change_count);
	ASSERT_EQ_INT(0, stats.did_save_count);
	ASSERT_EQ_INT(0, stats.did_close_count);

	ASSERT_TRUE(unlink(txt_path) == 0);
	return 0;
}

static int test_editor_process_keypress_ctrl_o_goto_definition_single_location(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 0;

	char go_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(go_path, sizeof(go_path),
			"rotide-test-go-lsp-fixture-", ".go",
			"tests/lsp/supported/go/single_file_definition.go"));
	editorOpen(go_path);

	E.cy = 5;
	E.cx = 5;

	struct editorLspLocation target = {
		.path = go_path,
		.line = 2,
		.character = 5
	};
	editorLspTestSetMockDefinitionResponse(1, &target, 1);

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(2, E.cy);
	ASSERT_EQ_INT(5, E.cx);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.definition_count);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_process_keypress_ctrl_o_goto_definition_single_location_c_buffer(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 1;

	char c_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(c_path, sizeof(c_path),
			"rotide-test-c-lsp-fixture-", ".c",
			"tests/lsp/supported/c/single_file_definition.c"));
	editorOpen(c_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());

	E.cy = 3;
	E.cx = 14;

	struct editorLspLocation target = {
		.path = c_path,
		.line = 0,
		.character = 4
	};
	editorLspTestSetMockDefinitionResponse(1, &target, 1);

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(4, E.cx);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.definition_count);

	ASSERT_TRUE(unlink(c_path) == 0);
	return 0;
}

static int test_editor_process_keypress_ctrl_o_goto_definition_single_location_cpp_buffer(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 1;

	char cpp_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(cpp_path, sizeof(cpp_path),
			"rotide-test-cpp-lsp-fixture-", ".cpp",
			"tests/lsp/supported/cpp/single_file_definition.cpp"));
	editorOpen(cpp_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());

	E.cy = 3;
	E.cx = 14;

	struct editorLspLocation target = {
		.path = cpp_path,
		.line = 0,
		.character = 4
	};
	editorLspTestSetMockDefinitionResponse(1, &target, 1);

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(4, E.cx);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.definition_count);

	ASSERT_TRUE(unlink(cpp_path) == 0);
	return 0;
}

static int test_editor_process_keypress_ctrl_o_goto_definition_single_location_html_buffer(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 1;

	char html_path[64];
	ASSERT_TRUE(copy_fixture_to_temp_file_with_suffix(html_path, sizeof(html_path),
			"rotide-test-html-lsp-fixture-", ".html",
			"tests/lsp/supported/html/single_file_definition.html"));
	editorOpen(html_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_HTML, editorSyntaxLanguageActive());

	E.cy = 1;
	E.cx = 11;

	struct editorLspLocation target = {
		.path = html_path,
		.line = 0,
		.character = 9
	};
	editorLspTestSetMockDefinitionResponse(1, &target, 1);

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(9, E.cx);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.definition_count);

	char language_id[32];
	editorLspTestGetLastDidOpenLanguageId(language_id, sizeof(language_id));
	ASSERT_EQ_STR("html", language_id);

	ASSERT_TRUE(unlink(html_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_cross_file_reuses_tab(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	char src_path[64];
	char dst_path[64];
	ASSERT_TRUE(write_temp_go_file(src_path, sizeof(src_path),
			"package main\n\nfunc main() { helper() }\n"));
	ASSERT_TRUE(write_temp_go_file(dst_path, sizeof(dst_path),
			"package main\n\nfunc helper() {}\n"));

	editorOpen(src_path);
	E.cy = 2;
	E.cx = 16;

	struct editorLspLocation target = {
		.path = dst_path,
		.line = 2,
		.character = 5
	};
	editorLspTestSetMockDefinitionResponse(1, &target, 1);

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_STR(dst_path, E.filename);

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_EQ_STR(src_path, E.filename);
	editorLspTestSetMockDefinitionResponse(1, &target, 1);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_STR(dst_path, E.filename);

	ASSERT_TRUE(unlink(src_path) == 0);
	ASSERT_TRUE(unlink(dst_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_cross_file_cpp_fixture_reuses_tab(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	char dir_template[] = "/tmp/rotide-test-cpp-lsp-cross-file-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char main_path[512];
	char helper_path[512];
	char header_path[512];
	ASSERT_TRUE(path_join(main_path, sizeof(main_path), dir_path, "main.cpp"));
	ASSERT_TRUE(path_join(helper_path, sizeof(helper_path), dir_path, "helper.cpp"));
	ASSERT_TRUE(path_join(header_path, sizeof(header_path), dir_path, "helper.hpp"));
	ASSERT_TRUE(copy_fixture_to_path(main_path, "tests/lsp/supported/cpp/cross_file/main.cpp"));
	ASSERT_TRUE(copy_fixture_to_path(helper_path, "tests/lsp/supported/cpp/cross_file/helper.cpp"));
	ASSERT_TRUE(copy_fixture_to_path(header_path, "tests/lsp/supported/cpp/cross_file/helper.hpp"));

	editorOpen(main_path);
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());
	E.cy = 3;
	E.cx = 14;

	struct editorLspLocation target = {
		.path = helper_path,
		.line = 2,
		.character = 4
	};
	editorLspTestSetMockDefinitionResponse(1, &target, 1);

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_STR(helper_path, E.filename);

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_EQ_STR(main_path, E.filename);
	editorLspTestSetMockDefinitionResponse(1, &target, 1);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_STR(helper_path, E.filename);

	ASSERT_TRUE(unlink(main_path) == 0);
	ASSERT_TRUE(unlink(helper_path) == 0);
	ASSERT_TRUE(unlink(header_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_multi_picker_selects_choice(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 1;

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
			"package main\n\nfunc a() {}\nfunc b() {}\nfunc main() { a() }\n"));
	editorOpen(go_path);
	E.cy = 4;
	E.cx = 15;

	struct editorLspLocation targets[2] = {
		{.path = go_path, .line = 2, .character = 5},
		{.path = go_path, .line = 3, .character = 5},
	};
	editorLspTestSetMockDefinitionResponse(1, targets, 2);

	char input[] = {CTRL_KEY('o'), '2', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);
	ASSERT_EQ_INT(3, E.cy);
	ASSERT_EQ_INT(5, E.cx);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_process_keypress_mouse_ctrl_click_goto_definition_single_location(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 1;

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
			"package main\n\nfunc helper() {}\nfunc main() { helper() }\n"));
	editorOpen(go_path);
	E.window_rows = 6;
	E.window_cols = 40;
	E.rowoff = 0;
	E.coloff = 0;
	E.cy = 0;
	E.cx = 0;

	struct editorLspLocation target = {
		.path = go_path,
		.line = 2,
		.character = 5
	};
	editorLspTestSetMockDefinitionResponse(1, &target, 1);

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char click[32];
	ASSERT_TRUE(format_sgr_mouse_event(click, sizeof(click), 16, text_start + 16, 5, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input_silent(click, strlen(click)) == 0);
	ASSERT_EQ_INT(2, E.cy);
	ASSERT_EQ_INT(5, E.cx);
	ASSERT_EQ_INT(0, E.mouse_left_button_down);
	ASSERT_EQ_INT(0, E.mouse_drag_started);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(1, stats.definition_count);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_process_keypress_mouse_ctrl_click_goto_definition_multi_picker_selects_choice(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 1;

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
			"package main\n\nfunc a() {}\nfunc b() {}\nfunc main() { a() }\n"));
	editorOpen(go_path);
	E.window_rows = 7;
	E.window_cols = 40;
	E.rowoff = 0;
	E.coloff = 0;

	struct editorLspLocation targets[2] = {
		{.path = go_path, .line = 2, .character = 5},
		{.path = go_path, .line = 3, .character = 5},
	};
	editorLspTestSetMockDefinitionResponse(1, targets, 2);

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char click[32];
	char input[40];
	ASSERT_TRUE(format_sgr_mouse_event(click, sizeof(click), 16, text_start + 16, 6, 'M'));
	int written = snprintf(input, sizeof(input), "%s2\r", click);
	ASSERT_TRUE(written > 0 && (size_t)written < sizeof(input));
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, (size_t)written) == 0);
	ASSERT_EQ_INT(3, E.cy);
	ASSERT_EQ_INT(5, E.cx);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_timeout_error_and_no_result(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 1;

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
			"package main\n\nfunc main() { helper() }\n"));
	editorOpen(go_path);
	E.cy = 2;
	E.cx = 16;

	char goto_def[] = {CTRL_KEY('o')};

	editorLspTestSetMockDefinitionResponse(-2, NULL, 0);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_TRUE(strstr(E.statusmsg, "timed out") != NULL);

	editorLspTestSetMockDefinitionResponse(-1, NULL, 0);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_TRUE(strstr(E.statusmsg, "failed") != NULL);

	editorLspTestSetMockDefinitionResponse(1, NULL, 0);
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_TRUE(strstr(E.statusmsg, "not found") != NULL);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_reports_lsp_disabled(void) {
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 1;

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
			"package main\n\nfunc main() { helper() }\n"));
	editorOpen(go_path);
	E.cy = 2;
	E.cx = 16;

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_STR("gopls is disabled in config", E.statusmsg);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_reports_lsp_disabled_for_c(void) {
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 0;

	char c_path[64];
	ASSERT_TRUE(write_temp_c_file(c_path, sizeof(c_path),
			"int main(void) { return helper(); }\n"));
	editorOpen(c_path);
	E.cy = 0;
	E.cx = 24;

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_STR("clangd is disabled in config", E.statusmsg);

	ASSERT_TRUE(unlink(c_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_reports_lsp_disabled_for_html(void) {
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 1;
	E.lsp_html_enabled = 0;

	char html_path[64];
	ASSERT_TRUE(write_temp_html_file(html_path, sizeof(html_path),
			"<div id=\"app\"></div>\n<a href=\"#app\">jump</a>\n"));
	editorOpen(html_path);
	E.cy = 1;
	E.cx = 11;

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_STR("vscode-html-language-server is disabled in config", E.statusmsg);

	ASSERT_TRUE(unlink(html_path) == 0);
	return 0;
}

static int test_editor_process_keypress_mouse_ctrl_click_goto_definition_reports_lsp_disabled(void) {
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	add_row("plain text");
	E.window_rows = 4;
	E.window_cols = 40;
	E.rowoff = 0;
	E.coloff = 0;
	E.syntax_language = EDITOR_SYNTAX_NONE;

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char click[32];
	ASSERT_TRUE(format_sgr_mouse_event(click, sizeof(click), 16, text_start + 4, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input_silent(click, strlen(click)) == 0);
	ASSERT_TRUE(strstr(E.statusmsg, "Go to definition is available for Go, C, C++, HTML") != NULL);

	struct editorLspTestStats stats = {0};
	editorLspTestGetStats(&stats);
	ASSERT_EQ_INT(0, stats.definition_count);
	return 0;
}

static int test_editor_process_keypress_goto_definition_requires_saved_c_buffer(void) {
	add_row("int main(void) { return helper(); }");
	E.window_rows = 4;
	E.window_cols = 40;
	E.rowoff = 0;
	E.coloff = 0;
	E.syntax_language = EDITOR_SYNTAX_C;
	free(E.filename);
	E.filename = NULL;

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_STR("Save this C/C++ buffer before using go to definition", E.statusmsg);
	return 0;
}

static int test_editor_process_keypress_goto_definition_reports_empty_clangd_command(void) {
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 1;
	E.lsp_clangd_command[0] = '\0';

	char c_path[64];
	ASSERT_TRUE(write_temp_c_file(c_path, sizeof(c_path),
			"int main(void) { return helper(); }\n"));
	editorOpen(c_path);
	E.cy = 0;
	E.cx = 24;

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_STR("LSP disabled: [lsp].clangd_command is empty", E.statusmsg);

	ASSERT_TRUE(unlink(c_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_reports_empty_html_command(void) {
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 1;
	E.lsp_html_enabled = 1;
	E.lsp_html_command[0] = '\0';

	char html_path[64];
	ASSERT_TRUE(write_temp_html_file(html_path, sizeof(html_path),
			"<div id=\"app\"></div>\n<a href=\"#app\">jump</a>\n"));
	editorOpen(html_path);
	E.cy = 1;
	E.cx = 11;

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_EQ_STR("LSP disabled: [lsp].html_command is empty", E.statusmsg);

	ASSERT_TRUE(unlink(html_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_startup_failure_reports_reason(void) {
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 1;

	strncpy(E.lsp_gopls_command, "true", sizeof(E.lsp_gopls_command) - 1);
	E.lsp_gopls_command[sizeof(E.lsp_gopls_command) - 1] = '\0';

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
			"package main\n\nfunc main() { helper() }\n"));
	editorOpen(go_path);
	E.cy = 2;
	E.cx = 16;

	char goto_def[] = {CTRL_KEY('o')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(goto_def, sizeof(goto_def)) == 0);
	ASSERT_TRUE(strstr(E.statusmsg, "LSP startup failed") != NULL);
	ASSERT_TRUE(strstr(E.statusmsg, "unavailable for this file") == NULL);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_process_keypress_mouse_ctrl_click_goto_definition_requires_saved_go_buffer(void) {
	add_row("package main");
	add_row("");
	add_row("func main() { helper() }");
	E.window_rows = 5;
	E.window_cols = 40;
	E.rowoff = 0;
	E.coloff = 0;
	E.syntax_language = EDITOR_SYNTAX_GO;
	free(E.filename);
	E.filename = NULL;

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char click[32];
	ASSERT_TRUE(format_sgr_mouse_event(click, sizeof(click), 16, text_start + 16, 4, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input_silent(click, strlen(click)) == 0);
	ASSERT_EQ_STR("Save this Go buffer before using go to definition", E.statusmsg);
	return 0;
}

static int test_editor_process_keypress_goto_definition_missing_gopls_decline_install(void) {
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 0;
	ASSERT_TRUE(editorTabsInit());

	strncpy(E.lsp_gopls_command,
			"exec >/dev/null; sleep 0.05; rotide_missing_gopls_decline_command",
			sizeof(E.lsp_gopls_command) - 1);
	E.lsp_gopls_command[sizeof(E.lsp_gopls_command) - 1] = '\0';
	strncpy(E.lsp_gopls_install_command, "printf 'install skipped\\n'",
			sizeof(E.lsp_gopls_install_command) - 1);
	E.lsp_gopls_install_command[sizeof(E.lsp_gopls_install_command) - 1] = '\0';

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
			"package main\n\nfunc main() { helper() }\n"));
	editorOpen(go_path);
	E.cy = 2;
	E.cx = 16;

	char input[] = {CTRL_KEY('o'), '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);
	ASSERT_EQ_STR("gopls not installed", E.statusmsg);
	ASSERT_TRUE(!editorTaskIsRunning());
	ASSERT_EQ_INT(1, editorTabCount());
	ASSERT_TRUE(!editorActiveTabIsTaskLog());

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_missing_gopls_starts_install_task(void) {
	E.lsp_gopls_enabled = 1;
	E.lsp_clangd_enabled = 0;
	ASSERT_TRUE(editorTabsInit());

	strncpy(E.lsp_gopls_command,
			"exec >/dev/null; sleep 0.05; rotide_missing_gopls_install_command",
			sizeof(E.lsp_gopls_command) - 1);
	E.lsp_gopls_command[sizeof(E.lsp_gopls_command) - 1] = '\0';
	strncpy(E.lsp_gopls_install_command, "printf 'install ok\\n'",
			sizeof(E.lsp_gopls_install_command) - 1);
	E.lsp_gopls_install_command[sizeof(E.lsp_gopls_install_command) - 1] = '\0';

	char go_path[64];
	ASSERT_TRUE(write_temp_go_file(go_path, sizeof(go_path),
			"package main\n\nfunc main() { helper() }\n"));
	editorOpen(go_path);
	E.cy = 2;
	E.cx = 16;

	char input[] = {CTRL_KEY('o'), 'y', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);
	ASSERT_TRUE(editorTaskIsRunning());
	ASSERT_TRUE(editorActiveTabIsTaskLog());
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_STR("Task: Install gopls", editorActiveBufferDisplayName());
	ASSERT_TRUE(wait_for_task_completion_with_timeout(1500));
	ASSERT_EQ_STR("gopls installed. Retry Ctrl-O", E.statusmsg);

	size_t textlen = 0;
	char *text = editorRowsToStr(&textlen);
	ASSERT_TRUE(text != NULL);
	ASSERT_TRUE(strstr(text, "install ok") != NULL);
	free(text);

	ASSERT_TRUE(unlink(go_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_missing_clangd_declines_instructions(void) {
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	strncpy(E.lsp_clangd_command,
			"exec >/dev/null; sleep 0.05; rotide_missing_clangd_command",
			sizeof(E.lsp_clangd_command) - 1);
	E.lsp_clangd_command[sizeof(E.lsp_clangd_command) - 1] = '\0';

	char c_path[64];
	ASSERT_TRUE(write_temp_c_file(c_path, sizeof(c_path),
			"int helper(void) { return 1; }\nint main(void) { return helper(); }\n"));
	editorOpen(c_path);
	E.cy = 1;
	E.cx = 27;

	char input[] = {CTRL_KEY('o'), '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);
	ASSERT_TRUE(!editorTaskIsRunning());
	ASSERT_TRUE(!editorActiveTabIsTaskLog());
	ASSERT_EQ_INT(1, editorTabCount());
	ASSERT_EQ_STR("clangd not installed", E.statusmsg);

	ASSERT_TRUE(unlink(c_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_missing_clangd_shows_install_instructions(void) {
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	strncpy(E.lsp_clangd_command,
			"exec >/dev/null; sleep 0.05; rotide_missing_clangd_command",
			sizeof(E.lsp_clangd_command) - 1);
	E.lsp_clangd_command[sizeof(E.lsp_clangd_command) - 1] = '\0';

	char c_path[64];
	ASSERT_TRUE(write_temp_c_file(c_path, sizeof(c_path),
			"int helper(void) { return 1; }\nint main(void) { return helper(); }\n"));
	editorOpen(c_path);
	E.cy = 1;
	E.cx = 27;

	char input[] = {CTRL_KEY('o'), 'y', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);
	ASSERT_TRUE(!editorTaskIsRunning());
	ASSERT_TRUE(editorActiveTabIsTaskLog());
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_STR("Task: Install clangd", editorActiveBufferDisplayName());
	ASSERT_EQ_STR("clangd not installed; see task log", E.statusmsg);

	size_t textlen = 0;
	char *text = editorRowsToStr(&textlen);
	ASSERT_TRUE(text != NULL);
	ASSERT_TRUE(strstr(text, "clangd was not found on PATH") != NULL);
	ASSERT_TRUE(strstr(text, "https://clangd.llvm.org/installation") != NULL);
	ASSERT_TRUE(strstr(text, "compile_commands.json") != NULL);
	ASSERT_TRUE(strstr(text, "cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON") != NULL);
	ASSERT_TRUE(strstr(text, "bear -- make") != NULL);
	ASSERT_TRUE(strstr(text, "[lsp].clangd_command") != NULL);
	free(text);

	ASSERT_TRUE(unlink(c_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_missing_vscode_langservers_decline_install(void) {
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	strncpy(E.lsp_html_command,
			"exec >/dev/null; sleep 0.05; rotide_missing_vscode_langservers_command",
			sizeof(E.lsp_html_command) - 1);
	E.lsp_html_command[sizeof(E.lsp_html_command) - 1] = '\0';
	strncpy(E.lsp_vscode_langservers_install_command, "printf 'install skipped\\n'",
			sizeof(E.lsp_vscode_langservers_install_command) - 1);
	E.lsp_vscode_langservers_install_command[
			sizeof(E.lsp_vscode_langservers_install_command) - 1] = '\0';

	char html_path[64];
	ASSERT_TRUE(write_temp_html_file(html_path, sizeof(html_path),
			"<div id=\"app\"></div>\n<a href=\"#app\">jump</a>\n"));
	editorOpen(html_path);
	E.cy = 1;
	E.cx = 11;

	char input[] = {CTRL_KEY('o'), '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);
	ASSERT_EQ_STR("vscode-langservers-extracted not installed", E.statusmsg);
	ASSERT_TRUE(!editorTaskIsRunning());
	ASSERT_EQ_INT(1, editorTabCount());
	ASSERT_TRUE(!editorActiveTabIsTaskLog());

	ASSERT_TRUE(unlink(html_path) == 0);
	return 0;
}

static int test_editor_process_keypress_goto_definition_missing_vscode_langservers_starts_install_task(void) {
	E.lsp_gopls_enabled = 0;
	E.lsp_clangd_enabled = 0;
	E.lsp_html_enabled = 1;
	ASSERT_TRUE(editorTabsInit());

	strncpy(E.lsp_html_command,
			"exec >/dev/null; sleep 0.05; rotide_missing_vscode_langservers_install_command",
			sizeof(E.lsp_html_command) - 1);
	E.lsp_html_command[sizeof(E.lsp_html_command) - 1] = '\0';
	strncpy(E.lsp_vscode_langservers_install_command, "printf 'install ok\\n'",
			sizeof(E.lsp_vscode_langservers_install_command) - 1);
	E.lsp_vscode_langservers_install_command[
			sizeof(E.lsp_vscode_langservers_install_command) - 1] = '\0';

	char html_path[64];
	ASSERT_TRUE(write_temp_html_file(html_path, sizeof(html_path),
			"<div id=\"app\"></div>\n<a href=\"#app\">jump</a>\n"));
	editorOpen(html_path);
	E.cy = 1;
	E.cx = 11;

	char input[] = {CTRL_KEY('o'), 'y', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);
	ASSERT_TRUE(editorTaskIsRunning());
	ASSERT_TRUE(editorActiveTabIsTaskLog());
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_STR("Task: Install vscode-langservers-extracted", editorActiveBufferDisplayName());
	ASSERT_TRUE(wait_for_task_completion_with_timeout(1500));
	ASSERT_EQ_STR("vscode-langservers-extracted installed. Retry Ctrl-O", E.statusmsg);

	size_t textlen = 0;
	char *text = editorRowsToStr(&textlen);
	ASSERT_TRUE(text != NULL);
	ASSERT_TRUE(strstr(text, "install ok") != NULL);
	free(text);

	ASSERT_TRUE(unlink(html_path) == 0);
	return 0;
}

static int test_editor_task_log_read_only_search_and_copy(void) {
	ASSERT_TRUE(editorTabsInit());
	editorDocumentTestResetStats();
	ASSERT_TRUE(editorTaskStart("Task: Echo", "printf 'alpha\\nbeta\\n'", NULL, NULL));
	ASSERT_TRUE(wait_for_task_completion_with_timeout(1500));
	ASSERT_TRUE(editorActiveTabIsTaskLog());
	ASSERT_EQ_STR("Task: Echo", editorActiveBufferDisplayName());
	ASSERT_TRUE(E.document != NULL);
	ASSERT_TRUE(editorDocumentTestFullRebuildCount() > 0);

	editorDocumentTestResetStats();
	editorActiveTextSourceDupTestResetCount();
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(0, editorActiveTextSourceDupTestCount());

	size_t before_len = 0;
	char *before = editorRowsToStr(&before_len);
	ASSERT_TRUE(before != NULL);

	char insert[] = {'x'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(insert, sizeof(insert)) == 0);
	ASSERT_EQ_STR("Task log is read-only", E.statusmsg);

	size_t after_len = 0;
	char *after = editorRowsToStr(&after_len);
	ASSERT_TRUE(after != NULL);
	ASSERT_EQ_INT((int)before_len, (int)after_len);
	ASSERT_MEM_EQ(before, after, before_len);
	free(before);
	free(after);

	char save[] = {CTRL_KEY('s')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(save, sizeof(save)) == 0);
	ASSERT_EQ_STR("Task logs cannot be saved", E.statusmsg);

	char find_input[] = {CTRL_KEY('f'), 'c', 'o', 'm', 'p', 'l', 'e', 't', 'e', 'd', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(find_input, sizeof(find_input)) == 0);
	ASSERT_TRUE(E.search_match_len > 0);

	editorActiveTextSourceDupTestResetCount();
	E.cy = 3;
	E.cx = 4;
	E.selection_mode_active = 1;
	ASSERT_TRUE(set_selection_anchor(3, 0));
	char copy[] = {CTRL_KEY('c')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(copy, sizeof(copy)) == 0);

	size_t copied_len = 0;
	const char *copied = editorClipboardGet(&copied_len);
	ASSERT_EQ_INT(4, (int)copied_len);
	ASSERT_MEM_EQ("beta", copied, copied_len);
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	ASSERT_EQ_INT(0, editorActiveTextSourceDupTestCount());
	return 0;
}

static int test_editor_task_log_document_stays_authoritative(void) {
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorTaskStart("Task: Echo", "printf 'alpha\\nbeta\\n'", NULL, NULL));
	ASSERT_TRUE(wait_for_task_completion_with_timeout(1500));
	ASSERT_TRUE(editorActiveTabIsTaskLog());
	ASSERT_TRUE(E.document != NULL);

	editorDocumentTestResetStats();
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());
	ASSERT_EQ_INT(0, editorDocumentTestFullRebuildCount());
	return 0;
}

static int test_editor_task_log_streams_output_while_inactive(void) {
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorTaskStart("Task: Background",
			"printf 'alpha\\n'; sleep 0.1; printf 'beta\\n'", NULL, NULL));
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_TRUE(editorActiveTabIsTaskLog());
	ASSERT_TRUE(editorTabNewEmpty());
	ASSERT_EQ_INT(3, editorTabCount());
	ASSERT_TRUE(!editorActiveTabIsTaskLog());

	ASSERT_TRUE(wait_for_task_completion_with_timeout(1500));
	ASSERT_TRUE(editorTabSwitchToIndex(1));
	ASSERT_TRUE(editorActiveTabIsTaskLog());
	ASSERT_EQ_STR("Task: Background", editorActiveBufferDisplayName());
	ASSERT_TRUE(E.document != NULL);

	size_t textlen = 0;
	char *text = editorRowsToStr(&textlen);
	ASSERT_TRUE(text != NULL);
	ASSERT_TRUE(strstr(text, "alpha") != NULL);
	ASSERT_TRUE(strstr(text, "beta") != NULL);
	free(text);
	return 0;
}

static int test_editor_task_runner_merges_stderr_and_close_requires_confirmation(void) {
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorTaskStart("Task: Mixed",
			"printf 'out\\n'; printf 'err\\n' 1>&2; sleep 1", NULL, NULL));
	ASSERT_TRUE(editorTaskIsRunning());

	char close_once[] = {CTRL_KEY('w')};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(close_once, sizeof(close_once)) == 0);
	ASSERT_TRUE(strstr(E.statusmsg, "Task is still running") != NULL);
	ASSERT_TRUE(editorTaskIsRunning());
	ASSERT_EQ_INT(2, editorTabCount());

	ASSERT_TRUE(editor_process_keypress_with_input_silent(close_once, sizeof(close_once)) == 0);
	ASSERT_TRUE(!editorTaskIsRunning());
	ASSERT_EQ_INT(1, editorTabCount());
	ASSERT_TRUE(!editorActiveTabIsTaskLog());

	ASSERT_TRUE(editorTaskStart("Task: Mixed Output",
			"printf 'out\\n'; printf 'err\\n' 1>&2", NULL, NULL));
	ASSERT_TRUE(wait_for_task_completion_with_timeout(1500));
	size_t textlen = 0;
	char *text = editorRowsToStr(&textlen);
	ASSERT_TRUE(text != NULL);
	ASSERT_TRUE(strstr(text, "out") != NULL);
	ASSERT_TRUE(strstr(text, "err") != NULL);
	free(text);
	return 0;
}

static int test_editor_task_runner_truncates_large_output(void) {
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorTaskStart("Task: Large Output",
			"yes 1234567890 | head -c 150000", NULL, NULL));
	ASSERT_TRUE(wait_for_task_completion_with_timeout(3000));

	size_t textlen = 0;
	char *text = editorRowsToStr(&textlen);
	ASSERT_TRUE(text != NULL);
	ASSERT_TRUE(textlen <= ROTIDE_TASK_LOG_MAX_BYTES + 256);
	ASSERT_TRUE(strstr(text, "[output truncated]") != NULL);
	free(text);
	return 0;
}

static int test_editor_process_keypress_keymap_remap_changes_dispatch(void) {
	char dir_template[] = "/tmp/rotide-test-keymap-dispatch-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path,
				"[keymap]\n"
				"save = \"ctrl+a\"\n"
				"redraw = \"ctrl+s\"\n"));

	enum editorKeymapLoadStatus status = editorKeymapLoadFromPaths(&E.keymap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_OK, status);

	char save_path[] = "/tmp/rotide-test-keymap-dispatch-save-XXXXXX";
	int fd = mkstemp(save_path);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(close(fd) == 0);

	add_row("line1");
	E.filename = strdup(save_path);
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 1;

	char ctrl_s[] = {CTRL_KEY('s')};
	ASSERT_TRUE(editor_process_keypress_with_input(ctrl_s, sizeof(ctrl_s)) == 0);
	ASSERT_EQ_INT(1, E.dirty);

	size_t first_read_len = 0;
	char *first_contents = read_file_contents(save_path, &first_read_len);
	ASSERT_TRUE(first_contents != NULL);
	ASSERT_EQ_INT(0, first_read_len);
	free(first_contents);

	char ctrl_a[] = {CTRL_KEY('a')};
	ASSERT_TRUE(editor_process_keypress_with_input(ctrl_a, sizeof(ctrl_a)) == 0);
	ASSERT_EQ_INT(0, E.dirty);

	size_t second_read_len = 0;
	char *second_contents = read_file_contents(save_path, &second_read_len);
	ASSERT_TRUE(second_contents != NULL);
	ASSERT_MEM_EQ("line1\n", second_contents, second_read_len);
	free(second_contents);

	ASSERT_TRUE(unlink(save_path) == 0);
	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_process_keypress_keymap_ctrl_alt_letter_dispatches_mapped_action(void) {
	char dir_template[] = "/tmp/rotide-test-keymap-ctrl-alt-dispatch-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);

	char project_path[512];
	ASSERT_TRUE(path_join(project_path, sizeof(project_path), dir_path, ".rotide.toml"));
	ASSERT_TRUE(write_text_file(project_path,
				"[keymap]\n"
				"new_tab = \"ctrl+alt+n\"\n"));

	enum editorKeymapLoadStatus status = editorKeymapLoadFromPaths(&E.keymap, NULL, project_path);
	ASSERT_EQ_INT(EDITOR_KEYMAP_LOAD_OK, status);
	ASSERT_TRUE(editorTabsInit());
	ASSERT_EQ_INT(1, editorTabCount());

	char input[] = {'\x1b', CTRL_KEY('n')};
	ASSERT_TRUE(editor_process_keypress_with_input(input, sizeof(input)) == 0);
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());

	ASSERT_TRUE(unlink(project_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_editor_process_keypress_resize_drawer_shortcuts(void) {
	E.window_cols = 40;
	E.drawer_width_cols = 10;

	const char alt_shift_right[] = "\x1b[1;4C";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_right,
				sizeof(alt_shift_right) - 1) == 0);
	ASSERT_EQ_INT(11, editorDrawerWidthForCols(E.window_cols));

	const char alt_shift_left[] = "\x1b[1;4D";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_left,
				sizeof(alt_shift_left) - 1) == 0);
	ASSERT_EQ_INT(10, editorDrawerWidthForCols(E.window_cols));

	E.pane_focus = EDITOR_PANE_DRAWER;
	ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_left,
				sizeof(alt_shift_left) - 1) == 0);
	ASSERT_EQ_INT(9, editorDrawerWidthForCols(E.window_cols));

	E.drawer_width_cols = 1;
	ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_left,
				sizeof(alt_shift_left) - 1) == 0);
	ASSERT_EQ_INT(1, editorDrawerWidthForCols(E.window_cols));
	return 0;
}

static int test_editor_process_keypress_toggle_drawer_shortcut_collapses_and_expands(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));

	char toggle_drawer[] = {'\x1b', CTRL_KEY('e')};
	ASSERT_TRUE(editor_process_keypress_with_input(toggle_drawer, sizeof(toggle_drawer)) == 0);
	ASSERT_TRUE(editorDrawerIsCollapsed());
	ASSERT_EQ_INT(EDITOR_PANE_TEXT, E.pane_focus);
	ASSERT_EQ_STR("Drawer collapsed", E.statusmsg);

	ASSERT_TRUE(editor_process_keypress_with_input(toggle_drawer, sizeof(toggle_drawer)) == 0);
	ASSERT_TRUE(!editorDrawerIsCollapsed());
	ASSERT_EQ_INT(EDITOR_PANE_DRAWER, E.pane_focus);
	ASSERT_EQ_STR("Drawer expanded", E.statusmsg);

	ASSERT_TRUE(editorDrawerSetCollapsed(1));
	E.pane_focus = EDITOR_PANE_TEXT;
	char focus_drawer[] = {CTRL_KEY('e')};
	ASSERT_TRUE(editor_process_keypress_with_input(focus_drawer, sizeof(focus_drawer)) == 0);
	ASSERT_TRUE(!editorDrawerIsCollapsed());
	ASSERT_EQ_INT(EDITOR_PANE_DRAWER, E.pane_focus);
	ASSERT_EQ_STR("Drawer expanded", E.statusmsg);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_tabs_switch_restores_per_tab_state(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("tab-zero");
	E.cx = 4;
	E.cy = 0;
	E.search_query = strdup("zero");
	ASSERT_TRUE(E.search_query != NULL);

	ASSERT_TRUE(editorTabNewEmpty());
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_INT(0, E.numrows);

	add_row("tab-one");
	E.cx = 2;
	E.cy = 0;
	free(E.search_query);
	E.search_query = strdup("one");
	ASSERT_TRUE(E.search_query != NULL);

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("tab-zero", E.rows[0].chars);
	ASSERT_EQ_INT(4, E.cx);
	ASSERT_TRUE(E.search_query != NULL);
	ASSERT_EQ_STR("zero", E.search_query);

	ASSERT_TRUE(editorTabSwitchToIndex(1));
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("tab-one", E.rows[0].chars);
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_TRUE(E.search_query != NULL);
	ASSERT_EQ_STR("one", E.search_query);
	return 0;
}

static int test_editor_tab_close_last_tab_keeps_one_empty_tab(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("x");
	E.dirty = 1;
	E.filename = strdup("dirty.txt");
	ASSERT_TRUE(E.filename != NULL);

	ASSERT_TRUE(editorTabCloseActive());
	ASSERT_EQ_INT(1, editorTabCount());
	ASSERT_EQ_INT(0, editorTabActiveIndex());
	ASSERT_EQ_INT(0, E.numrows);
	ASSERT_EQ_INT(0, E.dirty);
	ASSERT_TRUE(E.filename == NULL);
	return 0;
}

static int test_editor_process_keypress_ctrl_w_dirty_requires_second_press(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("dirty");
	E.dirty = 1;

	char ctrl_w[] = {CTRL_KEY('w')};
	ASSERT_TRUE(editor_process_keypress_with_input(ctrl_w, sizeof(ctrl_w)) == 0);
	ASSERT_EQ_INT(1, editorTabCount());
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_TRUE(strstr(E.statusmsg, "unsaved changes") != NULL);

	ASSERT_TRUE(editor_process_keypress_with_input(ctrl_w, sizeof(ctrl_w)) == 0);
	ASSERT_EQ_INT(1, editorTabCount());
	ASSERT_EQ_INT(0, E.numrows);
	ASSERT_EQ_INT(0, E.dirty);
	return 0;
}

static int test_editor_process_keypress_close_tab_confirmation_resets_on_other_action(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("dirty");
	E.dirty = 1;

	char ctrl_w[] = {CTRL_KEY('w')};
	ASSERT_TRUE(editor_process_keypress_with_input(ctrl_w, sizeof(ctrl_w)) == 0);
	ASSERT_EQ_INT(1, E.numrows);

	const char move_right[] = "\x1b[C";
	ASSERT_TRUE(editor_process_keypress_with_input(move_right, sizeof(move_right) - 1) == 0);

	ASSERT_TRUE(editor_process_keypress_with_input(ctrl_w, sizeof(ctrl_w)) == 0);
	ASSERT_EQ_INT(1, E.numrows);

	ASSERT_TRUE(editor_process_keypress_with_input(ctrl_w, sizeof(ctrl_w)) == 0);
	ASSERT_EQ_INT(0, E.numrows);
	return 0;
}

static int test_editor_process_keypress_ctrl_q_checks_dirty_tabs_globally(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("dirty-first-tab");
	E.dirty = 1;
	ASSERT_TRUE(editorTabNewEmpty());
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_INT(0, E.dirty);

	char ctrl_q[] = {CTRL_KEY('q')};
	ASSERT_TRUE(editor_process_keypress_with_input(ctrl_q, sizeof(ctrl_q)) == 0);
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_TRUE(strstr(E.statusmsg, "unsaved changes") != NULL);
	return 0;
}

static int test_editor_process_keypress_tab_actions_new_next_prev(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("left");

	char ctrl_n[] = {CTRL_KEY('n')};
	ASSERT_TRUE(editor_process_keypress_with_input(ctrl_n, sizeof(ctrl_n)) == 0);
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_INT(0, E.numrows);

	add_row("right");
	const char alt_left[] = "\x1b[1;3D";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_left, sizeof(alt_left) - 1) == 0);
	ASSERT_EQ_INT(0, editorTabActiveIndex());
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("left", E.rows[0].chars);

	const char alt_right_fallback[] = "\x1b\x1b[C";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_right_fallback,
				sizeof(alt_right_fallback) - 1) == 0);
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("right", E.rows[0].chars);
	return 0;
}

static int test_editor_tab_open_file_reuses_active_clean_empty_buffer(void) {
	char open_file[64];
	ASSERT_TRUE(write_temp_text_file(open_file, sizeof(open_file), "opened\n"));

	ASSERT_TRUE(editorTabsInit());
	ASSERT_EQ_INT(1, editorTabCount());
	ASSERT_EQ_INT(0, editorTabActiveIndex());
	ASSERT_EQ_INT(0, E.numrows);
	ASSERT_EQ_INT(0, E.dirty);
	ASSERT_TRUE(E.filename == NULL);

	ASSERT_TRUE(editorTabOpenFileAsNew(open_file));
	ASSERT_EQ_INT(1, editorTabCount());
	ASSERT_EQ_INT(0, editorTabActiveIndex());
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR(open_file, E.filename);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("opened", E.rows[0].chars);

	ASSERT_TRUE(unlink(open_file) == 0);
	return 0;
}

static int test_editor_tab_open_file_opens_new_tab_when_empty_buffer_is_inactive(void) {
	char open_file[64];
	ASSERT_TRUE(write_temp_text_file(open_file, sizeof(open_file), "opened\n"));

	ASSERT_TRUE(editorTabsInit());
	ASSERT_EQ_INT(1, editorTabCount());
	ASSERT_EQ_INT(0, editorTabActiveIndex());
	ASSERT_EQ_INT(0, E.numrows);

	// Leave tab 0 as a clean empty buffer, then make tab 1 active and non-empty.
	ASSERT_TRUE(editorTabNewEmpty());
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	add_row("keep");

	ASSERT_TRUE(editorTabOpenFileAsNew(open_file));
	ASSERT_EQ_INT(3, editorTabCount());
	ASSERT_EQ_INT(2, editorTabActiveIndex());
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR(open_file, E.filename);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("opened", E.rows[0].chars);

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_TRUE(E.filename == NULL);
	ASSERT_EQ_INT(0, E.numrows);

	ASSERT_TRUE(editorTabSwitchToIndex(1));
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("keep", E.rows[0].chars);

	ASSERT_TRUE(unlink(open_file) == 0);
	return 0;
}

static int test_editor_process_keypress_focus_drawer_and_arrow_navigation(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char src_dir[512];
	char child_file[512];
	ASSERT_TRUE(path_join(src_dir, sizeof(src_dir), env.project_dir, "src"));
	ASSERT_TRUE(path_join(child_file, sizeof(child_file), src_dir, "child.txt"));
	ASSERT_TRUE(make_dir(src_dir));
	ASSERT_TRUE(write_text_file(child_file, "child\n"));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows));

	add_row("line");
	E.cy = 0;
	E.cx = 2;
	int initial_cy = E.cy;
	int initial_cx = E.cx;

	char focus_drawer[] = {CTRL_KEY('e')};
	ASSERT_TRUE(editor_process_keypress_with_input(focus_drawer, sizeof(focus_drawer)) == 0);
	ASSERT_EQ_INT(EDITOR_PANE_DRAWER, E.pane_focus);

	const char arrow_down[] = "\x1b[B";
	ASSERT_TRUE(editor_process_keypress_with_input(arrow_down, sizeof(arrow_down) - 1) == 0);
	ASSERT_TRUE(E.drawer_selected_index > 0);
	ASSERT_EQ_INT(initial_cy, E.cy);
	ASSERT_EQ_INT(initial_cx, E.cx);

	int src_idx = -1;
	ASSERT_TRUE(find_drawer_entry("src", &src_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(src_idx, E.window_rows));
	int collapsed_count = editorDrawerVisibleCount();

	const char arrow_right[] = "\x1b[C";
	ASSERT_TRUE(editor_process_keypress_with_input(arrow_right, sizeof(arrow_right) - 1) == 0);
	ASSERT_TRUE(editorDrawerVisibleCount() > collapsed_count);
	ASSERT_TRUE(find_drawer_entry("child.txt", NULL, NULL));

	const char arrow_left[] = "\x1b[D";
	ASSERT_TRUE(editor_process_keypress_with_input(arrow_left, sizeof(arrow_left) - 1) == 0);
	ASSERT_EQ_INT(collapsed_count, editorDrawerVisibleCount());

	const char esc_input[] = "\x1b[x";
	ASSERT_TRUE(editor_process_keypress_with_input_silent(esc_input, sizeof(esc_input) - 1) == 0);
	ASSERT_EQ_INT(EDITOR_PANE_TEXT, E.pane_focus);

	ASSERT_TRUE(unlink(child_file) == 0);
	ASSERT_TRUE(rmdir(src_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_drawer_enter_toggles_directory(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char src_dir[512];
	char child_file[512];
	ASSERT_TRUE(path_join(src_dir, sizeof(src_dir), env.project_dir, "src"));
	ASSERT_TRUE(path_join(child_file, sizeof(child_file), src_dir, "child.txt"));
	ASSERT_TRUE(make_dir(src_dir));
	ASSERT_TRUE(write_text_file(child_file, "child\n"));

	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));

	int src_idx = -1;
	ASSERT_TRUE(find_drawer_entry("src", &src_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(src_idx, E.window_rows + 1));
	int collapsed_count = editorDrawerVisibleCount();

	E.pane_focus = EDITOR_PANE_DRAWER;
	char enter_key[] = {'\r'};
	ASSERT_TRUE(editor_process_keypress_with_input(enter_key, sizeof(enter_key)) == 0);
	ASSERT_TRUE(editorDrawerVisibleCount() > collapsed_count);
	ASSERT_EQ_INT(1, editorTabCount());
	ASSERT_EQ_INT(EDITOR_PANE_DRAWER, E.pane_focus);

	ASSERT_TRUE(editor_process_keypress_with_input(enter_key, sizeof(enter_key)) == 0);
	ASSERT_EQ_INT(collapsed_count, editorDrawerVisibleCount());
	ASSERT_EQ_INT(EDITOR_PANE_DRAWER, E.pane_focus);

	ASSERT_TRUE(unlink(child_file) == 0);
	ASSERT_TRUE(rmdir(src_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_drawer_enter_opens_file_in_new_tab(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char open_file[512];
	ASSERT_TRUE(path_join(open_file, sizeof(open_file), env.project_dir, "open.txt"));
	ASSERT_TRUE(write_text_file(open_file, "opened\n"));

	ASSERT_TRUE(editorTabsInit());
	add_row("keep");
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));
	int file_idx = -1;
	ASSERT_TRUE(find_drawer_entry("open.txt", &file_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(file_idx, E.window_rows + 1));
	E.pane_focus = EDITOR_PANE_DRAWER;

	char enter_key[] = {'\r'};
	ASSERT_TRUE(editor_process_keypress_with_input(enter_key, sizeof(enter_key)) == 0);
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_INT(EDITOR_PANE_TEXT, E.pane_focus);
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR(open_file, E.filename);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("opened", E.rows[0].chars);

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("keep", E.rows[0].chars);

	ASSERT_TRUE(unlink(open_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_insert_move_and_backspace(void) {
	add_row("ab");
	E.cy = 0;
	E.cx = 2;

	char backspace[] = {BACKSPACE};
	ASSERT_TRUE(editor_process_keypress_with_input(backspace, sizeof(backspace)) == 0);
	ASSERT_EQ_STR("a", E.rows[0].chars);
	ASSERT_EQ_INT(1, E.cx);

	char insert_z[] = {'Z'};
	ASSERT_TRUE(editor_process_keypress_with_input(insert_z, sizeof(insert_z)) == 0);
	ASSERT_EQ_STR("aZ", E.rows[0].chars);
	ASSERT_EQ_INT(2, E.cx);

	char arrow_left[] = "\x1b[D";
	ASSERT_TRUE(editor_process_keypress_with_input(arrow_left, sizeof(arrow_left) - 1) == 0);
	ASSERT_EQ_INT(1, E.cx);

	char home_key[] = "\x1b[H";
	ASSERT_TRUE(editor_process_keypress_with_input(home_key, sizeof(home_key) - 1) == 0);
	ASSERT_EQ_INT(0, E.cx);

	char end_key[] = "\x1b[F";
	ASSERT_TRUE(editor_process_keypress_with_input(end_key, sizeof(end_key) - 1) == 0);
	ASSERT_EQ_INT((int)strlen(E.rows[0].chars), E.cx);
	return 0;
}

static int test_editor_process_keypress_ctrl_j_does_not_insert_newline(void) {
	add_row("ab");
	E.cy = 0;
	E.cx = 1;
	int dirty_before = E.dirty;

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('j')) == 0);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("ab", E.rows[0].chars);
	ASSERT_EQ_INT(1, E.cx);
	ASSERT_EQ_INT(dirty_before, E.dirty);
	ASSERT_TRUE(assert_active_source_matches_rows() == 0);
	return 0;
}

static int test_editor_process_keypress_tab_inserts_literal_tab(void) {
	add_row("");
	E.cy = 0;
	E.cx = 0;

	ASSERT_TRUE(editor_process_single_key('\t') == 0);
	ASSERT_EQ_INT(1, E.rows[0].size);
	ASSERT_EQ_STR("\t", E.rows[0].chars);
	ASSERT_EQ_INT(1, E.cx);
	ASSERT_TRUE(assert_active_source_matches_rows() == 0);
	return 0;
}

static int test_editor_process_keypress_utf8_bytes_insert_verbatim(void) {
	static const unsigned char input[] = {0xC3, 0xB6, 0xF0, 0x9F, 0x99, 0x82};
	static const unsigned char expected[] = {0xC3, 0xB6, 0xF0, 0x9F, 0x99, 0x82, '\0'};

	add_row("");
	E.cy = 0;
	E.cx = 0;

	for (size_t i = 0; i < sizeof(input); i++) {
		ASSERT_TRUE(editor_process_single_key((char)input[i]) == 0);
	}

	ASSERT_EQ_INT((int)sizeof(input), E.rows[0].size);
	ASSERT_MEM_EQ(expected, E.rows[0].chars, sizeof(expected));
	ASSERT_EQ_INT((int)sizeof(input), E.cx);
	ASSERT_TRUE(assert_active_source_matches_rows() == 0);
	return 0;
}

static int test_editor_process_keypress_delete_key(void) {
	add_row("abcd");
	E.cy = 0;
	E.cx = 1;

	char del_key[] = "\x1b[3~";
	ASSERT_TRUE(editor_process_keypress_with_input(del_key, sizeof(del_key) - 1) == 0);
	ASSERT_EQ_STR("acd", E.rows[0].chars);
	ASSERT_EQ_INT(1, E.cx);
	return 0;
}

static int test_editor_process_keypress_arrow_down_keeps_visual_column(void) {
	add_row("a\t\tb");
	add_row("0123456789ABCDEFGHI");
	E.cy = 0;
	E.cx = 3;

	char arrow_down[] = "\x1b[B";
	ASSERT_TRUE(editor_process_keypress_with_input(arrow_down, sizeof(arrow_down) - 1) == 0);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(16, E.cx);
	return 0;
}

static int test_editor_process_keypress_ctrl_s_saves_file(void) {
	char path[] = "/tmp/rotide-test-ctrls-XXXXXX";
	int fd = mkstemp(path);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(close(fd) == 0);

	add_row("line1");
	add_row("line2");
	E.filename = strdup(path);
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 7;

	char ctrl_s[] = {CTRL_KEY('s')};
	ASSERT_TRUE(editor_process_keypress_with_input(ctrl_s, sizeof(ctrl_s)) == 0);
	ASSERT_EQ_INT(0, E.dirty);

	size_t content_len = 0;
	char *contents = read_file_contents(path, &content_len);
	ASSERT_TRUE(contents != NULL);
	ASSERT_MEM_EQ("line1\nline2\n", contents, content_len);

	free(contents);
	unlink(path);
	return 0;
}

static int test_editor_process_keypress_resize_event_updates_window_size(void) {
	char response[] = "\x1b[9;33R";
	int saved_stdin;
	size_t stdout_len = 0;
	struct stdoutCapture capture;

	E.window_rows = 8;
	E.window_cols = 40;
	E.undo_history.len = 0;
	E.redo_history.len = 0;

	editorQueueResizeEvent();
	ASSERT_TRUE(start_stdout_capture(&capture) == 0);
	ASSERT_TRUE(setup_stdin_bytes(response, sizeof(response) - 1, &saved_stdin) == 0);
	editorProcessKeypress();
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);
	char *stdout_bytes = stop_stdout_capture(&capture, &stdout_len);
	ASSERT_TRUE(stdout_bytes != NULL);

	ASSERT_EQ_INT(6, E.window_rows);
	ASSERT_EQ_INT(33, E.window_cols);
	ASSERT_EQ_INT(0, E.undo_history.len);
	ASSERT_EQ_INT(0, E.redo_history.len);
	free(stdout_bytes);
	return 0;
}

static int test_editor_process_keypress_mouse_left_click_places_cursor_with_offsets(void) {
	add_row("0123456789");
	add_row("abcdefghij");
	add_row("klmnopqrst");
	E.window_rows = 4;
	E.window_cols = 20;
	E.rowoff = 1;
	E.coloff = 2;
	E.cy = 0;
	E.cx = 0;

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char click[32];
	ASSERT_TRUE(format_sgr_mouse_event(click, sizeof(click), 0, text_start + 4, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click, strlen(click)) == 0);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(5, E.cx);
	return 0;
}

static int test_editor_process_keypress_mouse_ctrl_click_does_not_start_drag_selection(void) {
	add_row("abcdef");
	E.window_rows = 4;
	E.window_cols = 20;
	E.rowoff = 0;
	E.coloff = 0;
	E.syntax_language = EDITOR_SYNTAX_NONE;
	E.cy = 0;
	E.cx = 0;

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char click[32];
	ASSERT_TRUE(format_sgr_mouse_event(click, sizeof(click), 16, text_start + 4, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click, strlen(click)) == 0);
	ASSERT_EQ_INT(0, E.mouse_left_button_down);
	ASSERT_EQ_INT(0, E.mouse_drag_started);
	ASSERT_EQ_INT(0, E.selection_mode_active);
	ASSERT_EQ_INT(3, E.cx);
	return 0;
}

static int test_editor_process_keypress_mouse_left_click_ignores_non_text_rows(void) {
	add_row("abc");
	E.window_rows = 4;
	E.window_cols = 20;
	E.rowoff = 0;
	E.coloff = 0;
	E.cy = 0;
	E.cx = 1;

	const char click_status_bar[] = "\x1b[<0;2;6M";
	ASSERT_TRUE(editor_process_keypress_with_input(click_status_bar,
				sizeof(click_status_bar) - 1) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(1, E.cx);

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char click_filler_row[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_filler_row, sizeof(click_filler_row), 0,
				text_start + 2, 4, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_filler_row, strlen(click_filler_row)) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(1, E.cx);
	return 0;
}

static int test_editor_process_keypress_mouse_left_click_ignores_indicator_padding_columns(void) {
	add_row("0123456789");
	E.window_rows = 4;
	E.window_cols = 24;
	E.rowoff = 0;
	E.coloff = 1;
	E.cy = 0;
	E.cx = 3;
	ASSERT_TRUE(editorDrawerSetWidthForCols(1, E.window_cols));

	int text_start = editorDrawerTextStartColForCols(E.window_cols);
	int text_cols = editorDrawerTextViewportCols(E.window_cols);

	char click_left_padding[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_left_padding, sizeof(click_left_padding), 0,
				text_start + 1, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_left_padding,
				strlen(click_left_padding)) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(3, E.cx);

	char click_right_padding[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_right_padding, sizeof(click_right_padding), 0,
				text_start + text_cols, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_right_padding,
				strlen(click_right_padding)) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(3, E.cx);
	return 0;
}

static int test_editor_process_keypress_mouse_drawer_click_selects_and_toggles_directory(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char src_dir[512];
	char child_file[512];
	ASSERT_TRUE(path_join(src_dir, sizeof(src_dir), env.project_dir, "src"));
	ASSERT_TRUE(path_join(child_file, sizeof(child_file), src_dir, "child.txt"));
	ASSERT_TRUE(make_dir(src_dir));
	ASSERT_TRUE(write_text_file(child_file, "child\n"));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows));
	ASSERT_TRUE(find_drawer_entry("src", NULL, NULL));
	ASSERT_EQ_INT(EDITOR_PANE_TEXT, E.pane_focus);

	char click_src[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_src, sizeof(click_src), 0, 2, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_src, strlen(click_src)) == 0);
	ASSERT_EQ_INT(EDITOR_PANE_DRAWER, E.pane_focus);
	ASSERT_TRUE(find_drawer_entry("child.txt", NULL, NULL));

	ASSERT_TRUE(editor_process_keypress_with_input(click_src, strlen(click_src)) == 0);
	ASSERT_EQ_INT(0, find_drawer_entry("child.txt", NULL, NULL));

	ASSERT_TRUE(unlink(child_file) == 0);
	ASSERT_TRUE(rmdir(src_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_mouse_click_expands_collapsed_drawer(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));

	char click_collapse[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_collapse, sizeof(click_collapse), 0, 1, 1, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_collapse, strlen(click_collapse)) == 0);
	ASSERT_TRUE(editorDrawerIsCollapsed());
	ASSERT_EQ_INT(EDITOR_PANE_TEXT, E.pane_focus);
	ASSERT_EQ_STR("Drawer collapsed", E.statusmsg);

	char click_drawer[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_drawer, sizeof(click_drawer), 0, 1, 3, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_drawer, strlen(click_drawer)) == 0);
	ASSERT_TRUE(!editorDrawerIsCollapsed());
	ASSERT_EQ_INT(EDITOR_PANE_DRAWER, E.pane_focus);
	ASSERT_EQ_STR("Drawer expanded", E.statusmsg);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_mouse_drawer_single_file_click_opens_preview_tab(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char open_file[512];
	ASSERT_TRUE(path_join(open_file, sizeof(open_file), env.project_dir, "single.txt"));
	ASSERT_TRUE(write_text_file(open_file, "single\n"));

	ASSERT_TRUE(editorTabsInit());
	add_row("keep");
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));
	int file_idx = -1;
	ASSERT_TRUE(find_drawer_entry("single.txt", &file_idx, NULL));

	int row = file_idx - E.drawer_rowoff + 1;
	ASSERT_TRUE(row >= 1);
	char click_file[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_file, sizeof(click_file), 0, 2, row, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_file, strlen(click_file)) == 0);
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_INT(file_idx, E.drawer_selected_index);
	ASSERT_EQ_INT(EDITOR_PANE_DRAWER, E.pane_focus);
	ASSERT_TRUE(editorActiveTabIsPreview());
	ASSERT_EQ_STR("Preview tab opened. Double-click to keep it open", E.statusmsg);
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR(open_file, E.filename);
	ASSERT_EQ_STR("single", E.rows[0].chars);

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_EQ_STR("keep", E.rows[0].chars);

	ASSERT_TRUE(unlink(open_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_drawer_open_selected_file_in_preview_reuses_preview_tab(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char first_file[512];
	char second_file[512];
	ASSERT_TRUE(path_join(first_file, sizeof(first_file), env.project_dir, "first.txt"));
	ASSERT_TRUE(path_join(second_file, sizeof(second_file), env.project_dir, "second.txt"));
	ASSERT_TRUE(write_text_file(first_file, "first\n"));
	ASSERT_TRUE(write_text_file(second_file, "second\n"));

	ASSERT_TRUE(editorTabsInit());
	add_row("keep");
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));

	int first_idx = -1;
	int second_idx = -1;
	ASSERT_TRUE(find_drawer_entry("first.txt", &first_idx, NULL));
	ASSERT_TRUE(find_drawer_entry("second.txt", &second_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(first_idx, E.window_rows + 1));
	ASSERT_TRUE(editorDrawerOpenSelectedFileInPreviewTab());
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_TRUE(editorActiveTabIsPreview());
	ASSERT_EQ_STR(first_file, E.filename);

	ASSERT_TRUE(editorDrawerSelectVisibleIndex(second_idx, E.window_rows + 1));
	ASSERT_TRUE(editorDrawerOpenSelectedFileInPreviewTab());
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_TRUE(editorActiveTabIsPreview());
	ASSERT_EQ_STR(second_file, E.filename);
	ASSERT_EQ_STR("second", E.rows[0].chars);

	ASSERT_TRUE(unlink(first_file) == 0);
	ASSERT_TRUE(unlink(second_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_mouse_drawer_double_click_file_pins_preview_tab(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char open_file[512];
	ASSERT_TRUE(path_join(open_file, sizeof(open_file), env.project_dir, "double.txt"));
	ASSERT_TRUE(write_text_file(open_file, "double\n"));

	ASSERT_TRUE(editorTabsInit());
	add_row("orig");
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));
	int file_idx = -1;
	ASSERT_TRUE(find_drawer_entry("double.txt", &file_idx, NULL));

	int row = file_idx - E.drawer_rowoff + 1;
	ASSERT_TRUE(row >= 1);
	char click_file[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_file, sizeof(click_file), 0, 2, row, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_file, strlen(click_file)) == 0);
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_INT(EDITOR_PANE_DRAWER, E.pane_focus);
	ASSERT_TRUE(editorActiveTabIsPreview());
	ASSERT_EQ_STR("Preview tab opened. Double-click to keep it open", E.statusmsg);
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR(open_file, E.filename);

	ASSERT_TRUE(editor_process_keypress_with_input(click_file, strlen(click_file)) == 0);
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_INT(EDITOR_PANE_TEXT, E.pane_focus);
	ASSERT_TRUE(!editorActiveTabIsPreview());
	ASSERT_EQ_STR("Tab kept open", E.statusmsg);
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR(open_file, E.filename);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("double", E.rows[0].chars);

	ASSERT_TRUE(unlink(open_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_mouse_top_row_click_switches_tab(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("zero");
	ASSERT_TRUE(editorTabNewEmpty());
	add_row("one");
	ASSERT_TRUE(editorTabNewEmpty());
	add_row("two");
	ASSERT_EQ_INT(3, editorTabCount());
	ASSERT_EQ_INT(2, editorTabActiveIndex());

	E.window_cols = 80;
	int text_start = editorDrawerTextStartColForCols(E.window_cols);
	char click_first_tab[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_first_tab, sizeof(click_first_tab), 0,
				text_start + 1, 1, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_first_tab, strlen(click_first_tab)) == 0);
	ASSERT_EQ_INT(0, editorTabActiveIndex());
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("zero", E.rows[0].chars);
	ASSERT_EQ_INT(0, E.mouse_left_button_down);
	ASSERT_EQ_INT(0, E.mouse_drag_started);
	return 0;
}

static int test_editor_process_keypress_mouse_top_row_click_uses_variable_tab_layout(void) {
	ASSERT_TRUE(editorTabsInit());
	free(E.filename);
	E.filename = strdup("/tmp/aaaaaaaaaaabbbbbbbbbbbccccccccccc");
	ASSERT_TRUE(E.filename != NULL);
	add_row("zero");

	ASSERT_TRUE(editorTabNewEmpty());
	free(E.filename);
	E.filename = strdup("/tmp/one.txt");
	ASSERT_TRUE(E.filename != NULL);
	add_row("one");

	ASSERT_TRUE(editorTabNewEmpty());
	free(E.filename);
	E.filename = strdup("/tmp/two.txt");
	ASSERT_TRUE(E.filename != NULL);
	add_row("two");

	E.window_cols = 90;
	int text_cols = editorDrawerTextViewportCols(E.window_cols);
	struct editorTabLayoutEntry layout[ROTIDE_MAX_TABS];
	int layout_count = 0;
	ASSERT_TRUE(editorTabBuildLayoutForWidth(text_cols, layout, ROTIDE_MAX_TABS, &layout_count));
	ASSERT_TRUE(layout_count >= 2);

	int second_tab_col = -1;
	for (int i = 0; i < layout_count; i++) {
		if (layout[i].tab_idx == 1) {
			second_tab_col = layout[i].start_col + 1;
			break;
		}
	}
	ASSERT_TRUE(second_tab_col >= 0);

	int text_start = editorDrawerTextStartColForCols(E.window_cols);
	char click_second_tab[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_second_tab, sizeof(click_second_tab), 0,
				text_start + second_tab_col + 1, 1, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_second_tab, strlen(click_second_tab)) == 0);
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_STR("one", E.rows[0].chars);
	return 0;
}

static int test_editor_process_keypress_mouse_drag_on_splitter_resizes_drawer(void) {
	add_row("abcdef");
	E.window_rows = 4;
	E.window_cols = 40;
	E.drawer_width_cols = 12;
	E.cy = 0;
	E.cx = 3;
	E.pane_focus = EDITOR_PANE_TEXT;

	int separator_x = editorDrawerWidthForCols(E.window_cols) + 1;
	char press[32];
	ASSERT_TRUE(format_sgr_mouse_event(press, sizeof(press), 0, separator_x, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_EQ_INT(1, E.drawer_resize_active);
	ASSERT_EQ_INT(0, E.mouse_left_button_down);
	ASSERT_EQ_INT(0, E.mouse_drag_started);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(3, E.cx);

	char drag_smaller[32];
	ASSERT_TRUE(format_sgr_mouse_event(drag_smaller, sizeof(drag_smaller), 32, 6, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(drag_smaller, strlen(drag_smaller)) == 0);
	ASSERT_EQ_INT(5, editorDrawerWidthForCols(E.window_cols));
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(3, E.cx);

	char drag_larger[32];
	ASSERT_TRUE(format_sgr_mouse_event(drag_larger, sizeof(drag_larger), 32, 200, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(drag_larger, strlen(drag_larger)) == 0);
	ASSERT_EQ_INT(38, editorDrawerWidthForCols(E.window_cols));

	char release[32];
	ASSERT_TRUE(format_sgr_mouse_event(release, sizeof(release), 0, 200, 2, 'm'));
	ASSERT_TRUE(editor_process_keypress_with_input(release, strlen(release)) == 0);
	ASSERT_EQ_INT(0, E.drawer_resize_active);
	ASSERT_EQ_INT(0, E.mouse_left_button_down);
	ASSERT_EQ_INT(0, E.mouse_drag_started);
	return 0;
}

static int test_editor_process_keypress_mouse_wheel_scrolls_three_lines_and_clamps(void) {
	for (int i = 0; i < 10; i++) {
		add_row("line");
	}
	E.window_rows = 5;
	E.window_cols = 20;
	E.cy = 4;
	E.cx = 0;
	E.rowoff = 0;

	int text_x = editorTextBodyStartColForCols(E.window_cols) + 1;
	char wheel_down[32];
	ASSERT_TRUE(format_sgr_mouse_event(wheel_down, sizeof(wheel_down), 65, text_x, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_down, strlen(wheel_down)) == 0);
	ASSERT_EQ_INT(4, E.cy);
	ASSERT_EQ_INT(3, E.rowoff);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FREE_SCROLL, E.viewport_mode);

	E.rowoff = 8;
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_down, strlen(wheel_down)) == 0);
	ASSERT_EQ_INT(9, E.rowoff);
	ASSERT_EQ_INT(4, E.cy);

	char wheel_up[32];
	ASSERT_TRUE(format_sgr_mouse_event(wheel_up, sizeof(wheel_up), 64, text_x, 2, 'M'));
	E.rowoff = 1;
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_up, strlen(wheel_up)) == 0);
	ASSERT_EQ_INT(0, E.rowoff);
	ASSERT_EQ_INT(4, E.cy);
	return 0;
}

static int test_editor_process_keypress_mouse_wheel_scrolls_horizontally_and_clamps(void) {
	add_row("abcdefghijklmnopqrstuvwxyz");
	E.window_rows = 5;
	E.window_cols = 12;
	E.cy = 0;
	E.cx = 5;
	E.coloff = 0;

	int text_x = editorTextBodyStartColForCols(E.window_cols) + 1;
	char wheel_right[32];
	ASSERT_TRUE(format_sgr_mouse_event(wheel_right, sizeof(wheel_right), 67, text_x, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_right, strlen(wheel_right)) == 0);
	ASSERT_EQ_INT(5, E.cx);
	ASSERT_EQ_INT(3, E.coloff);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FREE_SCROLL, E.viewport_mode);

	E.coloff = 24;
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_right, strlen(wheel_right)) == 0);
	ASSERT_EQ_INT(25, E.coloff);

	char wheel_left[32];
	ASSERT_TRUE(format_sgr_mouse_event(wheel_left, sizeof(wheel_left), 66, text_x, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_left, strlen(wheel_left)) == 0);
	ASSERT_EQ_INT(22, E.coloff);

	E.coloff = 1;
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_left, strlen(wheel_left)) == 0);
	ASSERT_EQ_INT(0, E.coloff);
	return 0;
}

static int test_editor_process_keypress_mouse_wheel_scrolls_drawer_when_hovered(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	for (int i = 0; i < 12; i++) {
		char name[32];
		char path[512];
		ASSERT_TRUE(snprintf(name, sizeof(name), "file-%02d.txt", i) > 0);
		ASSERT_TRUE(path_join(path, sizeof(path), env.project_dir, name));
		ASSERT_TRUE(write_text_file(path, "x\n"));
	}

	for (int i = 0; i < 10; i++) {
		add_row("line");
	}
	E.window_rows = 4;
	E.window_cols = 30;
	E.cy = 4;
	E.cx = 0;
	E.rowoff = 2;
	E.drawer_rowoff = 0;

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	E.pane_focus = EDITOR_PANE_TEXT;

	const char wheel_down[] = "\x1b[<65;1;2M";
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_down, sizeof(wheel_down) - 1) == 0);
	ASSERT_EQ_INT(3, E.drawer_rowoff);
	ASSERT_EQ_INT(2, E.rowoff);

	const char wheel_up[] = "\x1b[<64;1;2M";
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_up, sizeof(wheel_up) - 1) == 0);
	ASSERT_EQ_INT(0, E.drawer_rowoff);
	ASSERT_EQ_INT(2, E.rowoff);

	for (int i = 0; i < 12; i++) {
		char name[32];
		char path[512];
		ASSERT_TRUE(snprintf(name, sizeof(name), "file-%02d.txt", i) > 0);
		ASSERT_TRUE(path_join(path, sizeof(path), env.project_dir, name));
		ASSERT_TRUE(unlink(path) == 0);
	}
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_mouse_wheel_scrolls_drawer_with_empty_buffer(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	for (int i = 0; i < 12; i++) {
		char name[32];
		char path[512];
		ASSERT_TRUE(snprintf(name, sizeof(name), "file-%02d.txt", i) > 0);
		ASSERT_TRUE(path_join(path, sizeof(path), env.project_dir, name));
		ASSERT_TRUE(write_text_file(path, "x\n"));
	}

	E.window_rows = 4;
	E.window_cols = 30;
	E.drawer_rowoff = 0;

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_EQ_INT(0, E.numrows);
	ASSERT_EQ_INT(0, E.drawer_selected_index);

	const char wheel_down[] = "\x1b[<65;1;2M";
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_down, sizeof(wheel_down) - 1) == 0);
	ASSERT_EQ_INT(3, E.drawer_rowoff);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	free(output);
	ASSERT_EQ_INT(3, E.drawer_rowoff);

	for (int i = 0; i < 12; i++) {
		char name[32];
		char path[512];
		ASSERT_TRUE(snprintf(name, sizeof(name), "file-%02d.txt", i) > 0);
		ASSERT_TRUE(path_join(path, sizeof(path), env.project_dir, name));
		ASSERT_TRUE(unlink(path) == 0);
	}
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_mouse_wheel_scrolls_text_when_hovered_even_if_drawer_focused(void) {
	for (int i = 0; i < 10; i++) {
		add_row("line");
	}
	E.window_rows = 5;
	E.window_cols = 30;
	E.cy = 4;
	E.cx = 0;
	E.rowoff = 0;
	E.drawer_rowoff = 2;
	E.pane_focus = EDITOR_PANE_DRAWER;

	int text_x = editorTextBodyStartColForCols(E.window_cols) + 1;
	char wheel_down[32];
	ASSERT_TRUE(format_sgr_mouse_event(wheel_down, sizeof(wheel_down), 65, text_x, 3, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_down, strlen(wheel_down)) == 0);
	ASSERT_EQ_INT(3, E.rowoff);
	ASSERT_EQ_INT(2, E.drawer_rowoff);

	char wheel_up[32];
	ASSERT_TRUE(format_sgr_mouse_event(wheel_up, sizeof(wheel_up), 64, text_x, 3, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_up, strlen(wheel_up)) == 0);
	ASSERT_EQ_INT(0, E.rowoff);
	ASSERT_EQ_INT(2, E.drawer_rowoff);
	return 0;
}

static int test_editor_process_keypress_page_up_down_scroll_viewport_without_moving_cursor(void) {
	for (int i = 0; i < 20; i++) {
		add_row("line");
	}
	E.window_rows = 5;
	E.window_cols = 20;
	E.cy = 10;
	E.cx = 2;
	E.rowoff = 4;

	const char page_down[] = "\x1b[6~";
	ASSERT_TRUE(editor_process_keypress_with_input(page_down, sizeof(page_down) - 1) == 0);
	ASSERT_EQ_INT(10, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_EQ_INT(9, E.rowoff);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FREE_SCROLL, E.viewport_mode);

	const char page_up[] = "\x1b[5~";
	ASSERT_TRUE(editor_process_keypress_with_input(page_up, sizeof(page_up) - 1) == 0);
	ASSERT_EQ_INT(10, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_EQ_INT(4, E.rowoff);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FREE_SCROLL, E.viewport_mode);
	return 0;
}

static int test_editor_process_keypress_ctrl_arrow_scrolls_horizontally_without_moving_cursor(void) {
	add_row("abcdefghijklmnopqrstuvwxyz");
	E.window_rows = 5;
	E.window_cols = 12;
	E.cy = 0;
	E.cx = 7;
	E.coloff = 0;

	const char ctrl_right[] = "\x1b[1;5C";
	ASSERT_TRUE(editor_process_keypress_with_input(ctrl_right, sizeof(ctrl_right) - 1) == 0);
	ASSERT_EQ_INT(7, E.cx);
	ASSERT_EQ_INT(3, E.coloff);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FREE_SCROLL, E.viewport_mode);

	const char ctrl_left[] = "\x1b[1;5D";
	ASSERT_TRUE(editor_process_keypress_with_input(ctrl_left, sizeof(ctrl_left) - 1) == 0);
	ASSERT_EQ_INT(7, E.cx);
	ASSERT_EQ_INT(0, E.coloff);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FREE_SCROLL, E.viewport_mode);
	return 0;
}

static int test_editor_process_keypress_free_scroll_can_leave_cursor_offscreen(void) {
	for (int i = 0; i < 12; i++) {
		add_row("line");
	}
	E.window_rows = 4;
	E.window_cols = 20;
	E.cy = 0;
	E.cx = 0;
	E.rowoff = 0;

	int text_x = editorTextBodyStartColForCols(E.window_cols) + 1;
	char wheel_down[32];
	ASSERT_TRUE(format_sgr_mouse_event(wheel_down, sizeof(wheel_down), 65, text_x, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_down, strlen(wheel_down)) == 0);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FREE_SCROLL, E.viewport_mode);
	ASSERT_TRUE(E.cy < E.rowoff);
	return 0;
}

static int test_editor_process_keypress_cursor_move_resyncs_follow_scroll(void) {
	for (int i = 0; i < 12; i++) {
		add_row("line");
	}
	E.window_rows = 4;
	E.window_cols = 20;
	E.cy = 0;
	E.cx = 0;
	E.rowoff = 0;

	int text_x = editorTextBodyStartColForCols(E.window_cols) + 1;
	char wheel_down[32];
	ASSERT_TRUE(format_sgr_mouse_event(wheel_down, sizeof(wheel_down), 65, text_x, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_down, strlen(wheel_down)) == 0);
	ASSERT_TRUE(E.cy < E.rowoff);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FREE_SCROLL, E.viewport_mode);

	const char arrow_down[] = "\x1b[B";
	ASSERT_TRUE(editor_process_keypress_with_input(arrow_down, sizeof(arrow_down) - 1) == 0);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(1, E.rowoff);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FOLLOW_CURSOR, E.viewport_mode);
	ASSERT_TRUE(E.cy >= E.rowoff);
	ASSERT_TRUE(E.cy < E.rowoff + E.window_rows);
	return 0;
}

static int test_editor_process_keypress_edit_resyncs_follow_scroll(void) {
	for (int i = 0; i < 12; i++) {
		add_row("line");
	}
	E.window_rows = 4;
	E.window_cols = 20;
	E.cy = 0;
	E.cx = 0;
	E.rowoff = 0;

	int text_x = editorTextBodyStartColForCols(E.window_cols) + 1;
	char wheel_down[32];
	ASSERT_TRUE(format_sgr_mouse_event(wheel_down, sizeof(wheel_down), 65, text_x, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_down, strlen(wheel_down)) == 0);
	ASSERT_TRUE(E.cy < E.rowoff);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FREE_SCROLL, E.viewport_mode);

	const char insert_char[] = {'x'};
	ASSERT_TRUE(editor_process_keypress_with_input(insert_char, sizeof(insert_char)) == 0);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FOLLOW_CURSOR, E.viewport_mode);
	ASSERT_EQ_INT(0, E.rowoff);
	ASSERT_EQ_INT(1, E.cx);
	ASSERT_TRUE(E.rows[0].chars[0] == 'x');
	return 0;
}

static int test_editor_process_keypress_mouse_click_clears_existing_selection(void) {
	add_row("abcdef");
	E.window_cols = 20;
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char click[32];
	ASSERT_TRUE(format_sgr_mouse_event(click, sizeof(click), 0, text_start + 5, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click, strlen(click)) == 0);
	ASSERT_EQ_INT(0, E.selection_mode_active);
	ASSERT_EQ_INT(0, E.selection_anchor_offset);
	ASSERT_EQ_INT(4, E.cx);

	struct editorSelectionRange range;
	ASSERT_EQ_INT(0, editorGetSelectionRange(&range));
	return 0;
}

static int test_editor_process_keypress_mouse_drag_starts_selection_without_ctrl_b(void) {
	add_row("abcdef");
	E.window_rows = 3;
	E.window_cols = 20;
	E.cy = 0;
	E.cx = 0;

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char press[32];
	char drag[32];
	ASSERT_TRUE(format_sgr_mouse_event(press, sizeof(press), 0, text_start + 2, 2, 'M'));
	ASSERT_TRUE(format_sgr_mouse_event(drag, sizeof(drag), 32, text_start + 6, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_EQ_INT(0, E.selection_mode_active);
	ASSERT_TRUE(editor_process_keypress_with_input(drag, strlen(drag)) == 0);
	ASSERT_EQ_INT(1, E.selection_mode_active);
	ASSERT_EQ_INT(0, assert_selection_anchor(0, 1));

	struct editorSelectionRange range;
	ASSERT_EQ_INT(1, editorGetSelectionRange(&range));
	ASSERT_EQ_INT(0, range.start_cy);
	ASSERT_EQ_INT(1, range.start_cx);
	ASSERT_EQ_INT(0, range.end_cy);
	ASSERT_EQ_INT(5, range.end_cx);
	return 0;
}

static int test_editor_process_keypress_mouse_press_without_drag_keeps_click_behavior(void) {
	add_row("abcdef");
	E.window_rows = 3;
	E.window_cols = 20;
	E.cy = 0;
	E.cx = 0;

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char press[32];
	char release[32];
	ASSERT_TRUE(format_sgr_mouse_event(press, sizeof(press), 0, text_start + 4, 2, 'M'));
	ASSERT_TRUE(format_sgr_mouse_event(release, sizeof(release), 0, text_start + 4, 2, 'm'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_EQ_INT(0, E.selection_mode_active);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(3, E.cx);
	ASSERT_EQ_INT(1, E.mouse_left_button_down);

	ASSERT_TRUE(editor_process_keypress_with_input(release, strlen(release)) == 0);
	ASSERT_EQ_INT(0, E.mouse_left_button_down);
	ASSERT_EQ_INT(0, E.mouse_drag_started);

	struct editorSelectionRange range;
	ASSERT_EQ_INT(0, editorGetSelectionRange(&range));
	return 0;
}

static int test_editor_process_keypress_mouse_drag_resets_existing_selection_anchor(void) {
	add_row("abcdef");
	E.window_cols = 20;
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	E.cx = 4;

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char press[32];
	char drag[32];
	ASSERT_TRUE(format_sgr_mouse_event(press, sizeof(press), 0, text_start + 6, 2, 'M'));
	ASSERT_TRUE(format_sgr_mouse_event(drag, sizeof(drag), 32, text_start + 3, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_TRUE(editor_process_keypress_with_input(drag, strlen(drag)) == 0);

	ASSERT_EQ_INT(1, E.selection_mode_active);
	ASSERT_EQ_INT(0, assert_selection_anchor(0, 5));

	struct editorSelectionRange range;
	ASSERT_EQ_INT(1, editorGetSelectionRange(&range));
	ASSERT_EQ_INT(2, range.start_cx);
	ASSERT_EQ_INT(5, range.end_cx);
	return 0;
}

static int test_editor_process_keypress_mouse_drag_clamps_to_viewport_without_autoscroll(void) {
	for (int i = 0; i < 6; i++) {
		add_row("0123456789");
	}
	E.window_rows = 3;
	E.window_cols = 10;
	E.rowoff = 2;
	E.coloff = 1;
	E.cy = 0;
	E.cx = 0;

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char press[32];
	char drag[32];
	ASSERT_TRUE(format_sgr_mouse_event(press, sizeof(press), 0, text_start + 3, 3, 'M'));
	ASSERT_TRUE(format_sgr_mouse_event(drag, sizeof(drag), 32, text_start + 50, 9, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_TRUE(editor_process_keypress_with_input(drag, strlen(drag)) == 0);

	ASSERT_EQ_INT(1, E.selection_mode_active);
	ASSERT_EQ_INT(0, assert_selection_anchor(3, 3));
	ASSERT_EQ_INT(4, E.cy);
	ASSERT_EQ_INT(3, E.cx);
	ASSERT_EQ_INT(2, E.rowoff);
	return 0;
}

static int test_editor_process_keypress_mouse_drag_honors_rowoff_and_coloff(void) {
	add_row("0123456789");
	add_row("abcdefghij");
	add_row("klmnopqrst");
	E.window_rows = 4;
	E.window_cols = 20;
	E.rowoff = 1;
	E.coloff = 2;

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char press[32];
	char drag[32];
	ASSERT_TRUE(format_sgr_mouse_event(press, sizeof(press), 0, text_start + 2, 2, 'M'));
	ASSERT_TRUE(format_sgr_mouse_event(drag, sizeof(drag), 32, text_start + 4, 3, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_TRUE(editor_process_keypress_with_input(drag, strlen(drag)) == 0);

	ASSERT_EQ_INT(2, E.cy);
	ASSERT_EQ_INT(5, E.cx);
	ASSERT_EQ_INT(1, E.selection_mode_active);

	struct editorSelectionRange range;
	ASSERT_EQ_INT(1, editorGetSelectionRange(&range));
	ASSERT_EQ_INT(1, range.start_cy);
	ASSERT_EQ_INT(3, range.start_cx);
	ASSERT_EQ_INT(2, range.end_cy);
	ASSERT_EQ_INT(5, range.end_cx);
	return 0;
}

static int test_editor_process_keypress_mouse_release_stops_drag_session(void) {
	add_row("abcdef");
	E.window_rows = 3;
	E.window_cols = 20;
	E.cy = 0;
	E.cx = 0;

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char press[32];
	char drag[32];
	char release[32];
	char drag_after_release[32];
	ASSERT_TRUE(format_sgr_mouse_event(press, sizeof(press), 0, text_start + 2, 2, 'M'));
	ASSERT_TRUE(format_sgr_mouse_event(drag, sizeof(drag), 32, text_start + 5, 2, 'M'));
	ASSERT_TRUE(format_sgr_mouse_event(release, sizeof(release), 0, text_start + 5, 2, 'm'));
	ASSERT_TRUE(format_sgr_mouse_event(drag_after_release, sizeof(drag_after_release), 32,
				text_start + 6, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_TRUE(editor_process_keypress_with_input(drag, strlen(drag)) == 0);
	ASSERT_EQ_INT(4, E.cx);
	ASSERT_TRUE(editor_process_keypress_with_input(release, strlen(release)) == 0);
	ASSERT_EQ_INT(1, E.selection_mode_active);
	ASSERT_EQ_INT(0, assert_selection_anchor(0, 1));
	ASSERT_EQ_INT(0, E.mouse_left_button_down);
	ASSERT_TRUE(editor_process_keypress_with_input(drag_after_release, strlen(drag_after_release)) == 0);
	ASSERT_EQ_INT(4, E.cx);

	struct editorSelectionRange range;
	ASSERT_EQ_INT(1, editorGetSelectionRange(&range));
	ASSERT_EQ_INT(1, range.start_cx);
	ASSERT_EQ_INT(4, range.end_cx);
	return 0;
}

static int test_editor_prompt_ignores_mouse_events(void) {
	add_row("abcdef");
	E.cy = 0;
	E.cx = 2;

	const char input[] = "\x1b[<0;6;1M\x1b[<32;6;1M\x1b[<0;6;1m\x1b[x";
	char *result = editor_prompt_with_input(input, sizeof(input) - 1, "Prompt: %s");
	ASSERT_TRUE(result == NULL);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	return 0;
}

static int test_editor_prompt_ignores_resize_events(void) {
	const char input[] = "\x1b[8;20Rok\r";
	int saved_stdin;
	size_t stdout_len = 0;
	struct stdoutCapture capture;

	E.window_rows = 4;
	E.window_cols = 10;
	editorQueueResizeEvent();

	ASSERT_TRUE(start_stdout_capture(&capture) == 0);
	ASSERT_TRUE(setup_stdin_bytes(input, sizeof(input) - 1, &saved_stdin) == 0);
	char *result = editorPrompt("Prompt: %s");
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);
	char *stdout_bytes = stop_stdout_capture(&capture, &stdout_len);
	ASSERT_TRUE(stdout_bytes != NULL);

	ASSERT_TRUE(result != NULL);
	ASSERT_EQ_STR("ok", result);
	ASSERT_EQ_INT(5, E.window_rows);
	ASSERT_EQ_INT(20, E.window_cols);

	free(result);
	free(stdout_bytes);
	return 0;
}

static int test_editor_process_keypress_ctrl_b_toggles_selection_mode(void) {
	add_row("abcd");
	E.cy = 0;
	E.cx = 2;

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	ASSERT_EQ_INT(1, E.selection_mode_active);
	ASSERT_EQ_INT(0, assert_selection_anchor(0, 2));

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	ASSERT_EQ_INT(0, E.selection_mode_active);

	struct editorSelectionRange range;
	ASSERT_EQ_INT(0, editorGetSelectionRange(&range));
	return 0;
}

static int test_editor_selection_range_tracks_cursor_movement(void) {
	add_row("abcd");
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);

	char arrow_right[] = "\x1b[C";
	ASSERT_TRUE(editor_process_keypress_with_input(arrow_right, sizeof(arrow_right) - 1) == 0);

	struct editorSelectionRange range;
	ASSERT_EQ_INT(1, editorGetSelectionRange(&range));
	ASSERT_EQ_INT(0, range.start_cy);
	ASSERT_EQ_INT(1, range.start_cx);
	ASSERT_EQ_INT(0, range.end_cy);
	ASSERT_EQ_INT(2, range.end_cx);

	ASSERT_TRUE(editor_process_keypress_with_input(arrow_right, sizeof(arrow_right) - 1) == 0);
	ASSERT_EQ_INT(1, editorGetSelectionRange(&range));
	ASSERT_EQ_INT(3, range.end_cx);

	char arrow_left[] = "\x1b[D";
	ASSERT_TRUE(editor_process_keypress_with_input(arrow_left, sizeof(arrow_left) - 1) == 0);
	ASSERT_EQ_INT(1, editorGetSelectionRange(&range));
	ASSERT_EQ_INT(2, range.end_cx);
	return 0;
}

static int test_editor_extract_range_text_uses_document_when_row_cache_corrupt(void) {
	add_row("abc");
	E.rows[0].size = INT_MAX;

	struct editorSelectionRange range = {
		.start_cy = 0,
		.start_cx = 0,
		.end_cy = 1,
		.end_cx = 0
	};
	char *text = (char *)1;
	size_t len = 123;
	int extracted = editorExtractRangeText(&range, &text, &len);
	ASSERT_EQ_INT(1, extracted);
	ASSERT_TRUE(text != NULL);
	ASSERT_EQ_INT(4, len);
	ASSERT_MEM_EQ("abc\n", text, len);
	free(text);
	return 0;
}

static int test_editor_delete_range_uses_document_when_row_cache_corrupt(void) {
	add_row("abc");
	E.rows[0].size = INT_MAX;

	struct editorSelectionRange range = {
		.start_cy = 0,
		.start_cx = 0,
		.end_cy = 1,
		.end_cx = 0
	};
	int deleted = editorDeleteRange(&range);
	ASSERT_EQ_INT(1, deleted);
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());
	size_t len = 123;
	char *text = editorRowsToStr(&len);
	ASSERT_TRUE(text != NULL);
	ASSERT_EQ_INT(0, len);
	free(text);
	return 0;
}

static int test_editor_process_keypress_ctrl_c_copies_single_line_selection(void) {
	add_row("hello");
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	E.cx = 5;
	int dirty_before = E.dirty;

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('c')) == 0);

	size_t clip_len = 0;
	const char *clip = editorClipboardGet(&clip_len);
	ASSERT_EQ_INT(5, clip_len);
	ASSERT_MEM_EQ("hello", clip, (size_t)clip_len);
	ASSERT_EQ_INT(dirty_before, E.dirty);
	ASSERT_EQ_INT(0, E.selection_mode_active);
	return 0;
}

static int test_editor_process_keypress_ctrl_c_copies_multiline_selection(void) {
	add_row("abc");
	add_row("def");
	add_row("ghi");
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	E.cy = 2;
	E.cx = 2;

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('c')) == 0);

	size_t clip_len = 0;
	const char *clip = editorClipboardGet(&clip_len);
	ASSERT_EQ_INT(9, clip_len);
	ASSERT_MEM_EQ("bc\ndef\ngh", clip, (size_t)clip_len);
	ASSERT_EQ_INT(0, E.selection_mode_active);
	return 0;
}

static int test_editor_process_keypress_ctrl_x_cuts_selection_and_updates_clipboard(void) {
	add_row("hello");
	add_row("world");
	E.cy = 0;
	E.cx = 2;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	E.cy = 1;
	E.cx = 3;
	int dirty_before = E.dirty;

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('x')) == 0);

	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("held", E.rows[0].chars);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_TRUE(E.dirty > dirty_before);
	ASSERT_EQ_INT(0, E.selection_mode_active);

	size_t clip_len = 0;
	const char *clip = editorClipboardGet(&clip_len);
	ASSERT_EQ_INT(7, clip_len);
	ASSERT_MEM_EQ("llo\nwor", clip, (size_t)clip_len);
	return 0;
}

static int test_editor_process_keypress_ctrl_d_deletes_selection_without_overwriting_clipboard(void) {
	ASSERT_TRUE(editorClipboardSet("keep", 4));

	add_row("hello");
	add_row("world");
	E.cy = 0;
	E.cx = 2;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	E.cy = 1;
	E.cx = 3;

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('d')) == 0);

	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("held", E.rows[0].chars);
	ASSERT_EQ_INT(0, E.selection_mode_active);

	size_t clip_len = 0;
	const char *clip = editorClipboardGet(&clip_len);
	ASSERT_EQ_INT(4, clip_len);
	ASSERT_MEM_EQ("keep", clip, (size_t)clip_len);
	return 0;
}

static int test_editor_process_keypress_ctrl_v_pastes_clipboard_text(void) {
	ASSERT_TRUE(editorClipboardSet("XYZ", 3));
	add_row("ab");
	E.cy = 0;
	E.cx = 1;
	int dirty_before = E.dirty;

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('v')) == 0);
	ASSERT_EQ_STR("aXYZb", E.rows[0].chars);
	ASSERT_EQ_INT(4, E.cx);
	ASSERT_TRUE(E.dirty > dirty_before);
	ASSERT_EQ_STR("Pasted 3 bytes", E.statusmsg);
	return 0;
}

static int test_editor_process_keypress_ctrl_v_pastes_multiline_clipboard_text(void) {
	ASSERT_TRUE(editorClipboardSet("A\nB", 3));
	add_row("xy");
	E.cy = 0;
	E.cx = 1;

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('v')) == 0);
	ASSERT_EQ_INT(2, E.numrows);
	ASSERT_EQ_STR("xA", E.rows[0].chars);
	ASSERT_EQ_STR("By", E.rows[1].chars);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(1, E.cx);
	ASSERT_EQ_STR("Pasted 3 bytes", E.statusmsg);
	return 0;
}

static int test_editor_process_keypress_ctrl_v_empty_clipboard_is_noop(void) {
	add_row("abc");
	E.cy = 0;
	E.cx = 2;
	int dirty_before = E.dirty;

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('v')) == 0);
	ASSERT_EQ_STR("abc", E.rows[0].chars);
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_EQ_INT(dirty_before, E.dirty);
	ASSERT_EQ_STR("Clipboard is empty", E.statusmsg);
	return 0;
}

static int test_editor_clipboard_sync_osc52_plain_sequence(void) {
	struct envVarBackup osc52_backup;
	struct envVarBackup tmux_backup;
	struct envVarBackup sty_backup;
	ASSERT_TRUE(backup_env_var(&osc52_backup, "ROTIDE_OSC52"));
	ASSERT_TRUE(backup_env_var(&tmux_backup, "TMUX"));
	ASSERT_TRUE(backup_env_var(&sty_backup, "STY"));
	ASSERT_TRUE(setenv("ROTIDE_OSC52", "force", 1) == 0);
	ASSERT_TRUE(unsetenv("TMUX") == 0);
	ASSERT_TRUE(unsetenv("STY") == 0);

	editorClipboardSetExternalSink(editorClipboardSyncOsc52);
	struct stdoutCapture capture;
	ASSERT_TRUE(start_stdout_capture(&capture) == 0);
	ASSERT_TRUE(editorClipboardSet("hello", 5));

	size_t output_len = 0;
	char *output = stop_stdout_capture(&capture, &output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(output_len > 0);
	ASSERT_TRUE(strstr(output, "\x1b]52;c;aGVsbG8=\a") != NULL);
	free(output);

	ASSERT_TRUE(restore_env_var(&sty_backup));
	ASSERT_TRUE(restore_env_var(&tmux_backup));
	ASSERT_TRUE(restore_env_var(&osc52_backup));
	return 0;
}

static int test_editor_clipboard_sync_osc52_tmux_wrapped_sequence(void) {
	struct envVarBackup osc52_backup;
	struct envVarBackup tmux_backup;
	struct envVarBackup sty_backup;
	ASSERT_TRUE(backup_env_var(&osc52_backup, "ROTIDE_OSC52"));
	ASSERT_TRUE(backup_env_var(&tmux_backup, "TMUX"));
	ASSERT_TRUE(backup_env_var(&sty_backup, "STY"));
	ASSERT_TRUE(setenv("ROTIDE_OSC52", "force", 1) == 0);
	ASSERT_TRUE(setenv("TMUX", "tmux-session", 1) == 0);
	ASSERT_TRUE(unsetenv("STY") == 0);

	editorClipboardSetExternalSink(editorClipboardSyncOsc52);
	struct stdoutCapture capture;
	ASSERT_TRUE(start_stdout_capture(&capture) == 0);
	ASSERT_TRUE(editorClipboardSet("hi", 2));

	size_t output_len = 0;
	char *output = stop_stdout_capture(&capture, &output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(output_len > 0);
	ASSERT_TRUE(strstr(output, "\x1bPtmux;\x1b\x1b]52;c;aGk=\a\x1b\\") != NULL);
	free(output);

	ASSERT_TRUE(restore_env_var(&sty_backup));
	ASSERT_TRUE(restore_env_var(&tmux_backup));
	ASSERT_TRUE(restore_env_var(&osc52_backup));
	return 0;
}

static int test_editor_clipboard_sync_osc52_screen_wrapped_sequence(void) {
	struct envVarBackup osc52_backup;
	struct envVarBackup tmux_backup;
	struct envVarBackup sty_backup;
	ASSERT_TRUE(backup_env_var(&osc52_backup, "ROTIDE_OSC52"));
	ASSERT_TRUE(backup_env_var(&tmux_backup, "TMUX"));
	ASSERT_TRUE(backup_env_var(&sty_backup, "STY"));
	ASSERT_TRUE(setenv("ROTIDE_OSC52", "force", 1) == 0);
	ASSERT_TRUE(unsetenv("TMUX") == 0);
	ASSERT_TRUE(setenv("STY", "screen-session", 1) == 0);

	editorClipboardSetExternalSink(editorClipboardSyncOsc52);
	struct stdoutCapture capture;
	ASSERT_TRUE(start_stdout_capture(&capture) == 0);
	ASSERT_TRUE(editorClipboardSet("hi", 2));

	size_t output_len = 0;
	char *output = stop_stdout_capture(&capture, &output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(output_len > 0);
	ASSERT_TRUE(strstr(output, "\x1bP\x1b]52;c;aGk=\a\x1b\\") != NULL);
	free(output);

	ASSERT_TRUE(restore_env_var(&sty_backup));
	ASSERT_TRUE(restore_env_var(&tmux_backup));
	ASSERT_TRUE(restore_env_var(&osc52_backup));
	return 0;
}

static int test_editor_clipboard_sync_osc52_mode_off_emits_nothing(void) {
	struct envVarBackup osc52_backup;
	struct envVarBackup tmux_backup;
	struct envVarBackup sty_backup;
	ASSERT_TRUE(backup_env_var(&osc52_backup, "ROTIDE_OSC52"));
	ASSERT_TRUE(backup_env_var(&tmux_backup, "TMUX"));
	ASSERT_TRUE(backup_env_var(&sty_backup, "STY"));
	ASSERT_TRUE(setenv("ROTIDE_OSC52", "off", 1) == 0);
	ASSERT_TRUE(unsetenv("TMUX") == 0);
	ASSERT_TRUE(unsetenv("STY") == 0);

	editorClipboardSetExternalSink(editorClipboardSyncOsc52);
	struct stdoutCapture capture;
	ASSERT_TRUE(start_stdout_capture(&capture) == 0);
	ASSERT_TRUE(editorClipboardSet("hello", 5));

	size_t output_len = 0;
	char *output = stop_stdout_capture(&capture, &output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_EQ_INT(0, output_len);
	free(output);

	ASSERT_TRUE(restore_env_var(&sty_backup));
	ASSERT_TRUE(restore_env_var(&tmux_backup));
	ASSERT_TRUE(restore_env_var(&osc52_backup));
	return 0;
}

static int test_editor_clipboard_sync_osc52_auto_mode_skips_non_tty(void) {
	struct envVarBackup osc52_backup;
	struct envVarBackup tmux_backup;
	struct envVarBackup sty_backup;
	ASSERT_TRUE(backup_env_var(&osc52_backup, "ROTIDE_OSC52"));
	ASSERT_TRUE(backup_env_var(&tmux_backup, "TMUX"));
	ASSERT_TRUE(backup_env_var(&sty_backup, "STY"));
	ASSERT_TRUE(setenv("ROTIDE_OSC52", "auto", 1) == 0);
	ASSERT_TRUE(unsetenv("TMUX") == 0);
	ASSERT_TRUE(unsetenv("STY") == 0);

	editorClipboardSetExternalSink(editorClipboardSyncOsc52);
	struct stdoutCapture capture;
	ASSERT_TRUE(start_stdout_capture(&capture) == 0);
	ASSERT_TRUE(editorClipboardSet("hello", 5));

	size_t output_len = 0;
	char *output = stop_stdout_capture(&capture, &output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_EQ_INT(0, output_len);
	free(output);

	ASSERT_TRUE(restore_env_var(&sty_backup));
	ASSERT_TRUE(restore_env_var(&tmux_backup));
	ASSERT_TRUE(restore_env_var(&osc52_backup));
	return 0;
}

static int test_editor_clipboard_sync_osc52_payload_cap_skips_external_write(void) {
	struct envVarBackup osc52_backup;
	struct envVarBackup tmux_backup;
	struct envVarBackup sty_backup;
	ASSERT_TRUE(backup_env_var(&osc52_backup, "ROTIDE_OSC52"));
	ASSERT_TRUE(backup_env_var(&tmux_backup, "TMUX"));
	ASSERT_TRUE(backup_env_var(&sty_backup, "STY"));
	ASSERT_TRUE(setenv("ROTIDE_OSC52", "force", 1) == 0);
	ASSERT_TRUE(unsetenv("TMUX") == 0);
	ASSERT_TRUE(unsetenv("STY") == 0);

	size_t payload_len = ROTIDE_OSC52_MAX_COPY_BYTES + 1;
	char *payload = malloc(payload_len);
	ASSERT_TRUE(payload != NULL);
	memset(payload, 'a', payload_len);

	editorClipboardSetExternalSink(editorClipboardSyncOsc52);
	struct stdoutCapture capture;
	ASSERT_TRUE(start_stdout_capture(&capture) == 0);
	ASSERT_TRUE(editorClipboardSet(payload, payload_len));

	size_t output_len = 0;
	char *output = stop_stdout_capture(&capture, &output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_EQ_INT(0, output_len);

	size_t clip_len = 0;
	const char *clip = editorClipboardGet(&clip_len);
	ASSERT_EQ_INT(payload_len, clip_len);
	ASSERT_MEM_EQ(payload, clip, payload_len);

	free(output);
	free(payload);
	ASSERT_TRUE(restore_env_var(&sty_backup));
	ASSERT_TRUE(restore_env_var(&tmux_backup));
	ASSERT_TRUE(restore_env_var(&osc52_backup));
	return 0;
}

static int test_editor_process_keypress_ctrl_v_clears_selection_mode(void) {
	ASSERT_TRUE(editorClipboardSet("Z", 1));
	add_row("ab");
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	ASSERT_EQ_INT(1, E.selection_mode_active);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('v')) == 0);
	ASSERT_EQ_INT(0, E.selection_mode_active);
	ASSERT_EQ_STR("aZb", E.rows[0].chars);
	return 0;
}

static int test_editor_process_keypress_ctrl_v_undo_roundtrip_single_step(void) {
	ASSERT_TRUE(editorClipboardSet("XY", 2));
	add_row("ab");
	E.cy = 0;
	E.cx = 1;

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('v')) == 0);
	ASSERT_EQ_STR("aXYb", E.rows[0].chars);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	ASSERT_EQ_STR("ab", E.rows[0].chars);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('y')) == 0);
	ASSERT_EQ_STR("aXYb", E.rows[0].chars);
	return 0;
}

static int test_editor_process_keypress_selection_ops_noop_without_selection(void) {
	add_row("abc");
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	ASSERT_EQ_INT(1, E.selection_mode_active);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('c')) == 0);
	ASSERT_EQ_STR("No selection", E.statusmsg);
	ASSERT_EQ_STR("abc", E.rows[0].chars);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('x')) == 0);
	ASSERT_EQ_STR("No selection", E.statusmsg);
	ASSERT_EQ_STR("abc", E.rows[0].chars);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('d')) == 0);
	ASSERT_EQ_STR("No selection", E.statusmsg);
	ASSERT_EQ_STR("abc", E.rows[0].chars);
	ASSERT_EQ_INT(1, E.selection_mode_active);
	return 0;
}

static int test_editor_process_keypress_escape_clears_selection_mode(void) {
	add_row("abcd");
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	E.cx = 3;
	ASSERT_EQ_INT(1, E.selection_mode_active);

	const char esc_input[] = "\x1b[x";
	ASSERT_TRUE(editor_process_keypress_with_input_silent(esc_input, sizeof(esc_input) - 1) == 0);
	ASSERT_EQ_INT(0, E.selection_mode_active);

	struct editorSelectionRange range;
	ASSERT_EQ_INT(0, editorGetSelectionRange(&range));
	return 0;
}

static int test_editor_process_keypress_edit_ops_clear_selection_mode(void) {
	add_row("ab");
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	ASSERT_TRUE(editor_process_single_key('Z') == 0);
	ASSERT_EQ_INT(0, E.selection_mode_active);
	ASSERT_EQ_STR("aZb", E.rows[0].chars);

	reset_editor_state();
	add_row("ab");
	E.cy = 0;
	E.cx = 2;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	ASSERT_TRUE(editor_process_single_key(BACKSPACE) == 0);
	ASSERT_EQ_INT(0, E.selection_mode_active);
	ASSERT_EQ_STR("a", E.rows[0].chars);

	reset_editor_state();
	add_row("ab");
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	ASSERT_TRUE(editor_process_single_key('\r') == 0);
	ASSERT_EQ_INT(0, E.selection_mode_active);
	ASSERT_EQ_INT(2, E.numrows);
	ASSERT_EQ_STR("a", E.rows[0].chars);
	ASSERT_EQ_STR("b", E.rows[1].chars);
	return 0;
}

static int test_editor_process_keypress_ctrl_z_ctrl_y_roundtrip_after_cut(void) {
	add_row("abcde");
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	E.cx = 3;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('x')) == 0);
	ASSERT_EQ_STR("ade", E.rows[0].chars);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	ASSERT_EQ_STR("abcde", E.rows[0].chars);
	ASSERT_EQ_INT(0, E.selection_mode_active);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('y')) == 0);
	ASSERT_EQ_STR("ade", E.rows[0].chars);
	ASSERT_EQ_INT(0, E.selection_mode_active);
	return 0;
}

static int test_editor_refresh_screen_highlights_active_selection_spans(void) {
	add_row("prefix alpha suffix");
	E.window_rows = 3;
	E.window_cols = 40;
	E.cy = 0;
	E.cx = 12;
	E.selection_mode_active = 1;
	ASSERT_TRUE(set_selection_anchor(0, 7));
	ASSERT_TRUE(set_active_search_match(0, 0, 6));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[7malpha\x1b[m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[7mprefix\x1b[m") == NULL);
	free(output);

	reset_editor_state();
	add_row("abc");
	add_row("def");
	E.window_rows = 4;
	E.window_cols = 20;
	E.cy = 1;
	E.cx = 2;
	E.selection_mode_active = 1;
	ASSERT_TRUE(set_selection_anchor(0, 1));

	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "a\x1b[7mbc\x1b[m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[7mde\x1b[mf") != NULL);
	free(output);
	return 0;
}

static int test_editor_process_keypress_ctrl_c_oom_preserves_buffer(void) {
	add_row("hello");
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	E.cx = 5;

	editorTestAllocFailAfter(0);
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('c')) == 0);
	ASSERT_EQ_STR("hello", E.rows[0].chars);
	ASSERT_EQ_STR("Out of memory", E.statusmsg);
	ASSERT_EQ_INT(1, E.selection_mode_active);
	editorTestAllocReset();
	return 0;
}

static int test_editor_process_keypress_ctrl_g_jumps_to_line_and_sets_col_zero(void) {
	add_row("one");
	add_row("two");
	add_row("three");
	E.cy = 2;
	E.cx = 4;

	const char input[] = {CTRL_KEY('g'), '2', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);

	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(0, E.cx);
	return 0;
}

static int test_editor_process_keypress_ctrl_g_clamps_to_last_line(void) {
	add_row("first");
	add_row("last");
	E.cy = 0;
	E.cx = 2;

	const char input[] = {CTRL_KEY('g'), '9', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);

	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(0, E.cx);
	return 0;
}

static int test_editor_process_keypress_ctrl_g_rejects_invalid_input(void) {
	add_row("alpha");
	add_row("beta");
	E.cy = 1;
	E.cx = 2;

	const char letters[] = {CTRL_KEY('g'), 'a', 'b', 'c', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(letters, sizeof(letters)) == 0);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_EQ_STR("Invalid line number", E.statusmsg);

	const char zero[] = {CTRL_KEY('g'), '0', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(zero, sizeof(zero)) == 0);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_EQ_STR("Invalid line number", E.statusmsg);

	char overflow[66];
	overflow[0] = CTRL_KEY('g');
	for (size_t i = 1; i < sizeof(overflow) - 1; i++) {
		overflow[i] = '9';
	}
	overflow[sizeof(overflow) - 1] = '\r';
	ASSERT_TRUE(editor_process_keypress_with_input_silent(overflow, sizeof(overflow)) == 0);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_EQ_STR("Invalid line number", E.statusmsg);

	return 0;
}

static int test_editor_process_keypress_ctrl_g_escape_cancels(void) {
	add_row("alpha");
	add_row("beta");
	E.cy = 1;
	E.cx = 2;

	const char input[] = {CTRL_KEY('g'), '1', '2', '\x1b', '[', 'x'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);

	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	return 0;
}

static int test_editor_process_keypress_ctrl_g_empty_buffer_sets_status(void) {
	E.cy = 0;
	E.cx = 0;

	const char input[] = {CTRL_KEY('g'), '1', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);

	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(0, E.cx);
	ASSERT_EQ_STR("Buffer is empty", E.statusmsg);
	return 0;
}

static int test_editor_process_keypress_ctrl_g_breaks_undo_typed_run_group(void) {
	ASSERT_TRUE(editor_process_single_key('a') == 0);
	ASSERT_TRUE(editor_process_single_key('b') == 0);
	ASSERT_EQ_STR("ab", E.rows[0].chars);

	const char goto_first_line[] = {CTRL_KEY('g'), '1', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(
				goto_first_line, sizeof(goto_first_line)) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(0, E.cx);

	ASSERT_TRUE(editor_process_single_key('z') == 0);
	ASSERT_EQ_STR("zab", E.rows[0].chars);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	ASSERT_EQ_STR("ab", E.rows[0].chars);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	ASSERT_EQ_INT(0, E.numrows);
	return 0;
}

static int test_editor_process_keypress_ctrl_q_exits_promptly(void) {
	pid_t pid = fork();
	ASSERT_TRUE(pid != -1);

	if (pid == 0) {
		int saved_stdout;
		if (redirect_stdout_to_devnull(&saved_stdout) == -1) {
			_exit(91);
		}

		char ctrl_q[] = {CTRL_KEY('q')};
		if (editor_process_keypress_with_input(ctrl_q, sizeof(ctrl_q)) == -1) {
			_exit(92);
		}
		_exit(93);
	}

	int status = 0;
	ASSERT_TRUE(wait_for_child_exit_with_timeout(pid, 1500, &status) == 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ_INT(EXIT_SUCCESS, WEXITSTATUS(status));
	return 0;
}

static int test_editor_process_keypress_ctrl_q_restores_cursor_shape(void) {
	int pipefd[2];
	ASSERT_TRUE(pipe(pipefd) == 0);

	pid_t pid = fork();
	ASSERT_TRUE(pid != -1);

	if (pid == 0) {
		if (close(pipefd[0]) == -1) {
			_exit(111);
		}
		if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
			_exit(112);
		}
		if (close(pipefd[1]) == -1) {
			_exit(113);
		}

		char ctrl_q[] = {CTRL_KEY('q')};
		if (editor_process_keypress_with_input(ctrl_q, sizeof(ctrl_q)) == -1) {
			_exit(114);
		}
		_exit(115);
	}

	ASSERT_TRUE(close(pipefd[1]) == 0);
	int status = 0;
	ASSERT_TRUE(wait_for_child_exit_with_timeout(pid, 1500, &status) == 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ_INT(EXIT_SUCCESS, WEXITSTATUS(status));

	size_t output_len = 0;
	char *output = read_all_fd(pipefd[0], &output_len);
	ASSERT_TRUE(close(pipefd[0]) == 0);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(output_len > 0);
	ASSERT_TRUE(strstr(output, "\x1b[0 q") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[?25h") != NULL);
	free(output);
	return 0;
}

static int test_editor_process_keypress_ctrl_q_dirty_requires_second_press(void) {
	pid_t pid = fork();
	ASSERT_TRUE(pid != -1);

	if (pid == 0) {
		int saved_stdout;
		if (redirect_stdout_to_devnull(&saved_stdout) == -1) {
			_exit(101);
		}

		add_row("unsaved");
		E.dirty = 1;

		char ctrl_q[] = {CTRL_KEY('q')};
		if (editor_process_keypress_with_input(ctrl_q, sizeof(ctrl_q)) == -1) {
			_exit(102);
		}
		if (strcmp(E.statusmsg, "File has unsaved changes. Press Ctrl-Q again to quit") != 0) {
			_exit(103);
		}
		if (editor_process_keypress_with_input(ctrl_q, sizeof(ctrl_q)) == -1) {
			_exit(104);
		}

		_exit(105);
	}

	int status = 0;
	ASSERT_TRUE(wait_for_child_exit_with_timeout(pid, 1500, &status) == 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ_INT(EXIT_SUCCESS, WEXITSTATUS(status));
	return 0;
}

static int test_editor_process_keypress_eof_exits_promptly_with_failure(void) {
	pid_t pid = fork();
	ASSERT_TRUE(pid != -1);

	if (pid == 0) {
		int saved_stdout;
		if (redirect_stdout_to_devnull(&saved_stdout) == -1) {
			_exit(121);
		}

		add_row("unsaved");
		E.dirty = 1;

		if (editor_process_keypress_with_input("", 0) == -1) {
			_exit(122);
		}
		_exit(123);
	}

	int status = 0;
	ASSERT_TRUE(wait_for_child_exit_with_timeout(pid, 1500, &status) == 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ_INT(EXIT_FAILURE, WEXITSTATUS(status));
	return 0;
}

static int test_editor_process_keypress_eof_restores_terminal_visual_state(void) {
	int pipefd[2];
	ASSERT_TRUE(pipe(pipefd) == 0);

	pid_t pid = fork();
	ASSERT_TRUE(pid != -1);

	if (pid == 0) {
		if (close(pipefd[0]) == -1) {
			_exit(131);
		}
		if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
			_exit(132);
		}
		if (close(pipefd[1]) == -1) {
			_exit(133);
		}

		if (editor_process_keypress_with_input("", 0) == -1) {
			_exit(134);
		}
		_exit(135);
	}

	ASSERT_TRUE(close(pipefd[1]) == 0);
	int status = 0;
	ASSERT_TRUE(wait_for_child_exit_with_timeout(pid, 1500, &status) == 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ_INT(EXIT_FAILURE, WEXITSTATUS(status));

	size_t output_len = 0;
	char *output = read_all_fd(pipefd[0], &output_len);
	ASSERT_TRUE(close(pipefd[0]) == 0);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(output_len > 0);
	ASSERT_TRUE(strstr(output, "\x1b[0 q") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[?25h") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[2J") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[H") != NULL);
	free(output);
	return 0;
}

static int test_editor_process_keypress_prompt_eof_exits_with_failure(void) {
	pid_t pid = fork();
	ASSERT_TRUE(pid != -1);

	if (pid == 0) {
		int saved_stdout;
		if (redirect_stdout_to_devnull(&saved_stdout) == -1) {
			_exit(141);
		}

		char input[] = {CTRL_KEY('f')};
		if (editor_process_keypress_with_input(input, sizeof(input)) == -1) {
			_exit(142);
		}
		_exit(143);
	}

	int status = 0;
	ASSERT_TRUE(wait_for_child_exit_with_timeout(pid, 1500, &status) == 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ_INT(EXIT_FAILURE, WEXITSTATUS(status));
	return 0;
}

static int test_process_terminates_promptly_on_sigterm(void) {
	pid_t pid = fork();
	ASSERT_TRUE(pid != -1);

	if (pid == 0) {
		for (;;) {
			pause();
		}
	}

	ASSERT_TRUE(kill(pid, SIGTERM) == 0);
	int status = 0;
	ASSERT_TRUE(wait_for_child_exit_with_timeout(pid, 1500, &status) == 0);
	ASSERT_TRUE(WIFSIGNALED(status));
	ASSERT_EQ_INT(SIGTERM, WTERMSIG(status));
	return 0;
}

static int test_editor_process_keypress_ctrl_f_incremental_find_first_match(void) {
	add_row("zz alpha");
	add_row("alpha later");
	E.cy = 1;
	E.cx = 2;

	const char input[] = {CTRL_KEY('f'), 'a', 'l', 'p', 'h', 'a', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);

	ASSERT_TRUE(E.search_query != NULL);
	ASSERT_EQ_STR("alpha", E.search_query);
	ASSERT_EQ_INT(0, assert_active_search_match(0, 3, 5));
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(3, E.cx);
	return 0;
}

static int test_editor_process_keypress_ctrl_f_arrow_navigation_wraps(void) {
	add_row("alpha one");
	add_row("middle alpha");
	add_row("tail alpha");
	E.cy = 0;
	E.cx = 0;

	const char input[] = {
		CTRL_KEY('f'), 'a', 'l', 'p', 'h', 'a',
		'\x1b', '[', 'B',
		'\x1b', '[', 'B',
		'\x1b', '[', 'B',
		'\x1b', '[', 'A',
		'\r'
	};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);

	ASSERT_TRUE(E.search_query != NULL);
	ASSERT_EQ_STR("alpha", E.search_query);
	ASSERT_EQ_INT(0, assert_active_search_match(2, 5, 5));
	ASSERT_EQ_INT(2, E.cy);
	ASSERT_EQ_INT(5, E.cx);
	return 0;
}

static int test_editor_process_keypress_ctrl_f_escape_restores_cursor_and_clears_match(void) {
	add_row("alpha row");
	add_row("other");
	E.cy = 1;
	E.cx = 2;

	const char input[] = {CTRL_KEY('f'), 'a', 'l', 'p', 'h', 'a', '\x1b', '[', 'x'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);

	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_TRUE(E.search_query == NULL);
	ASSERT_EQ_INT(0, E.search_match_len);
	return 0;
}

static int test_editor_process_keypress_ctrl_f_enter_keeps_active_match(void) {
	add_row("xx alpha");
	add_row("alpha second");
	E.cy = 1;
	E.cx = 4;

	const char input[] = {CTRL_KEY('f'), 'a', 'l', 'p', 'h', 'a', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);

	ASSERT_TRUE(E.search_query != NULL);
	ASSERT_EQ_STR("alpha", E.search_query);
	ASSERT_EQ_INT(0, assert_active_search_match(0, 3, 5));
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(3, E.cx);
	return 0;
}

static int test_editor_process_keypress_ctrl_f_no_match_preserves_cursor_and_sets_status(void) {
	add_row("hello world");
	add_row("second line");
	E.cy = 0;
	E.cx = 5;

	const char input[] = {CTRL_KEY('f'), 'z', 'z', 'z', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input_silent(input, sizeof(input)) == 0);

	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(5, E.cx);
	ASSERT_TRUE(E.search_query != NULL);
	ASSERT_EQ_STR("zzz", E.search_query);
	ASSERT_EQ_INT(0, E.search_match_len);
	ASSERT_EQ_STR("No matches for \"zzz\"", E.statusmsg);
	return 0;
}

static int test_editor_process_keypress_ctrl_z_ctrl_y_roundtrip_typed_run(void) {
	ASSERT_TRUE(editor_process_single_key('a') == 0);
	ASSERT_TRUE(editor_process_single_key('b') == 0);
	ASSERT_TRUE(editor_process_single_key('c') == 0);

	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("abc", E.rows[0].chars);
	int dirty_after_insert = E.dirty;

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	ASSERT_EQ_INT(0, E.numrows);
	ASSERT_EQ_INT(0, E.dirty);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('y')) == 0);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("abc", E.rows[0].chars);
	ASSERT_EQ_INT(dirty_after_insert, E.dirty);
	return 0;
}

static int test_editor_process_keypress_ctrl_z_group_break_on_navigation(void) {
	ASSERT_TRUE(editor_process_single_key('a') == 0);
	ASSERT_TRUE(editor_process_single_key('b') == 0);

	char arrow_left[] = "\x1b[D";
	ASSERT_TRUE(editor_process_keypress_with_input(arrow_left, sizeof(arrow_left) - 1) == 0);
	ASSERT_TRUE(editor_process_single_key('c') == 0);
	ASSERT_EQ_STR("acb", E.rows[0].chars);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	ASSERT_EQ_STR("ab", E.rows[0].chars);
	ASSERT_EQ_INT(1, E.cx);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	ASSERT_EQ_INT(0, E.numrows);
	return 0;
}

static int test_editor_process_keypress_ctrl_z_for_delete_and_newline_steps(void) {
	add_row("ab");
	E.cy = 0;
	E.cx = 2;

	ASSERT_TRUE(editor_process_single_key(BACKSPACE) == 0);
	ASSERT_EQ_STR("a", E.rows[0].chars);
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	ASSERT_EQ_STR("ab", E.rows[0].chars);
	ASSERT_EQ_INT(2, E.cx);

	E.cx = 1;
	ASSERT_TRUE(editor_process_single_key('\r') == 0);
	ASSERT_EQ_INT(2, E.numrows);
	ASSERT_EQ_STR("a", E.rows[0].chars);
	ASSERT_EQ_STR("b", E.rows[1].chars);
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("ab", E.rows[0].chars);
	ASSERT_EQ_INT(1, E.cx);
	return 0;
}

static int test_editor_process_keypress_ctrl_y_clears_after_new_edit(void) {
	ASSERT_TRUE(editor_process_single_key('a') == 0);
	ASSERT_TRUE(editor_process_single_key('b') == 0);
	ASSERT_TRUE(editor_process_single_key('c') == 0);
	ASSERT_EQ_STR("abc", E.rows[0].chars);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	ASSERT_EQ_INT(0, E.numrows);

	ASSERT_TRUE(editor_process_single_key('x') == 0);
	ASSERT_EQ_STR("x", E.rows[0].chars);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('y')) == 0);
	ASSERT_EQ_STR("x", E.rows[0].chars);
	ASSERT_EQ_STR("Nothing to redo", E.statusmsg);
	return 0;
}

static int test_editor_process_keypress_ctrl_z_ctrl_y_empty_stack_status(void) {
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	ASSERT_EQ_STR("Nothing to undo", E.statusmsg);
	ASSERT_EQ_INT(0, E.numrows);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('y')) == 0);
	ASSERT_EQ_STR("Nothing to redo", E.statusmsg);
	ASSERT_EQ_INT(0, E.numrows);
	return 0;
}

static int test_editor_process_keypress_ctrl_z_history_cap_eviction(void) {
	char text[ROTIDE_UNDO_HISTORY_LIMIT + 2];
	memset(text, 'x', ROTIDE_UNDO_HISTORY_LIMIT + 1);
	text[ROTIDE_UNDO_HISTORY_LIMIT + 1] = '\0';

	add_row(text);
	E.cy = 0;
	E.cx = ROTIDE_UNDO_HISTORY_LIMIT + 1;

	for (int i = 0; i < ROTIDE_UNDO_HISTORY_LIMIT + 1; i++) {
		ASSERT_TRUE(editor_process_single_key(BACKSPACE) == 0);
	}
	ASSERT_EQ_STR("", E.rows[0].chars);

	for (int i = 0; i < ROTIDE_UNDO_HISTORY_LIMIT; i++) {
		ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	}
	ASSERT_EQ_INT(ROTIDE_UNDO_HISTORY_LIMIT, E.rows[0].size);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	ASSERT_EQ_INT(ROTIDE_UNDO_HISTORY_LIMIT, E.rows[0].size);
	ASSERT_EQ_STR("Nothing to undo", E.statusmsg);
	return 0;
}

static int test_editor_process_keypress_ctrl_z_capture_oom_preserves_state(void) {
	add_row("hello");
	E.cy = 0;
	E.cx = 5;

	ASSERT_TRUE(editor_process_single_key(BACKSPACE) == 0);
	ASSERT_EQ_STR("hell", E.rows[0].chars);

	editorTestAllocFailAfter(0);
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	ASSERT_EQ_STR("hell", E.rows[0].chars);
	ASSERT_EQ_STR("Out of memory", E.statusmsg);

	editorTestAllocReset();
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	ASSERT_EQ_STR("hello", E.rows[0].chars);
	return 0;
}

static int test_editor_process_keypress_ctrl_z_restore_oom_preserves_state(void) {
	add_row("hello");
	E.cy = 0;
	E.cx = 5;

	ASSERT_TRUE(editor_process_single_key(BACKSPACE) == 0);
	ASSERT_EQ_STR("hell", E.rows[0].chars);

	editorTestAllocFailAfter(1);
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	ASSERT_EQ_STR("hell", E.rows[0].chars);
	ASSERT_EQ_STR("Out of memory", E.statusmsg);

	editorTestAllocReset();
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('z')) == 0);
	ASSERT_EQ_STR("hello", E.rows[0].chars);
	return 0;
}

static int test_editor_refresh_screen_highlights_active_search_match(void) {
	add_row("prefix alpha suffix");
	E.window_rows = 3;
	E.window_cols = 40;
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(set_active_search_match(0, 7, 5));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[7malpha\x1b[m") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_c_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-c-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 2,
			"tests/syntax/supported/c/highlight.c"));

	editorOpen(path);
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96mint\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[93mmain\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m// comment\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[32m\"txt\"\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mreturn\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m42\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_repo_buffer_c_stays_highlighted(void) {
	char *path = testResolveRepoPath("src/editing/buffer_core.c");
	ASSERT_TRUE(path != NULL);

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_EQ_INT(EDITOR_SYNTAX_C, editorSyntaxLanguageActive());

	int target_row = -1;
	for (int row_idx = 0; row_idx < E.numrows; row_idx++) {
		if (strstr(E.rows[row_idx].chars, "\"Out of memory\"") != NULL) {
			target_row = row_idx;
			break;
		}
	}
	ASSERT_TRUE(target_row >= 0);

	E.window_rows = 8;
	E.window_cols = 120;
	E.rowoff = target_row > 0 ? target_row - 1 : 0;
	E.cy = target_row;
	E.cx = 0;
	ASSERT_TRUE(editorSyntaxPrepareVisibleRowSpans(E.rowoff, 4));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[32m\"Out of memory\"\x1b[39m") != NULL);
	free(output);
	free(path);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_shell_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-shell-XXXXXX.sh";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/bash/highlight.sh"));

	editorOpen(path);
	E.window_rows = 8;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	int has_if = 0;
	int has_myfn = 0;
	int has_flag = 0;
	int has_comment = 0;
	int has_pipe = 0;
	ASSERT_TRUE(output != NULL);
	has_if = strstr(output, "\x1b[94mif\x1b[39m") != NULL;
	has_myfn = strstr(output, "\x1b[93mmyfn\x1b[39m") != NULL;
	has_flag = strstr(output, "\x1b[95m-n\x1b[39m") != NULL;
	has_comment = strstr(output, "\x1b[90m# comment\x1b[39m") != NULL;
	has_pipe = strstr(output, "\x1b[97m|\x1b[39m") != NULL;
	free(output);
	ASSERT_TRUE(has_if);
	ASSERT_TRUE(has_myfn);
	ASSERT_TRUE(has_flag);
	ASSERT_TRUE(has_comment);
	ASSERT_TRUE(has_pipe);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_html_with_injections(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-html-XXXXXX.html";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 5,
			"tests/syntax/supported/html/highlight.html"));

	editorOpen(path);
	E.window_rows = 8;
	E.window_cols = 120;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	int has_div = 0;
	int has_class = 0;
	int has_const = 0;
	int has_number = 0;
	int has_document = 0;
	int has_color = 0;
	ASSERT_TRUE(output != NULL);
	has_div = strstr(output, "\x1b[96mdiv\x1b[39m") != NULL;
	has_class = strstr(output, "\x1b[91mclass\x1b[39m") != NULL;
	has_const = strstr(output, "\x1b[94mconst\x1b[39m") != NULL;
	has_number = strstr(output, "\x1b[35m42\x1b[39m") != NULL;
	has_document = strstr(output, "\x1b[95mdocument\x1b[39m") != NULL;
	has_color = strstr(output, "\x1b[95mcolor\x1b[39m") != NULL;
	free(output);
	ASSERT_TRUE(has_div);
	ASSERT_TRUE(has_class);
	ASSERT_TRUE(has_const);
	ASSERT_TRUE(has_number);
	ASSERT_TRUE(has_document);
	ASSERT_TRUE(has_color);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_html_text_apostrophe_not_javascript_string(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-html-apostrophe-XXXXXX.html";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 5,
			"tests/syntax/supported/html/apostrophe.html"));

	editorOpen(path);
	E.window_rows = 4;
	E.window_cols = 80;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "Let's keep this plain text.") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[32mLet's keep this plain text.\x1b[39m") == NULL);
	ASSERT_TRUE(strstr(output, "\x1b[32m's keep this plain text.") == NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_javascript_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-js-XXXXXX.js";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/javascript/highlight.js"));

	editorOpen(path);
	E.window_rows = 6;
	E.window_cols = 100;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mfunction\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[93mmain\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mreturn\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m42\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_css_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-css-XXXXXX.css";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 4,
			"tests/syntax/supported/css/highlight.css"));

	editorOpen(path);
	E.window_rows = 4;
	E.window_cols = 80;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[95mbox\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[95mcolor\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_applies_syntax_highlighting_for_go_tokens(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-go-XXXXXX.go";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/go/highlight.go"));

	editorOpen(path);
	E.window_rows = 12;
	E.window_cols = 120;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mpackage\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m// comment\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96mpayload\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[93mmain\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m42\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[32m\"txt\"\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_javascript_predicates_and_locals(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-js-pred-XXXXXX.js";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/javascript/predicates.js"));

	editorOpen(path);
	E.window_rows = 10;
	E.window_cols = 120;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	int has_document = 0;
	int has_window = 0;
	int has_upper = 0;
	int has_lower = 0;
	ASSERT_TRUE(output != NULL);
	has_document = strstr(output, "\x1b[95mdocument\x1b[39m") != NULL;
	has_window = strstr(output, "\x1b[95mwindow\x1b[39m") != NULL;
	has_upper = strstr(output, "\x1b[96mUpper\x1b[39m") != NULL;
	has_lower = strstr(output, "\x1b[96mlower\x1b[39m") != NULL;
	free(output);
	ASSERT_TRUE(has_document);
	ASSERT_TRUE(!has_window);
	ASSERT_TRUE(has_upper);
	ASSERT_TRUE(!has_lower);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_javascript_predicates_repeat_refresh(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-js-repeat-XXXXXX.js";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/javascript/repeat_refresh.js"));

	editorOpen(path);
	E.window_rows = 8;
	E.window_cols = 120;
	E.cy = 0;
	E.cx = 0;

	editorOutputTestResetFrameCache();
	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	int first_has_document = 0;
	int first_has_window = 0;
	ASSERT_TRUE(output != NULL);
	first_has_document = strstr(output, "\x1b[95mdocument\x1b[39m") != NULL;
	first_has_window = strstr(output, "\x1b[95mwindow\x1b[39m") != NULL;
	free(output);
	ASSERT_TRUE(first_has_document);
	ASSERT_TRUE(!first_has_window);

	editorOutputTestResetFrameCache();
	output = refresh_screen_and_capture(&output_len);
	int second_has_document = 0;
	int second_has_window = 0;
	ASSERT_TRUE(output != NULL);
	second_has_document = strstr(output, "\x1b[95mdocument\x1b[39m") != NULL;
	second_has_window = strstr(output, "\x1b[95mwindow\x1b[39m") != NULL;
	free(output);
	ASSERT_TRUE(second_has_document);
	ASSERT_TRUE(!second_has_window);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_reports_query_budget_throttle_status(void) {
	char path[] = "/tmp/rotide-test-syntax-budget-query-XXXXXX.js";
	int fd = mkstemps(path, 3);
	ASSERT_TRUE(fd != -1);

	size_t source_len = 0;
	char *source = build_repeated_text("const value = document + window;\n", 800, &source_len);
	ASSERT_TRUE(source != NULL);
	ASSERT_TRUE(write_all(fd, source, source_len) == 0);
	free(source);
	ASSERT_TRUE(close(fd) == 0);

	editorSyntaxTestSetBudgetOverrides(1, 1, 0, 2000000000ULL);
	editorOpen(path);
	E.window_rows = 10;
	E.window_cols = 120;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	free(output);

	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(strstr(E.statusmsg, "Tree-sitter highlight throttled (budget)") != NULL ||
			strstr(E.statusmsg, "Tree-sitter throttled (parse/query budget)") != NULL);

	editorSyntaxTestResetBudgetOverrides();
	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_plain_text_file_has_no_syntax_highlighting(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-txt-XXXXXX.txt";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 4,
			"tests/syntax/supported/c/activation.c"));

	editorOpen(path);
	E.window_rows = 4;
	E.window_cols = 80;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[96mint\x1b[39m") == NULL);
	ASSERT_TRUE(strstr(output, "\x1b[94mreturn\x1b[39m") == NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m42\x1b[39m") == NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_shell_selection_and_search_override_syntax_colors(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-priority-shell-XXXXXX.sh";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 3,
			"tests/syntax/supported/bash/priority.sh"));

	editorOpen(path);
	E.window_rows = 4;
	E.window_cols = 60;
	E.cy = 0;
	E.cx = 0;

	E.selection_mode_active = 1;
	ASSERT_TRUE(set_selection_anchor(0, 0));
	E.cy = 0;
	E.cx = 2;
	ASSERT_TRUE(set_active_search_match(0, 19, 2));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	int has_selected_if = 0;
	int has_selected_if_syntax = 0;
	int has_flag = 0;
	ASSERT_TRUE(output != NULL);
	has_selected_if = strstr(output, "\x1b[7mif\x1b[m") != NULL;
	has_selected_if_syntax = strstr(output, "\x1b[7m\x1b[94m") != NULL;
	has_flag = strstr(output, "\x1b[95m-n\x1b[39m") != NULL;
	free(output);
	ASSERT_TRUE(has_selected_if);
	ASSERT_TRUE(!has_selected_if_syntax);
	ASSERT_TRUE(has_flag);

	E.selection_mode_active = 0;
	ASSERT_TRUE(set_active_search_match(0, 0, 2));

	editorOutputTestResetFrameCache();
	output = refresh_screen_and_capture(&output_len);
	int has_search_if = 0;
	int has_search_flag = 0;
	ASSERT_TRUE(output != NULL);
	has_search_if = strstr(output, "\x1b[7mif\x1b[m") != NULL;
	has_search_flag = strstr(output, "\x1b[95m-n\x1b[39m") != NULL;
	free(output);
	ASSERT_TRUE(has_search_if);
	ASSERT_TRUE(has_search_flag);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_selection_and_search_override_syntax_colors(void) {
	char path[] = "/tmp/rotide-test-syntax-highlight-priority-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 2,
			"tests/syntax/supported/c/priority.c"));

	editorOpen(path);
	E.window_rows = 4;
	E.window_cols = 60;
	E.cy = 0;
	E.cx = 0;

	E.selection_mode_active = 1;
	ASSERT_TRUE(set_selection_anchor(0, 0));
	E.cy = 0;
	E.cx = 6;
	ASSERT_TRUE(set_active_search_match(0, 7, 2));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[7mreturn\x1b[m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[7m\x1b[94m") == NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m42\x1b[39m") != NULL);
	free(output);

	E.selection_mode_active = 0;
	ASSERT_TRUE(set_active_search_match(0, 0, 6));

	editorOutputTestResetFrameCache();
	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[7mreturn\x1b[m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[35m42\x1b[39m") != NULL);
	free(output);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_editor_refresh_screen_highlight_alignment_with_escaped_controls(void) {
	const char text[] = "A\x1b" "BC";
	add_row_bytes(text, sizeof(text) - 1);
	E.window_rows = 3;
	E.window_cols = 40;
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(set_active_search_match(0, 2, 1));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "A^[\x1b[7mB\x1b[mC") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_escapes_filename_controls(void) {
	add_row("line");
	E.window_rows = 4;
	E.window_cols = 60;
	E.cy = 0;
	E.cx = 0;
	E.filename = strdup("bad\x1b[31mname.txt");
	ASSERT_TRUE(E.filename != NULL);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "bad^[[31mname.txt") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[31m") == NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_escapes_status_controls(void) {
	add_row("line");
	E.window_rows = 4;
	E.window_cols = 60;
	E.cy = 0;
	E.cx = 0;
	editorSetStatusMsg("warn:\x1b]52;c;Zm9v\a");

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "warn:^[]52;c;Zm9v^G") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b]52;c;") == NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_escapes_file_content_controls(void) {
	const char text[] = "A\x1b[2JB";
	add_row_bytes(text, sizeof(text) - 1);
	E.window_rows = 3;
	E.window_cols = 40;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "A^[[2JB") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[2J") == NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_renders_tab_bar_with_overflow_and_sanitized_labels(void) {
	ASSERT_TRUE(editorTabsInit());
	free(E.filename);
	E.filename = strdup("/tmp/a\x1b" "[31m.txt");
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 1;

	ASSERT_TRUE(editorTabNewEmpty());
	free(E.filename);
	E.filename = strdup("/tmp/beta.txt");
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 0;

	ASSERT_TRUE(editorTabNewEmpty());
	free(E.filename);
	E.filename = strdup("/tmp/gamma.txt");
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 0;

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	E.window_rows = 3;
	E.window_cols = 50;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[7m") != NULL);
	ASSERT_TRUE(strstr(output, "* a") != NULL);
	ASSERT_TRUE(strstr(output, "   >\x1b[m") != NULL);
	ASSERT_TRUE(strstr(output, "beta.txt") == NULL);
	ASSERT_TRUE(strstr(output, "gamma.txt") == NULL);
	ASSERT_TRUE(strstr(output, ">") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_tab_labels_middle_truncate_at_25_cols(void) {
	ASSERT_TRUE(editorTabsInit());
	free(E.filename);
	E.filename = strdup("/tmp/aaaaaaaaaaabbbbbbbbbbbccccccccccc");
	ASSERT_TRUE(E.filename != NULL);
	E.window_rows = 3;
	E.window_cols = 80;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "aaaaaaaaaaa...ccccccccccc") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_preview_tab_label_uses_italics(void) {
	ASSERT_TRUE(editorTabsInit());
	free(E.filename);
	E.filename = strdup("/tmp/sticky.txt");
	ASSERT_TRUE(E.filename != NULL);

	ASSERT_TRUE(editorTabNewEmpty());
	free(E.filename);
	E.filename = strdup("/tmp/preview.txt");
	ASSERT_TRUE(E.filename != NULL);
	E.is_preview = 1;

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	E.window_rows = 3;
	E.window_cols = 80;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[3mpreview.txt\x1b[23m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[3msticky.txt\x1b[23m") == NULL);
	free(output);
	return 0;
}

static int test_editor_tab_layout_width_includes_right_label_padding(void) {
	ASSERT_TRUE(editorTabsInit());
	free(E.filename);
	E.filename = strdup("/tmp/ab.txt");
	ASSERT_TRUE(E.filename != NULL);

	struct editorTabLayoutEntry layout[ROTIDE_MAX_TABS];
	int layout_count = 0;
	ASSERT_TRUE(editorTabBuildLayoutForWidth(80, layout, ROTIDE_MAX_TABS, &layout_count));
	ASSERT_EQ_INT(1, layout_count);
	ASSERT_EQ_INT(6 + (int)strlen("ab.txt"), layout[0].width_cols);
	return 0;
}

static int test_editor_tabs_align_view_keeps_active_visible_with_variable_widths(void) {
	ASSERT_TRUE(editorTabsInit());
	free(E.filename);
	E.filename = strdup("/tmp/first_tab_with_a_long_name_001.txt");
	ASSERT_TRUE(E.filename != NULL);

	ASSERT_TRUE(editorTabNewEmpty());
	free(E.filename);
	E.filename = strdup("/tmp/second_tab_with_a_long_name_002.txt");
	ASSERT_TRUE(E.filename != NULL);

	ASSERT_TRUE(editorTabNewEmpty());
	free(E.filename);
	E.filename = strdup("/tmp/third_tab_with_a_long_name_003.txt");
	ASSERT_TRUE(E.filename != NULL);

	ASSERT_TRUE(editorTabNewEmpty());
	free(E.filename);
	E.filename = strdup("/tmp/fourth_tab_with_a_long_name_004.txt");
	ASSERT_TRUE(E.filename != NULL);

	ASSERT_TRUE(editorTabSwitchToIndex(3));
	E.window_cols = 46;
	int text_cols = editorDrawerTextViewportCols(E.window_cols);

	struct editorTabLayoutEntry layout[ROTIDE_MAX_TABS];
	int layout_count = 0;
	ASSERT_TRUE(editorTabBuildLayoutForWidth(text_cols, layout, ROTIDE_MAX_TABS, &layout_count));
	ASSERT_TRUE(layout_count >= 1);
	ASSERT_TRUE(E.tab_view_start > 0);

	int active_visible = 0;
	for (int i = 0; i < layout_count; i++) {
		if (layout[i].tab_idx == 3) {
			active_visible = 1;
			break;
		}
	}
	ASSERT_TRUE(active_visible);
	return 0;
}

static int test_editor_refresh_screen_renders_drawer_entries_and_selection(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char src_dir[512];
	char child_file[512];
	ASSERT_TRUE(path_join(src_dir, sizeof(src_dir), env.project_dir, "src"));
	ASSERT_TRUE(path_join(child_file, sizeof(child_file), src_dir, "child.txt"));
	ASSERT_TRUE(make_dir(src_dir));
	ASSERT_TRUE(write_text_file(child_file, "child\n"));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows));
	int src_idx = -1;
	ASSERT_TRUE(find_drawer_entry("src", &src_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(src_idx, E.window_rows));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows));

	E.pane_focus = EDITOR_PANE_DRAWER;
	E.window_rows = 4;
	E.window_cols = 40;
	add_row("body");
	struct editorDrawerEntryView root_view;
	ASSERT_TRUE(editorDrawerGetVisibleEntry(0, &root_view));
	char expected_root_bold[256];
	ASSERT_TRUE(snprintf(expected_root_bold, sizeof(expected_root_bold),
				"\x1b[1m\x1b[37m%s\x1b[39m\x1b[22m",
				root_view.name) > 0);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, expected_root_bold) != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[7m \xE2\x96\xBE src") != NULL);
	ASSERT_TRUE(strstr(output, "\xE2\x94\x9C src") == NULL);
	ASSERT_TRUE(strstr(output, "\xE2\x94\x94 src") == NULL);
	ASSERT_TRUE(strstr(output, "\xE2\x96\xBE") != NULL);
	ASSERT_TRUE(strstr(output, "src") != NULL);
	ASSERT_TRUE(strstr(output, "\xE2\x94\x94") != NULL);
	ASSERT_TRUE(strstr(output, "\xE2\x94\x80") != NULL);
	ASSERT_TRUE(strstr(output, "child.txt") != NULL);
	ASSERT_TRUE(strstr(output, "\xE2\x94\x82 body") != NULL);
	free(output);

	ASSERT_TRUE(unlink(child_file) == 0);
	ASSERT_TRUE(rmdir(src_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_drawer_hides_selection_marker_when_unfocused(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char src_dir[512];
	ASSERT_TRUE(path_join(src_dir, sizeof(src_dir), env.project_dir, "src"));
	ASSERT_TRUE(make_dir(src_dir));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows));
	int src_idx = -1;
	ASSERT_TRUE(find_drawer_entry("src", &src_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(src_idx, E.window_rows));

	E.window_rows = 4;
	E.window_cols = 40;
	add_row("body");

	E.pane_focus = EDITOR_PANE_DRAWER;
	size_t focused_len = 0;
	char *focused = refresh_screen_and_capture(&focused_len);
	ASSERT_TRUE(focused != NULL);
	ASSERT_TRUE(strstr(focused, "\x1b[7m \xE2\x96\xB8 src") != NULL);
	free(focused);

	E.pane_focus = EDITOR_PANE_TEXT;
	size_t unfocused_len = 0;
	char *unfocused = refresh_screen_and_capture(&unfocused_len);
	ASSERT_TRUE(unfocused != NULL);
	ASSERT_TRUE(strstr(unfocused, "\xE2\x97\x8F") == NULL);
	ASSERT_TRUE(strstr(unfocused, "\x1b[7m \xE2\x96\xB8 src") == NULL);
	free(unfocused);

	ASSERT_TRUE(rmdir(src_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_drawer_active_file_uses_inverted_background(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char active_file[512];
	ASSERT_TRUE(path_join(active_file, sizeof(active_file), env.project_dir, "active.txt"));
	ASSERT_TRUE(write_text_file(active_file, "active\n"));

	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorTabOpenFileAsNew(active_file));
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));

	E.pane_focus = EDITOR_PANE_TEXT;
	E.window_rows = 6;
	E.window_cols = 60;
	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[7m active.txt") != NULL);
	ASSERT_TRUE(strstr(output, "active.txt\x1b[m") == NULL);
	ASSERT_TRUE(strstr(output, "\x1b[m\xE2\x94\x82") != NULL);
	free(output);

	ASSERT_TRUE(unlink(active_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_drawer_collapsed_renders_expand_indicator(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerSetCollapsed(1));

	add_row("body");
	E.window_rows = 4;
	E.window_cols = 20;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "[>]") != NULL);
	ASSERT_TRUE(strstr(output, "[<]") == NULL);
	ASSERT_EQ_INT(ROTIDE_DRAWER_COLLAPSED_WIDTH, editorDrawerWidthForCols(E.window_cols));
	free(output);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_drawer_renders_unicode_tree_connectors(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char src_dir[512];
	char zzz_dir[512];
	char alloc_file[512];
	char rotide_file[512];
	char helpers_file[512];
	ASSERT_TRUE(path_join(src_dir, sizeof(src_dir), env.project_dir, "src"));
	ASSERT_TRUE(path_join(zzz_dir, sizeof(zzz_dir), env.project_dir, "zzz"));
	ASSERT_TRUE(path_join(alloc_file, sizeof(alloc_file), src_dir, "alloc_test_hooks.c"));
	ASSERT_TRUE(path_join(rotide_file, sizeof(rotide_file), src_dir, "rotide_tests.c"));
	ASSERT_TRUE(path_join(helpers_file, sizeof(helpers_file), src_dir, "test_helpers.c"));
	ASSERT_TRUE(make_dir(src_dir));
	ASSERT_TRUE(make_dir(zzz_dir));
	ASSERT_TRUE(write_text_file(alloc_file, "a\n"));
	ASSERT_TRUE(write_text_file(rotide_file, "b\n"));
	ASSERT_TRUE(write_text_file(helpers_file, "c\n"));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));
	int src_idx = -1;
	ASSERT_TRUE(find_drawer_entry("src", &src_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(src_idx, E.window_rows + 1));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));

	E.window_rows = 8;
	E.window_cols = 80;
	(void)editorDrawerSetWidthForCols(40, E.window_cols);
	E.pane_focus = EDITOR_PANE_TEXT;
	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\xE2\x96\xBE src") != NULL);
	ASSERT_TRUE(strstr(output, "\xE2\x96\xB8 zzz") != NULL);
	ASSERT_TRUE(strstr(output, "\xE2\x94\x9C src") == NULL);
	ASSERT_TRUE(strstr(output, "\xE2\x94\x94 src") == NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x94\x9C\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x94\x94\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x94\x80\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "alloc_test_hooks.c") != NULL);
	ASSERT_TRUE(strstr(output, "rotide_tests.c") != NULL);
	ASSERT_TRUE(strstr(output, "test_helpers.c") != NULL);
	free(output);

	ASSERT_TRUE(unlink(alloc_file) == 0);
	ASSERT_TRUE(unlink(rotide_file) == 0);
	ASSERT_TRUE(unlink(helpers_file) == 0);
	ASSERT_TRUE(rmdir(src_dir) == 0);
	ASSERT_TRUE(rmdir(zzz_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_drawer_selected_overflow_spills_into_text_area(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char long_dir[512];
	const char *dirname = "drawer_item_with_overflow_tail_segment";
	ASSERT_TRUE(path_join(long_dir, sizeof(long_dir), env.project_dir, dirname));
	ASSERT_TRUE(make_dir(long_dir));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	int long_idx = -1;
	ASSERT_TRUE(find_drawer_entry(dirname, &long_idx, NULL));
	ASSERT_TRUE(editorDrawerSelectVisibleIndex(long_idx, E.window_rows + 1));

	E.pane_focus = EDITOR_PANE_DRAWER;
	E.window_rows = 4;
	E.window_cols = 60;
	ASSERT_TRUE(editorDrawerSetWidthForCols(12, E.window_cols));
	add_row("body");

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "overflow_tail_segment") != NULL);

	int highlighted_tail = 0;
	const char *scan = output;
	while ((scan = strstr(scan, "\x1b[7m")) != NULL) {
		const char *normal = strstr(scan + 4, "\x1b[m");
		if (normal == NULL) {
			break;
		}
		const char *tail = strstr(scan, "overflow_tail_segment");
		if (tail != NULL && tail < normal) {
			highlighted_tail = 1;
			break;
		}
		scan = normal + 3;
	}
	ASSERT_TRUE(highlighted_tail);

	free(output);
	ASSERT_TRUE(rmdir(long_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_drawer_splitter_spans_editor_rows(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));

	E.window_rows = 4;
	E.window_cols = 40;
	E.drawer_width_cols = 10;
	add_row("body");

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);

	const char *separator = "\xE2\x94\x82";
	size_t separator_len = strlen(separator);
	int separator_count = 0;
	const char *cursor = output;
	while ((cursor = strstr(cursor, separator)) != NULL) {
		separator_count++;
		cursor += separator_len;
	}
	ASSERT_EQ_INT(E.window_rows + 1, separator_count);

	free(output);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_cursor_column_offsets_for_drawer(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));

	add_row("abc");
	E.window_rows = 3;
	E.window_cols = 20;
	E.cy = 0;
	E.cx = 1;
	E.rowoff = 0;
	E.coloff = 0;
	E.pane_focus = EDITOR_PANE_TEXT;

	int expected_col = editorTextBodyStartColForCols(E.window_cols) + 2;
	char expected_cursor[32];
	ASSERT_TRUE(snprintf(expected_cursor, sizeof(expected_cursor), "\x1b[2;%dH", expected_col) > 0);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, expected_cursor) != NULL);
	free(output);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_hides_cursor_when_drawer_focused(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));

	add_row("abc");
	E.window_rows = 3;
	E.window_cols = 20;
	E.cy = 0;
	E.cx = 1;
	E.rowoff = 0;
	E.coloff = 0;
	E.pane_focus = EDITOR_PANE_DRAWER;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[?25h") == NULL);
	free(output);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_refresh_screen_hides_cursor_when_offscreen_in_free_scroll(void) {
	add_row("line1");
	add_row("line2");
	add_row("line3");
	add_row("line4");
	add_row("line5");
	add_row("line6");
	E.window_rows = 3;
	E.window_cols = 20;
	E.cy = 0;
	E.cx = 0;
	E.rowoff = 4;
	E.coloff = 0;
	E.pane_focus = EDITOR_PANE_TEXT;
	editorViewportSetMode(EDITOR_VIEWPORT_FREE_SCROLL);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[?25h") == NULL);
	free(output);
	return 0;
}

static int test_editor_drawer_layout_clamps_tiny_widths(void) {
	ASSERT_EQ_INT(0, editorDrawerWidthForCols(1));
	ASSERT_EQ_INT(1, editorDrawerTextViewportCols(1));
	ASSERT_EQ_INT(0, editorTextBodyStartColForCols(1));
	ASSERT_EQ_INT(1, editorTextBodyViewportCols(1));
	ASSERT_EQ_INT(1, editorDrawerWidthForCols(2));
	ASSERT_EQ_INT(1, editorDrawerTextViewportCols(2));
	ASSERT_EQ_INT(1, editorTextBodyStartColForCols(2));
	ASSERT_EQ_INT(1, editorTextBodyViewportCols(2));
	ASSERT_EQ_INT(0, editorDrawerSeparatorWidthForCols(2));

	E.drawer_width_cols = 24;
	E.drawer_width_user_set = 0;
	ASSERT_EQ_INT(4, editorDrawerWidthForCols(10));
	ASSERT_EQ_INT(1, editorDrawerSeparatorWidthForCols(10));
	ASSERT_EQ_INT(5, editorDrawerTextViewportCols(10));
	ASSERT_EQ_INT(6, editorTextBodyStartColForCols(10));
	ASSERT_EQ_INT(3, editorTextBodyViewportCols(10));

	ASSERT_TRUE(editorDrawerSetWidthForCols(24, 10));
	ASSERT_EQ_INT(8, editorDrawerWidthForCols(10));
	ASSERT_EQ_INT(1, editorDrawerTextViewportCols(10));
	ASSERT_EQ_INT(9, editorTextBodyStartColForCols(10));
	ASSERT_EQ_INT(1, editorTextBodyViewportCols(10));

	ASSERT_TRUE(editorDrawerSetWidthForCols(3, 10));
	ASSERT_EQ_INT(3, editorDrawerWidthForCols(10));
	ASSERT_EQ_INT(6, editorDrawerTextViewportCols(10));

	ASSERT_TRUE(editorDrawerResizeByDeltaForCols(-10, 10));
	ASSERT_EQ_INT(1, editorDrawerWidthForCols(10));
	ASSERT_EQ_INT(8, editorDrawerTextViewportCols(10));

	ASSERT_TRUE(editorDrawerResizeByDeltaForCols(50, 10));
	ASSERT_EQ_INT(8, editorDrawerWidthForCols(10));
	ASSERT_EQ_INT(1, editorDrawerTextViewportCols(10));
	return 0;
}

static int test_editor_refresh_screen_contains_expected_sequences(void) {
	add_row("first line");
	add_row("second line");
	E.cy = 1;
	E.cx = 3;
	E.rowoff = 0;
	E.coloff = 0;
	E.window_rows = 4;
	E.window_cols = 30;
	E.filename = strdup("sample.txt");
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 1;
	editorSetStatusMsg("status message");

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(output_len > 0);
	ASSERT_TRUE(strstr(output, "\x1b[?25l") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[6 q") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[?25h") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[7m") != NULL);
	ASSERT_TRUE(strstr(output, "first line") != NULL);
	ASSERT_TRUE(strstr(output, "status message") != NULL);

	free(output);
	return 0;
}

static int test_editor_refresh_screen_file_row_frame_diff_updates_only_changed_rows(void) {
	add_row("alpha");
	add_row("beta");
	add_row("gamma");
	E.window_rows = 4;
	E.window_cols = 40;
	E.cy = 0;
	E.cx = 0;

	editorOutputTestResetFrameCache();
	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	free(output);
	ASSERT_EQ_INT(E.window_rows, editorOutputTestLastRefreshFileRowDrawCount());

	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	free(output);
	ASSERT_EQ_INT(0, editorOutputTestLastRefreshFileRowDrawCount());

	E.cy = 1;
	E.cx = 2;
	editorInsertChar('X');
	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	free(output);

	int changed_rows = editorOutputTestLastRefreshFileRowDrawCount();
	ASSERT_TRUE(changed_rows > 0);
	ASSERT_TRUE(changed_rows < E.window_rows);
	return 0;
}

static int test_editor_refresh_screen_uses_configured_cursor_style(void) {
	add_row("cursor style");
	E.window_rows = 4;
	E.window_cols = 30;

	E.cursor_style = EDITOR_CURSOR_STYLE_BLOCK;
	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[2 q") != NULL);
	free(output);

	E.cursor_style = EDITOR_CURSOR_STYLE_UNDERLINE;
	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[4 q") != NULL);
	free(output);

	return 0;
}

static int test_editor_refresh_screen_hides_expired_message(void) {
	add_row("line one");
	add_row("line two");
	E.window_rows = 4;
	E.window_cols = 30;
	strncpy(E.statusmsg, "old message", sizeof(E.statusmsg) - 1);
	E.statusmsg[sizeof(E.statusmsg) - 1] = '\0';
	E.statusmsg_time = time(NULL) - 10;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "old message") == NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_shows_right_overflow_indicator(void) {
	add_row("0123456789abcdefghijklmnopqrstuvwxyz");
	E.window_rows = 3;
	E.window_cols = 24;
	E.rowoff = 0;
	E.coloff = 0;
	ASSERT_TRUE(editorDrawerSetWidthForCols(1, E.window_cols));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x86\x92\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x86\x90\x1b[39m") == NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_shows_left_overflow_indicator(void) {
	add_row("0123456789");
	E.window_rows = 3;
	E.window_cols = 24;
	E.rowoff = 0;
	E.coloff = 1;
	E.cy = 0;
	E.cx = 2;
	ASSERT_TRUE(editorDrawerSetWidthForCols(1, E.window_cols));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x86\x90\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x86\x92\x1b[39m") == NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_shows_both_horizontal_overflow_indicators(void) {
	add_row("0123456789abcdefghijklmnopqrstuvwxyz");
	E.window_rows = 3;
	E.window_cols = 24;
	E.rowoff = 0;
	E.coloff = 1;
	E.cy = 0;
	E.cx = 2;
	ASSERT_TRUE(editorDrawerSetWidthForCols(1, E.window_cols));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x86\x90\x1b[39m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x86\x92\x1b[39m") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_non_file_rows_do_not_show_overflow_indicators(void) {
	E.window_rows = 3;
	E.window_cols = 24;
	E.rowoff = 0;
	E.coloff = 4;
	ASSERT_TRUE(editorDrawerSetWidthForCols(1, E.window_cols));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x86\x90\x1b[39m") == NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m\xE2\x86\x92\x1b[39m") == NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_out_of_buffer_tildes_are_gray(void) {
	add_row("line");
	E.window_rows = 4;
	E.window_cols = 24;
	E.rowoff = 0;
	E.coloff = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[90m~\x1b[39m") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_updates_horizontal_scroll(void) {
	add_row("01234567890123456789");
	add_row("second");
	E.window_rows = 3;
	E.window_cols = 5;
	E.cy = 0;
	E.cx = 15;
	E.coloff = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_EQ_INT(15, E.rx);
	ASSERT_TRUE(E.coloff > 0);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_slice_after_multibyte_scroll(void) {
	add_row("\xC3\xB6XYZ");
	E.window_rows = 3;
	E.window_cols = 10;
	E.cy = 0;
	E.cx = 2;
	E.coloff = 1;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "XYZ") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_cursor_sequence_not_truncated_by_window_width(void) {
	add_row("x");
	E.window_rows = 3;
	E.window_cols = 1;
	E.cy = 0;
	E.cx = 0;
	E.rowoff = 0;
	E.coloff = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[2;1H") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_status_bar_single_row_percent(void) {
	add_row("single");
	E.window_rows = 3;
	E.window_cols = 40;
	E.cy = 0;
	E.cx = 0;
	E.filename = strdup("single.txt");
	ASSERT_TRUE(E.filename != NULL);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "1,1    100%") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_status_bar_cursor_multibyte_col(void) {
	add_row("\xC3\xB6" "a");
	E.window_rows = 3;
	E.window_cols = 40;
	E.cy = 0;
	E.cx = 2;
	E.filename = strdup("multi.txt");
	ASSERT_TRUE(E.filename != NULL);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "1,2    100%") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_status_bar_cursor_tab_display_col(void) {
	add_row("a\tb");
	E.window_rows = 3;
	E.window_cols = 40;
	E.cy = 0;
	E.cx = 2;
	E.filename = strdup("tabs.txt");
	ASSERT_TRUE(E.filename != NULL);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "1,9    100%") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_status_bar_shows_full_path_when_space_allows(void) {
	add_row("line");
	E.window_rows = 3;
	E.window_cols = 110;
	E.cy = 0;
	E.cx = 0;
	E.filename = strdup("/project/src/modules/editor/very_long_filename.c");
	ASSERT_TRUE(E.filename != NULL);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "/project/src/modules/editor/very_long_filename.c") != NULL);
	ASSERT_TRUE(strstr(output, "1,1    100%") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_status_bar_truncates_prefix_keeps_basename_visible(void) {
	add_row("line");
	E.window_rows = 3;
	E.window_cols = 45;
	E.cy = 0;
	E.cx = 0;
	E.filename = strdup("/very/long/prefix/that/keeps/growing/path/target_file_name.c");
	ASSERT_TRUE(E.filename != NULL);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "target_file_name.c") != NULL);
	ASSERT_TRUE(strstr(output, "...") != NULL);
	ASSERT_TRUE(strstr(output, "1,1    100%") != NULL);
	free(output);
	return 0;
}

struct testCase {
	const char *name;
	int (*run)(void);
};

int main(void) {
	setlocale(LC_CTYPE, "");
	char *startup_cwd = getcwd(NULL, 0);
	if (startup_cwd == NULL) {
		fprintf(stderr, "Failed to capture startup cwd: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}
	testHelpersInitPaths(startup_cwd);
	free(startup_cwd);

	const struct testCase tests[] = {
		{"utf8_decode_valid_sequences", test_utf8_decode_valid_sequences},
		{"utf8_decode_invalid_sequences", test_utf8_decode_invalid_sequences},
		{"rope_copy_and_dup_range", test_rope_copy_and_dup_range},
		{"rope_replace_range_across_large_text", test_rope_replace_range_across_large_text},
		{"document_line_index_tracks_blank_lines_and_trailing_newline",
			test_document_line_index_tracks_blank_lines_and_trailing_newline},
		{"document_replace_range_updates_text_and_line_index",
			test_document_replace_range_updates_text_and_line_index},
		{"document_position_offset_roundtrip",
			test_document_position_offset_roundtrip},
		{"document_reset_from_text_source_streams_bytes",
			test_document_reset_from_text_source_streams_bytes},
			{"editor_build_active_text_source_uses_document_after_open",
				test_editor_build_active_text_source_uses_document_after_open},
			{"editor_document_incremental_updates_for_basic_edits",
				test_editor_document_incremental_updates_for_basic_edits},
		{"editor_buffer_offset_roundtrip_uses_document_mapping",
			test_editor_buffer_offset_roundtrip_uses_document_mapping},
		{"editor_buffer_offsets_rebuild_document_after_row_mutation",
			test_editor_buffer_offsets_rebuild_document_after_row_mutation},
			{"editor_document_lazy_rebuild_after_low_level_row_mutation",
				test_editor_document_lazy_rebuild_after_low_level_row_mutation},
			{"editor_document_restored_for_undo_redo",
				test_editor_document_restored_for_undo_redo},
			{"editor_document_edit_capture_uses_active_source",
				test_editor_document_edit_capture_uses_active_source},
			{"editor_document_selection_and_delete_use_active_source",
				test_editor_document_selection_and_delete_use_active_source},
			{"editor_document_save_uses_active_source",
				test_editor_document_save_uses_active_source},
		{"editor_buffer_find_uses_active_source_without_full_dup",
			test_editor_buffer_find_uses_active_source_without_full_dup},
		{"utf8_continuation_detection", test_utf8_continuation_detection},
		{"grapheme_extend_classification", test_grapheme_extend_classification},
		{"char_display_width_basics", test_char_display_width_basics},
		{"row_char_boundaries", test_row_char_boundaries},
		{"row_cluster_boundaries_combining", test_row_cluster_boundaries_combining},
		{"row_cluster_boundaries_zwj_sequence", test_row_cluster_boundaries_zwj_sequence},
		{"row_cluster_boundaries_regional_indicators", test_row_cluster_boundaries_regional_indicators},
		{"row_cx_to_rx_with_tabs", test_row_cx_to_rx_with_tabs},
			{"row_rx_to_cx_with_tabs", test_row_rx_to_cx_with_tabs},
			{"editor_update_row_expands_tabs", test_editor_update_row_expands_tabs},
			{"editor_update_row_tab_alignment_after_multibyte",
				test_editor_update_row_tab_alignment_after_multibyte},
			{"editor_update_row_escapes_c0_and_esc_in_render",
				test_editor_update_row_escapes_c0_and_esc_in_render},
			{"editor_update_row_escapes_c1_codepoints_in_render",
				test_editor_update_row_escapes_c1_codepoints_in_render},
			{"editor_update_row_preserves_printable_utf8_with_80_9f_continuations",
				test_editor_update_row_preserves_printable_utf8_with_80_9f_continuations},
			{"row_cx_to_rx_with_escaped_controls", test_row_cx_to_rx_with_escaped_controls},
			{"row_rx_to_cx_with_escaped_controls", test_row_rx_to_cx_with_escaped_controls},
			{"insert_and_delete_row_updates_dirty", test_insert_and_delete_row_updates_dirty},
			{"editor_delete_row_rejects_idx_at_numrows",
				test_editor_delete_row_rejects_idx_at_numrows},
		{"insert_and_delete_chars", test_insert_and_delete_chars},
		{"editor_del_char_at_rejects_idx_at_row_size",
			test_editor_del_char_at_rejects_idx_at_row_size},
		{"editor_insert_char_creates_initial_row", test_editor_insert_char_creates_initial_row},
		{"editor_insert_newline_splits_row", test_editor_insert_newline_splits_row},
		{"editor_insert_newline_at_row_start", test_editor_insert_newline_at_row_start},
			{"editor_del_char_cluster_and_merge", test_editor_del_char_cluster_and_merge},
			{"editor_rows_to_str", test_editor_rows_to_str},
			{"editor_rows_to_str_uses_document_when_row_cache_corrupt",
				test_editor_rows_to_str_uses_document_when_row_cache_corrupt},
			{"editor_open_reads_rows_and_clears_dirty", test_editor_open_reads_rows_and_clears_dirty},
			{"editor_syntax_activation_for_c_and_h_files",
				test_editor_syntax_activation_for_c_and_h_files},
			{"editor_syntax_activation_for_shell_files_and_shebang",
				test_editor_syntax_activation_for_shell_files_and_shebang},
			{"editor_syntax_activation_for_html_js_and_css_files",
				test_editor_syntax_activation_for_html_js_and_css_files},
			{"editor_syntax_activation_for_go_and_mod_files",
				test_editor_syntax_activation_for_go_and_mod_files},
			{"editor_syntax_disabled_for_non_c_or_shell_files",
				test_editor_syntax_disabled_for_non_c_or_shell_files},
			{"editor_save_as_c_file_enables_syntax", test_editor_save_as_c_file_enables_syntax},
			{"editor_save_as_go_file_enables_syntax",
				test_editor_save_as_go_file_enables_syntax},
			{"editor_save_as_shell_and_non_shell_updates_syntax",
				test_editor_save_as_shell_and_non_shell_updates_syntax},
			{"editor_save_as_web_and_plain_updates_syntax",
				test_editor_save_as_web_and_plain_updates_syntax},
			{"editor_syntax_incremental_edits_keep_tree_valid",
				test_editor_syntax_incremental_edits_keep_tree_valid},
			{"editor_syntax_incremental_edits_keep_shell_tree_valid",
				test_editor_syntax_incremental_edits_keep_shell_tree_valid},
			{"editor_syntax_incremental_edits_keep_html_tree_valid",
				test_editor_syntax_incremental_edits_keep_html_tree_valid},
				{"editor_syntax_incremental_edits_keep_javascript_tree_valid",
					test_editor_syntax_incremental_edits_keep_javascript_tree_valid},
				{"editor_syntax_incremental_edits_keep_css_tree_valid",
					test_editor_syntax_incremental_edits_keep_css_tree_valid},
				{"editor_syntax_incremental_edits_keep_go_tree_valid",
					test_editor_syntax_incremental_edits_keep_go_tree_valid},
				{"editor_syntax_query_budget_match_limit_is_graceful",
					test_editor_syntax_query_budget_match_limit_is_graceful},
				{"editor_syntax_parse_budget_is_graceful",
					test_editor_syntax_parse_budget_is_graceful},
				{"editor_syntax_incremental_provider_parse_keeps_tree_valid",
					test_editor_syntax_incremental_provider_parse_keeps_tree_valid},
				{"editor_syntax_large_file_stays_enabled_in_degraded_mode",
					test_editor_syntax_large_file_stays_enabled_in_degraded_mode},
				{"editor_syntax_visible_cache_recomputes_only_changed_rows",
					test_editor_syntax_visible_cache_recomputes_only_changed_rows},
				{"editor_syntax_undo_redo_preserves_tree",
					test_editor_syntax_undo_redo_preserves_tree},
			{"editor_syntax_undo_redo_preserves_shell_tree",
				test_editor_syntax_undo_redo_preserves_shell_tree},
			{"editor_tabs_keep_independent_syntax_states",
				test_editor_tabs_keep_independent_syntax_states},
			{"editor_tabs_keep_shell_and_c_syntax_states",
				test_editor_tabs_keep_shell_and_c_syntax_states},
			{"editor_tabs_keep_web_and_c_syntax_states",
				test_editor_tabs_keep_web_and_c_syntax_states},
			{"editor_recovery_restore_rebuilds_c_syntax_tree",
				test_editor_recovery_restore_rebuilds_c_syntax_tree},
			{"editor_recovery_restore_rebuilds_shell_syntax_tree",
				test_editor_recovery_restore_rebuilds_shell_syntax_tree},
		{"editor_save_writes_file_and_clears_dirty", test_editor_save_writes_file_and_clears_dirty},
		{"editor_save_prompts_for_filename", test_editor_save_prompts_for_filename},
		{"editor_save_aborts_when_prompt_cancelled", test_editor_save_aborts_when_prompt_cancelled},
		{"editor_save_rejects_empty_filename_prompt",
			test_editor_save_rejects_empty_filename_prompt},
		{"editor_save_truncates_existing_file_with_empty_buffer",
			test_editor_save_truncates_existing_file_with_empty_buffer},
		{"editor_save_preserves_existing_file_mode",
			test_editor_save_preserves_existing_file_mode},
		{"editor_save_removes_temp_file_on_success",
			test_editor_save_removes_temp_file_on_success},
		{"editor_save_failure_cleans_temp_file", test_editor_save_failure_cleans_temp_file},
		{"editor_save_temp_fsync_failure_cleans_temp_file",
			test_editor_save_temp_fsync_failure_cleans_temp_file},
		{"editor_save_temp_close_failure_cleans_temp_file",
			test_editor_save_temp_close_failure_cleans_temp_file},
		{"editor_save_rename_failure_cleans_temp_file_deterministic",
			test_editor_save_rename_failure_cleans_temp_file_deterministic},
		{"editor_save_rename_failure_reports_permission_denied_class",
			test_editor_save_rename_failure_reports_permission_denied_class},
		{"editor_save_rename_failure_reports_missing_path_class",
			test_editor_save_rename_failure_reports_missing_path_class},
		{"editor_save_rename_failure_reports_read_only_fs_class",
			test_editor_save_rename_failure_reports_read_only_fs_class},
		{"editor_save_reports_temp_cleanup_failure",
			test_editor_save_reports_temp_cleanup_failure},
		{"editor_prompt_accept_and_cancel", test_editor_prompt_accept_and_cancel},
		{"editor_prompt_accepts_utf8_input", test_editor_prompt_accepts_utf8_input},
		{"editor_prompt_filters_ascii_controls_but_keeps_non_ascii_bytes",
			test_editor_prompt_filters_ascii_controls_but_keeps_non_ascii_bytes},
		{"editor_prompt_backspace_removes_entire_utf8_codepoint",
			test_editor_prompt_backspace_removes_entire_utf8_codepoint},
		{"editor_prompt_delete_key_removes_entire_utf8_codepoint",
			test_editor_prompt_delete_key_removes_entire_utf8_codepoint},
		{"editor_prompt_fails_on_initial_alloc", test_editor_prompt_fails_on_initial_alloc},
		{"editor_prompt_fails_on_growth_alloc", test_editor_prompt_fails_on_growth_alloc},
		{"editor_insert_row_render_alloc_failure_preserves_state",
			test_editor_insert_row_render_alloc_failure_preserves_state},
			{"editor_insert_char_render_alloc_failure_preserves_state",
				test_editor_insert_char_render_alloc_failure_preserves_state},
			{"editor_insert_char_uses_document_when_row_cache_corrupt",
				test_editor_insert_char_uses_document_when_row_cache_corrupt},
			{"editor_insert_row_rejects_size_overflow",
				test_editor_insert_row_rejects_size_overflow},
			{"editor_del_char_merge_alloc_failure_preserves_state",
				test_editor_del_char_merge_alloc_failure_preserves_state},
		{"editor_insert_newline_alloc_failure_preserves_state",
			test_editor_insert_newline_alloc_failure_preserves_state},
		{"editor_save_preserves_prompt_oom_status",
			test_editor_save_preserves_prompt_oom_status},
			{"editor_save_rows_to_str_alloc_failure_preserves_state",
				test_editor_save_rows_to_str_alloc_failure_preserves_state},
			{"editor_save_uses_document_when_row_cache_corrupt",
				test_editor_save_uses_document_when_row_cache_corrupt},
			{"editor_save_tmp_path_alloc_failure_preserves_state",
				test_editor_save_tmp_path_alloc_failure_preserves_state},
		{"editor_save_parent_dir_fsync_failure_after_rename_reports_failure",
			test_editor_save_parent_dir_fsync_failure_after_rename_reports_failure},
		{"editor_save_parent_dir_open_failure_reports_failure",
			test_editor_save_parent_dir_open_failure_reports_failure},
		{"editor_save_parent_dir_close_failure_reports_failure",
			test_editor_save_parent_dir_close_failure_reports_failure},
			{"editor_save_parent_dir_fsync_success_clears_dirty",
				test_editor_save_parent_dir_fsync_success_clears_dirty},
			{"editor_recovery_autosave_activity_debounce",
				test_editor_recovery_autosave_activity_debounce},
			{"editor_recovery_cleans_snapshot_when_all_tabs_clean",
				test_editor_recovery_cleans_snapshot_when_all_tabs_clean},
			{"editor_recovery_roundtrip_restores_tabs_and_cursor_state",
				test_editor_recovery_roundtrip_restores_tabs_and_cursor_state},
			{"editor_recovery_rejects_unsupported_snapshot_version",
				test_editor_recovery_rejects_unsupported_snapshot_version},
			{"editor_startup_restore_choice_ignores_cli_args",
				test_editor_startup_restore_choice_ignores_cli_args},
			{"editor_startup_discard_choice_opens_cli_args",
				test_editor_startup_discard_choice_opens_cli_args},
			{"editor_startup_invalid_recovery_discards_and_opens_cli_args",
				test_editor_startup_invalid_recovery_discards_and_opens_cli_args},
			{"editor_drawer_root_selection_modes", test_editor_drawer_root_selection_modes},
			{"editor_drawer_tree_lists_dotfiles_sorted_and_symlink_as_file",
				test_editor_drawer_tree_lists_dotfiles_sorted_and_symlink_as_file},
				{"editor_drawer_expand_collapse_reuses_cached_children",
					test_editor_drawer_expand_collapse_reuses_cached_children},
				{"editor_drawer_root_is_not_collapsible",
					test_editor_drawer_root_is_not_collapsible},
				{"editor_drawer_open_selected_file_in_new_tab",
					test_editor_drawer_open_selected_file_in_new_tab},
				{"editor_drawer_open_selected_file_switches_existing_relative_path_tab",
					test_editor_drawer_open_selected_file_switches_existing_relative_path_tab},
				{"editor_path_absolute_dup_makes_relative_paths_absolute",
					test_editor_path_absolute_dup_makes_relative_paths_absolute},
				{"editor_path_find_marker_upward_returns_project_root",
					test_editor_path_find_marker_upward_returns_project_root},
				{"editor_drawer_open_selected_file_respects_tab_limit",
					test_editor_drawer_open_selected_file_respects_tab_limit},
				{"editor_recovery_snapshot_permissions_are_0600",
					test_editor_recovery_snapshot_permissions_are_0600},
			{"editor_recovery_clean_quit_removes_snapshot",
				test_editor_recovery_clean_quit_removes_snapshot},
			{"editor_recovery_failure_exit_keeps_snapshot",
				test_editor_recovery_failure_exit_keeps_snapshot},
				{"editor_read_key_sequences", test_editor_read_key_sequences},
				{"editor_read_key_alt_arrow_sequences", test_editor_read_key_alt_arrow_sequences},
			{"editor_read_key_sgr_mouse_events", test_editor_read_key_sgr_mouse_events},
			{"editor_read_key_returns_input_eof_event_on_closed_stdin",
				test_editor_read_key_returns_input_eof_event_on_closed_stdin},
		{"editor_read_key_escape_parse_eof_returns_input_eof_event",
			test_editor_read_key_escape_parse_eof_returns_input_eof_event},
		{"editor_read_key_returns_resize_event_when_queued",
			test_editor_read_key_returns_resize_event_when_queued},
		{"read_cursor_position_and_window_size_fallback", test_read_cursor_position_and_window_size_fallback},
		{"read_cursor_position_rejects_malformed_responses",
			test_read_cursor_position_rejects_malformed_responses},
			{"editor_refresh_window_size_clamps_tiny_terminal",
				test_editor_refresh_window_size_clamps_tiny_terminal},
			{"editor_refresh_window_size_failure_keeps_previous_dimensions",
				test_editor_refresh_window_size_failure_keeps_previous_dimensions},
			{"editor_refresh_window_size_reserves_tab_status_and_message_rows",
				test_editor_refresh_window_size_reserves_tab_status_and_message_rows},
			{"editor_keymap_load_valid_project_overrides_defaults",
				test_editor_keymap_load_valid_project_overrides_defaults},
		{"editor_keymap_load_unknown_action_falls_back_to_defaults",
			test_editor_keymap_load_unknown_action_falls_back_to_defaults},
		{"editor_keymap_load_unknown_keyspec_falls_back_to_defaults",
			test_editor_keymap_load_unknown_keyspec_falls_back_to_defaults},
		{"editor_keymap_load_duplicate_binding_falls_back_to_defaults",
			test_editor_keymap_load_duplicate_binding_falls_back_to_defaults},
		{"editor_keymap_load_malformed_toml_falls_back_to_defaults",
			test_editor_keymap_load_malformed_toml_falls_back_to_defaults},
		{"editor_keymap_global_then_project_precedence",
			test_editor_keymap_global_then_project_precedence},
		{"editor_keymap_invalid_global_ignored_when_project_valid",
			test_editor_keymap_invalid_global_ignored_when_project_valid},
			{"editor_keymap_load_configured_prefers_project_over_global",
				test_editor_keymap_load_configured_prefers_project_over_global},
			{"editor_cursor_style_load_valid_values_case_insensitive",
				test_editor_cursor_style_load_valid_values_case_insensitive},
			{"editor_cursor_style_global_then_project_precedence",
				test_editor_cursor_style_global_then_project_precedence},
			{"editor_cursor_style_invalid_values_fallback_to_bar",
				test_editor_cursor_style_invalid_values_fallback_to_bar},
			{"editor_cursor_style_load_configured_prefers_project_over_global",
				test_editor_cursor_style_load_configured_prefers_project_over_global},
			{"editor_cursor_style_invalid_setting_does_not_break_keymap_loading",
				test_editor_cursor_style_invalid_setting_does_not_break_keymap_loading},
			{"editor_syntax_theme_load_global_project_precedence",
				test_editor_syntax_theme_load_global_project_precedence},
			{"editor_syntax_theme_invalid_entries_nonfatal_and_keymap_still_loads",
				test_editor_syntax_theme_invalid_entries_nonfatal_and_keymap_still_loads},
			{"editor_keymap_load_modifier_combo_specs_case_insensitive",
				test_editor_keymap_load_modifier_combo_specs_case_insensitive},
			{"editor_keymap_load_invalid_modifier_combos_fall_back_to_defaults",
				test_editor_keymap_load_invalid_modifier_combos_fall_back_to_defaults},
			{"editor_keymap_defaults_include_tab_actions",
				test_editor_keymap_defaults_include_tab_actions},
			{"editor_keymap_defaults_include_goto_definition_action",
				test_editor_keymap_defaults_include_goto_definition_action},
			{"editor_keymap_load_accepts_goto_definition_ctrl_o",
				test_editor_keymap_load_accepts_goto_definition_ctrl_o},
			{"editor_keymap_load_rejects_ctrl_i_binding_that_conflicts_with_tab_input",
				test_editor_keymap_load_rejects_ctrl_i_binding_that_conflicts_with_tab_input},
			{"editor_keymap_load_rejects_reserved_terminal_aliases_for_other_actions",
				test_editor_keymap_load_rejects_reserved_terminal_aliases_for_other_actions},
			{"editor_keymap_load_accepts_reserved_terminal_aliases_for_matching_actions",
				test_editor_keymap_load_accepts_reserved_terminal_aliases_for_matching_actions},
			{"editor_lsp_config_defaults_and_precedence",
				test_editor_lsp_config_defaults_and_precedence},
			{"editor_lsp_config_invalid_values_fallback_defaults",
				test_editor_lsp_config_invalid_values_fallback_defaults},
			{"editor_lsp_parse_definition_response_handles_clangd_field_order",
				test_editor_lsp_parse_definition_response_handles_clangd_field_order},
			{"editor_lsp_build_initialize_request_json_is_complete",
				test_editor_lsp_build_initialize_request_json_is_complete},
			{"editor_lsp_lifecycle_lazy_start_and_non_go_buffers",
				test_editor_lsp_lifecycle_lazy_start_and_non_go_buffers},
			{"editor_lsp_lifecycle_restart_after_mock_crash",
				test_editor_lsp_lifecycle_restart_after_mock_crash},
			{"editor_lsp_lifecycle_restarts_when_switching_between_go_clangd_and_html",
				test_editor_lsp_lifecycle_restarts_when_switching_between_go_clangd_and_html},
			{"editor_lsp_lifecycle_restarts_when_clangd_workspace_root_changes",
				test_editor_lsp_lifecycle_restarts_when_clangd_workspace_root_changes},
			{"editor_lsp_lifecycle_restarts_when_html_workspace_root_changes",
				test_editor_lsp_lifecycle_restarts_when_html_workspace_root_changes},
			{"editor_lsp_document_sync_for_go_edit_save_close",
				test_editor_lsp_document_sync_for_go_edit_save_close},
			{"editor_lsp_document_sync_for_c_edit_save_close",
				test_editor_lsp_document_sync_for_c_edit_save_close},
			{"editor_lsp_document_sync_for_html_edit_save_close",
				test_editor_lsp_document_sync_for_html_edit_save_close},
			{"editor_lsp_document_sync_for_css_edit_save_close",
				test_editor_lsp_document_sync_for_css_edit_save_close},
			{"editor_lsp_full_document_change_uses_active_source",
				test_editor_lsp_full_document_change_uses_active_source},
			{"editor_lsp_document_sync_ignores_non_go_buffers",
				test_editor_lsp_document_sync_ignores_non_go_buffers},
			{"editor_lsp_html_language_id_routing_for_supported_extensions",
				test_editor_lsp_html_language_id_routing_for_supported_extensions},
			{"editor_lsp_language_id_routing_for_css_scss_and_json",
				test_editor_lsp_language_id_routing_for_css_scss_and_json},
			{"editor_lsp_eslint_diagnostics_update_and_status_summary",
				test_editor_lsp_eslint_diagnostics_update_and_status_summary},
			{"editor_lsp_eslint_diagnostics_persist_across_tab_switches",
				test_editor_lsp_eslint_diagnostics_persist_across_tab_switches},
			{"editor_process_keypress_keymap_remap_changes_dispatch",
				test_editor_process_keypress_keymap_remap_changes_dispatch},
			{"editor_process_keypress_keymap_ctrl_alt_letter_dispatches_mapped_action",
				test_editor_process_keypress_keymap_ctrl_alt_letter_dispatches_mapped_action},
			{"editor_process_keypress_ctrl_o_goto_definition_single_location",
				test_editor_process_keypress_ctrl_o_goto_definition_single_location},
			{"editor_process_keypress_ctrl_o_goto_definition_single_location_c_buffer",
				test_editor_process_keypress_ctrl_o_goto_definition_single_location_c_buffer},
			{"editor_process_keypress_ctrl_o_goto_definition_single_location_cpp_buffer",
				test_editor_process_keypress_ctrl_o_goto_definition_single_location_cpp_buffer},
			{"editor_process_keypress_ctrl_o_goto_definition_single_location_html_buffer",
				test_editor_process_keypress_ctrl_o_goto_definition_single_location_html_buffer},
			{"editor_process_keypress_ctrl_o_goto_definition_single_location_css_buffer",
				test_editor_process_keypress_ctrl_o_goto_definition_single_location_css_buffer},
			{"editor_process_keypress_ctrl_o_goto_definition_single_location_json_buffer",
				test_editor_process_keypress_ctrl_o_goto_definition_single_location_json_buffer},
			{"editor_process_keypress_goto_definition_cross_file_reuses_tab",
				test_editor_process_keypress_goto_definition_cross_file_reuses_tab},
			{"editor_process_keypress_goto_definition_cross_file_cpp_fixture_reuses_tab",
				test_editor_process_keypress_goto_definition_cross_file_cpp_fixture_reuses_tab},
			{"editor_process_keypress_goto_definition_multi_picker_selects_choice",
				test_editor_process_keypress_goto_definition_multi_picker_selects_choice},
			{"editor_process_keypress_mouse_ctrl_click_goto_definition_single_location",
				test_editor_process_keypress_mouse_ctrl_click_goto_definition_single_location},
			{"editor_process_keypress_mouse_ctrl_click_goto_definition_multi_picker_selects_choice",
				test_editor_process_keypress_mouse_ctrl_click_goto_definition_multi_picker_selects_choice},
			{"editor_process_keypress_goto_definition_timeout_error_and_no_result",
				test_editor_process_keypress_goto_definition_timeout_error_and_no_result},
			{"editor_process_keypress_goto_definition_reports_lsp_disabled",
				test_editor_process_keypress_goto_definition_reports_lsp_disabled},
			{"editor_process_keypress_goto_definition_reports_lsp_disabled_for_c",
				test_editor_process_keypress_goto_definition_reports_lsp_disabled_for_c},
			{"editor_process_keypress_goto_definition_reports_lsp_disabled_for_html",
				test_editor_process_keypress_goto_definition_reports_lsp_disabled_for_html},
			{"editor_process_keypress_mouse_ctrl_click_goto_definition_reports_lsp_disabled",
				test_editor_process_keypress_mouse_ctrl_click_goto_definition_reports_lsp_disabled},
			{"editor_process_keypress_goto_definition_startup_failure_reports_reason",
				test_editor_process_keypress_goto_definition_startup_failure_reports_reason},
			{"editor_process_keypress_mouse_ctrl_click_goto_definition_requires_saved_go_buffer",
				test_editor_process_keypress_mouse_ctrl_click_goto_definition_requires_saved_go_buffer},
			{"editor_process_keypress_goto_definition_requires_saved_c_buffer",
				test_editor_process_keypress_goto_definition_requires_saved_c_buffer},
			{"editor_process_keypress_goto_definition_reports_empty_clangd_command",
				test_editor_process_keypress_goto_definition_reports_empty_clangd_command},
			{"editor_process_keypress_goto_definition_reports_empty_html_command",
				test_editor_process_keypress_goto_definition_reports_empty_html_command},
			{"editor_process_keypress_goto_definition_missing_gopls_decline_install",
				test_editor_process_keypress_goto_definition_missing_gopls_decline_install},
			{"editor_process_keypress_goto_definition_missing_gopls_starts_install_task",
				test_editor_process_keypress_goto_definition_missing_gopls_starts_install_task},
			{"editor_process_keypress_goto_definition_missing_clangd_declines_instructions",
				test_editor_process_keypress_goto_definition_missing_clangd_declines_instructions},
			{"editor_process_keypress_goto_definition_missing_clangd_shows_install_instructions",
				test_editor_process_keypress_goto_definition_missing_clangd_shows_install_instructions},
			{"editor_process_keypress_goto_definition_missing_vscode_langservers_decline_install",
				test_editor_process_keypress_goto_definition_missing_vscode_langservers_decline_install},
			{"editor_process_keypress_goto_definition_missing_vscode_langservers_starts_install_task",
				test_editor_process_keypress_goto_definition_missing_vscode_langservers_starts_install_task},
			{"editor_process_keypress_eslint_fix_action_applies_mock_edits",
				test_editor_process_keypress_eslint_fix_action_applies_mock_edits},
			{"editor_process_keypress_eslint_fix_missing_vscode_langservers_starts_install_task",
				test_editor_process_keypress_eslint_fix_missing_vscode_langservers_starts_install_task},
			{"editor_task_log_read_only_search_and_copy",
				test_editor_task_log_read_only_search_and_copy},
			{"editor_task_log_document_stays_authoritative",
				test_editor_task_log_document_stays_authoritative},
			{"editor_task_log_streams_output_while_inactive",
				test_editor_task_log_streams_output_while_inactive},
			{"editor_task_runner_merges_stderr_and_close_requires_confirmation",
				test_editor_task_runner_merges_stderr_and_close_requires_confirmation},
			{"editor_task_runner_truncates_large_output",
				test_editor_task_runner_truncates_large_output},
			{"editor_process_keypress_resize_drawer_shortcuts",
				test_editor_process_keypress_resize_drawer_shortcuts},
			{"editor_process_keypress_toggle_drawer_shortcut_collapses_and_expands",
				test_editor_process_keypress_toggle_drawer_shortcut_collapses_and_expands},
			{"editor_tabs_switch_restores_per_tab_state",
				test_editor_tabs_switch_restores_per_tab_state},
			{"editor_tab_close_last_tab_keeps_one_empty_tab",
				test_editor_tab_close_last_tab_keeps_one_empty_tab},
			{"editor_process_keypress_ctrl_w_dirty_requires_second_press",
				test_editor_process_keypress_ctrl_w_dirty_requires_second_press},
			{"editor_process_keypress_close_tab_confirmation_resets_on_other_action",
				test_editor_process_keypress_close_tab_confirmation_resets_on_other_action},
				{"editor_process_keypress_ctrl_q_checks_dirty_tabs_globally",
					test_editor_process_keypress_ctrl_q_checks_dirty_tabs_globally},
				{"editor_process_keypress_tab_actions_new_next_prev",
					test_editor_process_keypress_tab_actions_new_next_prev},
				{"editor_tab_open_file_reuses_active_clean_empty_buffer",
					test_editor_tab_open_file_reuses_active_clean_empty_buffer},
				{"editor_tab_open_file_opens_new_tab_when_empty_buffer_is_inactive",
					test_editor_tab_open_file_opens_new_tab_when_empty_buffer_is_inactive},
					{"editor_process_keypress_focus_drawer_and_arrow_navigation",
						test_editor_process_keypress_focus_drawer_and_arrow_navigation},
				{"editor_process_keypress_drawer_enter_toggles_directory",
					test_editor_process_keypress_drawer_enter_toggles_directory},
			{"editor_process_keypress_drawer_enter_opens_file_in_new_tab",
				test_editor_process_keypress_drawer_enter_opens_file_in_new_tab},
			{"editor_process_keypress_insert_move_and_backspace",
				test_editor_process_keypress_insert_move_and_backspace},
			{"editor_process_keypress_ctrl_j_does_not_insert_newline",
				test_editor_process_keypress_ctrl_j_does_not_insert_newline},
			{"editor_process_keypress_tab_inserts_literal_tab",
				test_editor_process_keypress_tab_inserts_literal_tab},
			{"editor_process_keypress_utf8_bytes_insert_verbatim",
				test_editor_process_keypress_utf8_bytes_insert_verbatim},
			{"editor_process_keypress_delete_key", test_editor_process_keypress_delete_key},
			{"editor_process_keypress_arrow_down_keeps_visual_column",
				test_editor_process_keypress_arrow_down_keeps_visual_column},
		{"editor_process_keypress_ctrl_s_saves_file", test_editor_process_keypress_ctrl_s_saves_file},
		{"editor_process_keypress_resize_event_updates_window_size",
			test_editor_process_keypress_resize_event_updates_window_size},
				{"editor_process_keypress_mouse_left_click_places_cursor_with_offsets",
					test_editor_process_keypress_mouse_left_click_places_cursor_with_offsets},
				{"editor_process_keypress_mouse_ctrl_click_does_not_start_drag_selection",
					test_editor_process_keypress_mouse_ctrl_click_does_not_start_drag_selection},
				{"editor_process_keypress_mouse_left_click_ignores_non_text_rows",
					test_editor_process_keypress_mouse_left_click_ignores_non_text_rows},
				{"editor_process_keypress_mouse_left_click_ignores_indicator_padding_columns",
					test_editor_process_keypress_mouse_left_click_ignores_indicator_padding_columns},
					{"editor_process_keypress_mouse_drawer_click_selects_and_toggles_directory",
						test_editor_process_keypress_mouse_drawer_click_selects_and_toggles_directory},
				{"editor_process_keypress_mouse_click_expands_collapsed_drawer",
					test_editor_process_keypress_mouse_click_expands_collapsed_drawer},
				{"editor_process_keypress_mouse_drawer_single_file_click_opens_preview_tab",
					test_editor_process_keypress_mouse_drawer_single_file_click_opens_preview_tab},
				{"editor_drawer_open_selected_file_in_preview_reuses_preview_tab",
					test_editor_drawer_open_selected_file_in_preview_reuses_preview_tab},
				{"editor_process_keypress_mouse_drawer_double_click_file_pins_preview_tab",
					test_editor_process_keypress_mouse_drawer_double_click_file_pins_preview_tab},
				{"editor_process_keypress_mouse_top_row_click_switches_tab",
					test_editor_process_keypress_mouse_top_row_click_switches_tab},
				{"editor_process_keypress_mouse_top_row_click_uses_variable_tab_layout",
					test_editor_process_keypress_mouse_top_row_click_uses_variable_tab_layout},
			{"editor_process_keypress_mouse_drag_on_splitter_resizes_drawer",
				test_editor_process_keypress_mouse_drag_on_splitter_resizes_drawer},
			{"editor_process_keypress_mouse_wheel_scrolls_three_lines_and_clamps",
				test_editor_process_keypress_mouse_wheel_scrolls_three_lines_and_clamps},
			{"editor_process_keypress_mouse_wheel_scrolls_horizontally_and_clamps",
				test_editor_process_keypress_mouse_wheel_scrolls_horizontally_and_clamps},
			{"editor_process_keypress_mouse_wheel_scrolls_drawer_when_hovered",
				test_editor_process_keypress_mouse_wheel_scrolls_drawer_when_hovered},
			{"editor_process_keypress_mouse_wheel_scrolls_drawer_with_empty_buffer",
				test_editor_process_keypress_mouse_wheel_scrolls_drawer_with_empty_buffer},
			{"editor_process_keypress_mouse_wheel_scrolls_text_when_hovered_even_if_drawer_focused",
				test_editor_process_keypress_mouse_wheel_scrolls_text_when_hovered_even_if_drawer_focused},
			{"editor_process_keypress_page_up_down_scroll_viewport_without_moving_cursor",
				test_editor_process_keypress_page_up_down_scroll_viewport_without_moving_cursor},
			{"editor_process_keypress_ctrl_arrow_scrolls_horizontally_without_moving_cursor",
				test_editor_process_keypress_ctrl_arrow_scrolls_horizontally_without_moving_cursor},
			{"editor_process_keypress_free_scroll_can_leave_cursor_offscreen",
				test_editor_process_keypress_free_scroll_can_leave_cursor_offscreen},
			{"editor_process_keypress_cursor_move_resyncs_follow_scroll",
				test_editor_process_keypress_cursor_move_resyncs_follow_scroll},
			{"editor_process_keypress_edit_resyncs_follow_scroll",
				test_editor_process_keypress_edit_resyncs_follow_scroll},
			{"editor_process_keypress_mouse_click_clears_existing_selection",
				test_editor_process_keypress_mouse_click_clears_existing_selection},
		{"editor_process_keypress_mouse_drag_starts_selection_without_ctrl_b",
			test_editor_process_keypress_mouse_drag_starts_selection_without_ctrl_b},
		{"editor_process_keypress_mouse_press_without_drag_keeps_click_behavior",
			test_editor_process_keypress_mouse_press_without_drag_keeps_click_behavior},
		{"editor_process_keypress_mouse_drag_resets_existing_selection_anchor",
			test_editor_process_keypress_mouse_drag_resets_existing_selection_anchor},
		{"editor_process_keypress_mouse_drag_clamps_to_viewport_without_autoscroll",
			test_editor_process_keypress_mouse_drag_clamps_to_viewport_without_autoscroll},
		{"editor_process_keypress_mouse_drag_honors_rowoff_and_coloff",
			test_editor_process_keypress_mouse_drag_honors_rowoff_and_coloff},
		{"editor_process_keypress_mouse_release_stops_drag_session",
			test_editor_process_keypress_mouse_release_stops_drag_session},
		{"editor_prompt_ignores_mouse_events", test_editor_prompt_ignores_mouse_events},
		{"editor_prompt_ignores_resize_events", test_editor_prompt_ignores_resize_events},
		{"editor_process_keypress_ctrl_b_toggles_selection_mode",
			test_editor_process_keypress_ctrl_b_toggles_selection_mode},
			{"editor_selection_range_tracks_cursor_movement",
				test_editor_selection_range_tracks_cursor_movement},
			{"editor_extract_range_text_uses_document_when_row_cache_corrupt",
				test_editor_extract_range_text_uses_document_when_row_cache_corrupt},
			{"editor_delete_range_uses_document_when_row_cache_corrupt",
				test_editor_delete_range_uses_document_when_row_cache_corrupt},
			{"editor_process_keypress_ctrl_c_copies_single_line_selection",
				test_editor_process_keypress_ctrl_c_copies_single_line_selection},
		{"editor_process_keypress_ctrl_c_copies_multiline_selection",
			test_editor_process_keypress_ctrl_c_copies_multiline_selection},
		{"editor_process_keypress_ctrl_x_cuts_selection_and_updates_clipboard",
			test_editor_process_keypress_ctrl_x_cuts_selection_and_updates_clipboard},
		{"editor_process_keypress_ctrl_d_deletes_selection_without_overwriting_clipboard",
			test_editor_process_keypress_ctrl_d_deletes_selection_without_overwriting_clipboard},
		{"editor_process_keypress_ctrl_v_pastes_clipboard_text",
			test_editor_process_keypress_ctrl_v_pastes_clipboard_text},
		{"editor_process_keypress_ctrl_v_pastes_multiline_clipboard_text",
			test_editor_process_keypress_ctrl_v_pastes_multiline_clipboard_text},
		{"editor_process_keypress_ctrl_v_empty_clipboard_is_noop",
			test_editor_process_keypress_ctrl_v_empty_clipboard_is_noop},
		{"editor_clipboard_sync_osc52_plain_sequence",
			test_editor_clipboard_sync_osc52_plain_sequence},
		{"editor_clipboard_sync_osc52_tmux_wrapped_sequence",
			test_editor_clipboard_sync_osc52_tmux_wrapped_sequence},
		{"editor_clipboard_sync_osc52_screen_wrapped_sequence",
			test_editor_clipboard_sync_osc52_screen_wrapped_sequence},
		{"editor_clipboard_sync_osc52_mode_off_emits_nothing",
			test_editor_clipboard_sync_osc52_mode_off_emits_nothing},
		{"editor_clipboard_sync_osc52_auto_mode_skips_non_tty",
			test_editor_clipboard_sync_osc52_auto_mode_skips_non_tty},
		{"editor_clipboard_sync_osc52_payload_cap_skips_external_write",
			test_editor_clipboard_sync_osc52_payload_cap_skips_external_write},
		{"editor_process_keypress_ctrl_v_clears_selection_mode",
			test_editor_process_keypress_ctrl_v_clears_selection_mode},
		{"editor_process_keypress_ctrl_v_undo_roundtrip_single_step",
			test_editor_process_keypress_ctrl_v_undo_roundtrip_single_step},
		{"editor_process_keypress_selection_ops_noop_without_selection",
			test_editor_process_keypress_selection_ops_noop_without_selection},
		{"editor_process_keypress_escape_clears_selection_mode",
			test_editor_process_keypress_escape_clears_selection_mode},
		{"editor_process_keypress_edit_ops_clear_selection_mode",
			test_editor_process_keypress_edit_ops_clear_selection_mode},
		{"editor_process_keypress_ctrl_z_ctrl_y_roundtrip_after_cut",
			test_editor_process_keypress_ctrl_z_ctrl_y_roundtrip_after_cut},
		{"editor_process_keypress_ctrl_c_oom_preserves_buffer",
			test_editor_process_keypress_ctrl_c_oom_preserves_buffer},
		{"editor_process_keypress_ctrl_g_jumps_to_line_and_sets_col_zero",
			test_editor_process_keypress_ctrl_g_jumps_to_line_and_sets_col_zero},
		{"editor_process_keypress_ctrl_g_clamps_to_last_line",
			test_editor_process_keypress_ctrl_g_clamps_to_last_line},
		{"editor_process_keypress_ctrl_g_rejects_invalid_input",
			test_editor_process_keypress_ctrl_g_rejects_invalid_input},
		{"editor_process_keypress_ctrl_g_escape_cancels",
			test_editor_process_keypress_ctrl_g_escape_cancels},
		{"editor_process_keypress_ctrl_g_empty_buffer_sets_status",
			test_editor_process_keypress_ctrl_g_empty_buffer_sets_status},
		{"editor_process_keypress_ctrl_g_breaks_undo_typed_run_group",
			test_editor_process_keypress_ctrl_g_breaks_undo_typed_run_group},
		{"editor_process_keypress_ctrl_q_exits_promptly",
			test_editor_process_keypress_ctrl_q_exits_promptly},
		{"editor_process_keypress_ctrl_q_restores_cursor_shape",
			test_editor_process_keypress_ctrl_q_restores_cursor_shape},
		{"editor_process_keypress_ctrl_q_dirty_requires_second_press",
			test_editor_process_keypress_ctrl_q_dirty_requires_second_press},
		{"editor_process_keypress_eof_exits_promptly_with_failure",
			test_editor_process_keypress_eof_exits_promptly_with_failure},
		{"editor_process_keypress_eof_restores_terminal_visual_state",
			test_editor_process_keypress_eof_restores_terminal_visual_state},
		{"editor_process_keypress_prompt_eof_exits_with_failure",
			test_editor_process_keypress_prompt_eof_exits_with_failure},
		{"process_terminates_promptly_on_sigterm",
			test_process_terminates_promptly_on_sigterm},
		{"editor_process_keypress_ctrl_f_incremental_find_first_match",
			test_editor_process_keypress_ctrl_f_incremental_find_first_match},
		{"editor_process_keypress_ctrl_f_arrow_navigation_wraps",
			test_editor_process_keypress_ctrl_f_arrow_navigation_wraps},
		{"editor_process_keypress_ctrl_f_escape_restores_cursor_and_clears_match",
			test_editor_process_keypress_ctrl_f_escape_restores_cursor_and_clears_match},
		{"editor_process_keypress_ctrl_f_enter_keeps_active_match",
			test_editor_process_keypress_ctrl_f_enter_keeps_active_match},
		{"editor_process_keypress_ctrl_f_no_match_preserves_cursor_and_sets_status",
			test_editor_process_keypress_ctrl_f_no_match_preserves_cursor_and_sets_status},
		{"editor_process_keypress_ctrl_z_ctrl_y_roundtrip_typed_run",
			test_editor_process_keypress_ctrl_z_ctrl_y_roundtrip_typed_run},
		{"editor_process_keypress_ctrl_z_group_break_on_navigation",
			test_editor_process_keypress_ctrl_z_group_break_on_navigation},
		{"editor_process_keypress_ctrl_z_for_delete_and_newline_steps",
			test_editor_process_keypress_ctrl_z_for_delete_and_newline_steps},
		{"editor_process_keypress_ctrl_y_clears_after_new_edit",
			test_editor_process_keypress_ctrl_y_clears_after_new_edit},
		{"editor_process_keypress_ctrl_z_ctrl_y_empty_stack_status",
			test_editor_process_keypress_ctrl_z_ctrl_y_empty_stack_status},
		{"editor_process_keypress_ctrl_z_history_cap_eviction",
			test_editor_process_keypress_ctrl_z_history_cap_eviction},
		{"editor_process_keypress_ctrl_z_capture_oom_preserves_state",
			test_editor_process_keypress_ctrl_z_capture_oom_preserves_state},
		{"editor_process_keypress_ctrl_z_restore_oom_preserves_state",
			test_editor_process_keypress_ctrl_z_restore_oom_preserves_state},
				{"editor_refresh_screen_contains_expected_sequences",
					test_editor_refresh_screen_contains_expected_sequences},
				{"editor_refresh_screen_file_row_frame_diff_updates_only_changed_rows",
					test_editor_refresh_screen_file_row_frame_diff_updates_only_changed_rows},
				{"editor_refresh_screen_uses_configured_cursor_style",
					test_editor_refresh_screen_uses_configured_cursor_style},
			{"editor_refresh_screen_highlights_active_search_match",
				test_editor_refresh_screen_highlights_active_search_match},
			{"editor_refresh_screen_applies_syntax_highlighting_for_c_tokens",
				test_editor_refresh_screen_applies_syntax_highlighting_for_c_tokens},
			{"editor_refresh_screen_repo_buffer_c_stays_highlighted",
				test_editor_refresh_screen_repo_buffer_c_stays_highlighted},
			{"editor_refresh_screen_applies_syntax_highlighting_for_shell_tokens",
				test_editor_refresh_screen_applies_syntax_highlighting_for_shell_tokens},
			{"editor_refresh_screen_applies_syntax_highlighting_for_html_with_injections",
				test_editor_refresh_screen_applies_syntax_highlighting_for_html_with_injections},
			{"editor_refresh_screen_html_text_apostrophe_not_javascript_string",
				test_editor_refresh_screen_html_text_apostrophe_not_javascript_string},
			{"editor_refresh_screen_applies_syntax_highlighting_for_javascript_tokens",
				test_editor_refresh_screen_applies_syntax_highlighting_for_javascript_tokens},
			{"editor_refresh_screen_applies_syntax_highlighting_for_css_tokens",
				test_editor_refresh_screen_applies_syntax_highlighting_for_css_tokens},
			{"editor_refresh_screen_applies_syntax_highlighting_for_go_tokens",
				test_editor_refresh_screen_applies_syntax_highlighting_for_go_tokens},
				{"editor_refresh_screen_javascript_predicates_and_locals",
					test_editor_refresh_screen_javascript_predicates_and_locals},
				{"editor_refresh_screen_javascript_predicates_repeat_refresh",
					test_editor_refresh_screen_javascript_predicates_repeat_refresh},
				{"editor_refresh_screen_reports_query_budget_throttle_status",
					test_editor_refresh_screen_reports_query_budget_throttle_status},
			{"editor_refresh_screen_plain_text_file_has_no_syntax_highlighting",
				test_editor_refresh_screen_plain_text_file_has_no_syntax_highlighting},
			{"editor_refresh_screen_selection_and_search_override_syntax_colors",
				test_editor_refresh_screen_selection_and_search_override_syntax_colors},
			{"editor_refresh_screen_shell_selection_and_search_override_syntax_colors",
				test_editor_refresh_screen_shell_selection_and_search_override_syntax_colors},
			{"editor_refresh_screen_highlight_alignment_with_escaped_controls",
				test_editor_refresh_screen_highlight_alignment_with_escaped_controls},
			{"editor_refresh_screen_escapes_filename_controls",
				test_editor_refresh_screen_escapes_filename_controls},
			{"editor_refresh_screen_escapes_status_controls",
				test_editor_refresh_screen_escapes_status_controls},
			{"editor_refresh_screen_escapes_file_content_controls",
				test_editor_refresh_screen_escapes_file_content_controls},
			{"editor_refresh_screen_renders_tab_bar_with_overflow_and_sanitized_labels",
				test_editor_refresh_screen_renders_tab_bar_with_overflow_and_sanitized_labels},
			{"editor_refresh_screen_tab_labels_middle_truncate_at_25_cols",
				test_editor_refresh_screen_tab_labels_middle_truncate_at_25_cols},
			{"editor_refresh_screen_preview_tab_label_uses_italics",
				test_editor_refresh_screen_preview_tab_label_uses_italics},
			{"editor_tab_layout_width_includes_right_label_padding",
				test_editor_tab_layout_width_includes_right_label_padding},
			{"editor_tabs_align_view_keeps_active_visible_with_variable_widths",
				test_editor_tabs_align_view_keeps_active_visible_with_variable_widths},
			{"editor_refresh_screen_renders_drawer_entries_and_selection",
				test_editor_refresh_screen_renders_drawer_entries_and_selection},
			{"editor_refresh_screen_drawer_hides_selection_marker_when_unfocused",
				test_editor_refresh_screen_drawer_hides_selection_marker_when_unfocused},
			{"editor_refresh_screen_drawer_active_file_uses_inverted_background",
				test_editor_refresh_screen_drawer_active_file_uses_inverted_background},
			{"editor_refresh_screen_drawer_collapsed_renders_expand_indicator",
				test_editor_refresh_screen_drawer_collapsed_renders_expand_indicator},
			{"editor_refresh_screen_drawer_renders_unicode_tree_connectors",
				test_editor_refresh_screen_drawer_renders_unicode_tree_connectors},
			{"editor_refresh_screen_drawer_selected_overflow_spills_into_text_area",
				test_editor_refresh_screen_drawer_selected_overflow_spills_into_text_area},
				{"editor_refresh_screen_drawer_splitter_spans_editor_rows",
					test_editor_refresh_screen_drawer_splitter_spans_editor_rows},
				{"editor_refresh_screen_cursor_column_offsets_for_drawer",
					test_editor_refresh_screen_cursor_column_offsets_for_drawer},
				{"editor_refresh_screen_hides_cursor_when_drawer_focused",
					test_editor_refresh_screen_hides_cursor_when_drawer_focused},
				{"editor_refresh_screen_hides_cursor_when_offscreen_in_free_scroll",
					test_editor_refresh_screen_hides_cursor_when_offscreen_in_free_scroll},
			{"editor_drawer_layout_clamps_tiny_widths", test_editor_drawer_layout_clamps_tiny_widths},
			{"editor_refresh_screen_highlights_active_selection_spans",
				test_editor_refresh_screen_highlights_active_selection_spans},
			{"editor_refresh_screen_hides_expired_message", test_editor_refresh_screen_hides_expired_message},
			{"editor_refresh_screen_shows_right_overflow_indicator",
				test_editor_refresh_screen_shows_right_overflow_indicator},
			{"editor_refresh_screen_shows_left_overflow_indicator",
				test_editor_refresh_screen_shows_left_overflow_indicator},
			{"editor_refresh_screen_shows_both_horizontal_overflow_indicators",
				test_editor_refresh_screen_shows_both_horizontal_overflow_indicators},
			{"editor_refresh_screen_non_file_rows_do_not_show_overflow_indicators",
				test_editor_refresh_screen_non_file_rows_do_not_show_overflow_indicators},
			{"editor_refresh_screen_out_of_buffer_tildes_are_gray",
				test_editor_refresh_screen_out_of_buffer_tildes_are_gray},
			{"editor_refresh_screen_updates_horizontal_scroll",
				test_editor_refresh_screen_updates_horizontal_scroll},
		{"editor_refresh_screen_slice_after_multibyte_scroll",
			test_editor_refresh_screen_slice_after_multibyte_scroll},
		{"editor_refresh_screen_cursor_sequence_not_truncated_by_window_width",
			test_editor_refresh_screen_cursor_sequence_not_truncated_by_window_width},
		{"editor_refresh_screen_status_bar_single_row_percent",
			test_editor_refresh_screen_status_bar_single_row_percent},
		{"editor_refresh_screen_status_bar_cursor_multibyte_col",
			test_editor_refresh_screen_status_bar_cursor_multibyte_col},
		{"editor_refresh_screen_status_bar_cursor_tab_display_col",
			test_editor_refresh_screen_status_bar_cursor_tab_display_col},
		{"editor_refresh_screen_status_bar_shows_full_path_when_space_allows",
			test_editor_refresh_screen_status_bar_shows_full_path_when_space_allows},
		{"editor_refresh_screen_status_bar_truncates_prefix_keeps_basename_visible",
			test_editor_refresh_screen_status_bar_truncates_prefix_keeps_basename_visible},
		{"editor_refresh_screen_reports_oom_without_crash",
			test_editor_refresh_screen_reports_oom_without_crash},
	};

	int total = (int)(sizeof(tests) / sizeof(tests[0]));
	int passed = 0;

	for (int i = 0; i < total; i++) {
		reset_editor_state();
		int failed = tests[i].run();
		reset_editor_state();

		if (failed == 0) {
			passed++;
			printf("PASS %s\n", tests[i].name);
		} else {
			printf("FAIL %s\n", tests[i].name);
		}
	}

	printf("\n%d/%d tests passed\n", passed, total);
	return (passed == total) ? EXIT_SUCCESS : EXIT_FAILURE;
}
