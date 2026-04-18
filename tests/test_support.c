#include "test_support.h"

struct editorConfig E;


int count_tmp_save_artifacts(const char *target_path) {
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

int remove_tmp_save_artifacts(const char *target_path) {
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

int write_fixture_to_temp_path(char path_buf[], int suffix_len,
		const char *fixture_relative_path);

int write_temp_file_with_suffix(char path_buf[], size_t path_buf_size, const char *prefix,
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

int copy_fixture_to_temp_file_with_suffix(char path_buf[], size_t path_buf_size,
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

int copy_fixture_to_path(const char *dest_path, const char *fixture_relative_path) {
	if (dest_path == NULL || fixture_relative_path == NULL) {
		return 0;
	}
	return copyTestFixtureToPath(fixture_relative_path, dest_path);
}

int write_temp_go_file(char path_buf[], size_t path_buf_size, const char *content) {
	return write_temp_file_with_suffix(path_buf, path_buf_size, "rotide-test-go-lsp-", ".go",
			content);
}

int write_temp_c_file(char path_buf[], size_t path_buf_size, const char *content) {
	return write_temp_file_with_suffix(path_buf, path_buf_size, "rotide-test-c-lsp-", ".c",
			content);
}

int write_temp_html_file(char path_buf[], size_t path_buf_size, const char *content) {
	return write_temp_file_with_suffix(path_buf, path_buf_size, "rotide-test-html-lsp-", ".html",
			content);
}

int write_temp_text_file(char path_buf[], size_t path_buf_size, const char *content) {
	return write_temp_file_with_suffix(path_buf, path_buf_size, "rotide-test-lsp-", ".txt",
			content);
}

char *dup_active_source_text(size_t *len_out) {
	struct editorTextSource source = {0};
	if (!editorBuildActiveTextSource(&source)) {
		if (len_out != NULL) {
			*len_out = 0;
		}
		return NULL;
	}
	return editorTextSourceDupRange(&source, 0, source.length, len_out);
}

int assert_active_source_matches_rows(void) {
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

int set_active_search_match(int row, int col, int len) {
	size_t offset = 0;
	if (!editorBufferPosToOffset(row, col, &offset)) {
		return 0;
	}
	E.search_match_offset = offset;
	E.search_match_len = len;
	return 1;
}

int assert_active_search_match(int row, int col, int len) {
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

int set_selection_anchor(int row, int col) {
	return editorBufferPosToOffset(row, col, &E.selection_anchor_offset);
}

int assert_selection_anchor(int row, int col) {
	int anchor_row = -1;
	int anchor_col = -1;
	ASSERT_TRUE(editorBufferOffsetToPos(E.selection_anchor_offset, &anchor_row, &anchor_col));
	ASSERT_EQ_INT(row, anchor_row);
	ASSERT_EQ_INT(col, anchor_col);
	return 0;
}

int write_fixture_to_temp_path(char path_buf[], int suffix_len,
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

int editor_process_keypress_with_input_silent(const char *input, size_t len) {
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

int wait_for_task_completion_with_timeout(int timeout_ms) {
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

int editor_process_single_key(int key) {
	char c = (char)key;
	return editor_process_keypress_with_input(&c, 1);
}

int format_sgr_mouse_event(char *buf, size_t bufsz, int cb, int x, int y, char suffix) {
	int written = snprintf(buf, bufsz, "\x1b[<%d;%d;%d%c", cb, x, y, suffix);
	return written > 0 && (size_t)written < bufsz;
}

int wait_for_child_exit_with_timeout(pid_t pid, int timeout_ms, int *status_out) {
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

int backup_env_var(struct envVarBackup *backup, const char *name) {
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

int restore_env_var(struct envVarBackup *backup) {
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

int write_text_file(const char *path, const char *text) {
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd == -1) {
		return 0;
	}

	size_t len = strlen(text);
	int write_ok = write_all(fd, text, len) == 0;
	int close_ok = close(fd) == 0;
	return write_ok && close_ok;
}

char *build_repeated_text(const char *line, int repeats, size_t *len_out) {
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

int path_join(char *buf, size_t bufsz, const char *dir, const char *name) {
	int written = snprintf(buf, bufsz, "%s/%s", dir, name);
	return written >= 0 && (size_t)written < bufsz;
}

int make_dir(const char *path) {
	return mkdir(path, 0700) == 0 || errno == EEXIST;
}

int find_drawer_entry(const char *name, int *idx_out, struct editorDrawerEntryView *view_out) {
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

void remove_files_in_dir(const char *dir_path) {
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

void cleanup_recovery_test_env(struct recoveryTestEnv *env) {
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

int setup_recovery_test_env(struct recoveryTestEnv *env) {
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
