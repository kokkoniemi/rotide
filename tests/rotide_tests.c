#include "rotide.h"

#include "buffer.h"
#include "input.h"
#include "keymap.h"
#include "output.h"
#include "terminal.h"
#include "alloc_test_hooks.h"
#include "save_syscalls_test_hooks.h"
#include "test_helpers.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
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

static int editor_process_single_key(int key) {
	char c = (char)key;
	return editor_process_keypress_with_input(&c, 1);
}

static int install_synthetic_single_row(int size, int rsize) {
	struct erow *rows = calloc(1, sizeof(*rows));
	if (rows == NULL) {
		return 0;
	}

	rows[0].size = size;
	rows[0].rsize = rsize;
	rows[0].chars = strdup("");
	rows[0].render = strdup("");
	if (rows[0].chars == NULL || rows[0].render == NULL) {
		free(rows[0].chars);
		free(rows[0].render);
		free(rows);
		return 0;
	}

	E.rows = rows;
	E.numrows = 1;
	return 1;
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

static int path_join(char *buf, size_t bufsz, const char *dir, const char *name) {
	int written = snprintf(buf, bufsz, "%s/%s", dir, name);
	return written >= 0 && (size_t)written < bufsz;
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
	ASSERT_EQ_INT(0, E.dirty);
	add_row("one");
	add_row("two");
	ASSERT_EQ_INT(2, E.numrows);
	ASSERT_EQ_INT(2, E.dirty);

	editorInsertRow(-1, "noop", 4);
	ASSERT_EQ_INT(2, E.numrows);
	ASSERT_EQ_INT(2, E.dirty);

	editorDeleteRow(0);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("two", E.rows[0].chars);
	ASSERT_EQ_INT(3, E.dirty);
	return 0;
}

static int test_editor_delete_row_rejects_idx_at_numrows(void) {
	add_row("only");
	E.dirty = 0;

	editorDeleteRow(E.numrows);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("only", E.rows[0].chars);
	ASSERT_EQ_INT(0, E.dirty);
	return 0;
}

static int test_insert_and_delete_chars(void) {
	add_row("abc");
	E.dirty = 0;

	struct erow *row = &E.rows[0];
	editorInsertCharAt(row, 1, 'X');
	ASSERT_EQ_STR("aXbc", row->chars);
	ASSERT_EQ_INT(1, E.dirty);

	editorDelCharAt(row, 2);
	ASSERT_EQ_STR("aXc", row->chars);
	ASSERT_EQ_INT(2, E.dirty);

	editorDelCharsAt(row, 1, 2);
	ASSERT_EQ_STR("a", row->chars);
	ASSERT_EQ_INT(3, E.dirty);

	editorDelCharsAt(row, 1, 0);
	ASSERT_EQ_STR("a", row->chars);
	ASSERT_EQ_INT(3, E.dirty);
	return 0;
}

static int test_editor_del_char_at_rejects_idx_at_row_size(void) {
	add_row("abc");
	E.dirty = 0;

	struct erow *row = &E.rows[0];
	editorDelCharAt(row, row->size);
	ASSERT_EQ_INT(3, row->size);
	ASSERT_EQ_STR("abc", row->chars);
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
	add_row("def");
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
	return 0;
}

static int test_editor_rows_to_str_rejects_oversized_total(void) {
	ASSERT_TRUE(install_synthetic_single_row(INT_MAX, 0));

	size_t buflen = 0;
	errno = 0;
	char *joined = editorRowsToStr(&buflen);
	ASSERT_TRUE(joined == NULL);
	ASSERT_EQ_INT(0, buflen);
	ASSERT_EQ_INT(EOVERFLOW, errno);
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

	editorTestAllocFailAfter(2);
	editorInsertRow(1, "xyz", 3);

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

	editorTestAllocFailAfter(1);
	editorInsertCharAt(&E.rows[0], 1, 'X');

	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_INT(2, E.rows[0].size);
	ASSERT_EQ_STR("ab", E.rows[0].chars);
	ASSERT_EQ_STR("ab", E.rows[0].render);
	ASSERT_EQ_INT(0, E.dirty);
	ASSERT_EQ_STR("Out of memory", E.statusmsg);
	return 0;
}

static int test_editor_insert_char_rejects_size_overflow(void) {
	ASSERT_TRUE(install_synthetic_single_row(INT_MAX, 0));
	E.dirty = 0;

	editorInsertCharAt(&E.rows[0], 0, 'X');

	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_INT(INT_MAX, E.rows[0].size);
	ASSERT_EQ_INT(0, E.dirty);
	ASSERT_EQ_STR("Operation too large", E.statusmsg);
	return 0;
}

static int test_editor_insert_row_rejects_size_overflow(void) {
	char c = 'x';
	E.dirty = 0;

	editorInsertRow(0, &c, (size_t)INT_MAX + 1);

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

static int test_editor_save_rejects_oversized_buffer(void) {
	char path[] = "/tmp/rotide-test-save-too-large-XXXXXX";
	int fd = mkstemp(path);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(close(fd) == 0);

	ASSERT_TRUE(install_synthetic_single_row(INT_MAX, 0));
	E.filename = strdup(path);
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 1;

	editorSave();

	ASSERT_EQ_INT(1, E.dirty);
	ASSERT_EQ_STR("File too large", E.statusmsg);

	size_t content_len = 0;
	char *contents = read_file_contents(path, &content_len);
	ASSERT_TRUE(contents != NULL);
	ASSERT_EQ_INT(0, content_len);

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
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorTabsInit());

	add_row("quit-cleanup");
	E.dirty = 1;
	E.recovery_last_autosave_time = 0;
	editorRecoveryMaybeAutosaveOnActivity();

	const char *recovery_path = editorRecoveryPath();
	ASSERT_TRUE(recovery_path != NULL);
	ASSERT_TRUE(access(recovery_path, F_OK) == 0);

	E.dirty = 0;
	pid_t pid = fork();
	ASSERT_TRUE(pid != -1);
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

	int status = 0;
	ASSERT_TRUE(wait_for_child_exit_with_timeout(pid, 1500, &status) == 0);
	ASSERT_TRUE(WIFEXITED(status));
	ASSERT_EQ_INT(EXIT_SUCCESS, WEXITSTATUS(status));
	ASSERT_TRUE(access(recovery_path, F_OK) == -1);

	cleanup_recovery_test_env(&env);
	return 0;
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
	char left_drag[] = "\x1b[<32;6;4M";
	char left_release[] = "\x1b[<0;6;4m";
	char wheel_up[] = "\x1b[<64;7;2M";
	char wheel_down[] = "\x1b[<65;4;9M";
	char modifier_drag_then_plain[] = "\x1b[<36;1;1MZ";
	char unsupported_then_plain[] = "\x1b[<2;1;1MY";

	ASSERT_TRUE(editor_read_key_with_input(left_click, sizeof(left_click) - 1, &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_LEFT_PRESS, event.kind);
	ASSERT_EQ_INT(5, event.x);
	ASSERT_EQ_INT(3, event.y);
	ASSERT_EQ_INT(0, editorConsumeMouseEvent(&event));

	ASSERT_TRUE(editor_read_key_with_input(left_drag, sizeof(left_drag) - 1, &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_LEFT_DRAG, event.kind);
	ASSERT_EQ_INT(6, event.x);
	ASSERT_EQ_INT(4, event.y);

	ASSERT_TRUE(editor_read_key_with_input(left_release, sizeof(left_release) - 1, &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_LEFT_RELEASE, event.kind);
	ASSERT_EQ_INT(6, event.x);
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
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, EDITOR_ALT_LETTER_KEY('b'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_MOVE_LEFT, action);
	ASSERT_TRUE(editorKeymapLookupAction(&keymap, EDITOR_CTRL_ALT_LETTER_KEY('z'), &action));
	ASSERT_EQ_INT(EDITOR_ACTION_MOVE_RIGHT, action);

	char binding[24];
	ASSERT_TRUE(editorKeymapFormatBinding(&keymap, EDITOR_ACTION_MOVE_LEFT, binding, sizeof(binding)));
	ASSERT_EQ_STR("Alt-B", binding);
	ASSERT_TRUE(editorKeymapFormatBinding(&keymap, EDITOR_ACTION_MOVE_RIGHT, binding, sizeof(binding)));
	ASSERT_EQ_STR("Ctrl-Alt-Z", binding);
	ASSERT_TRUE(editorKeymapFormatBinding(&keymap, EDITOR_ACTION_PREV_TAB, binding, sizeof(binding)));
	ASSERT_EQ_STR("Ctrl-Up", binding);
	ASSERT_TRUE(editorKeymapFormatBinding(&keymap, EDITOR_ACTION_NEXT_TAB, binding, sizeof(binding)));
	ASSERT_EQ_STR("Ctrl-Alt-Right", binding);

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

	const char click[] = "\x1b[<0;4;2M";
	ASSERT_TRUE(editor_process_keypress_with_input(click, sizeof(click) - 1) == 0);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(5, E.cx);
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

	const char click_filler_row[] = "\x1b[<0;2;4M";
	ASSERT_TRUE(editor_process_keypress_with_input(click_filler_row,
				sizeof(click_filler_row) - 1) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(1, E.cx);
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

	E.window_cols = 48;
	const char click_first_tab[] = "\x1b[<0;1;1M";
	ASSERT_TRUE(editor_process_keypress_with_input(click_first_tab, sizeof(click_first_tab) - 1) == 0);
	ASSERT_EQ_INT(0, editorTabActiveIndex());
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("zero", E.rows[0].chars);
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
	E.cy = 0;
	E.cx = 0;

	const char wheel_down[] = "\x1b[<65;1;1M";
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_down, sizeof(wheel_down) - 1) == 0);
	ASSERT_EQ_INT(3, E.cy);

	E.cy = 8;
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_down, sizeof(wheel_down) - 1) == 0);
	ASSERT_EQ_INT(10, E.cy);

	const char wheel_up[] = "\x1b[<64;1;1M";
	E.cy = 1;
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_up, sizeof(wheel_up) - 1) == 0);
	ASSERT_EQ_INT(0, E.cy);
	return 0;
}

static int test_editor_process_keypress_mouse_click_keeps_selection_anchor(void) {
	add_row("abcdef");
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);

	const char click[] = "\x1b[<0;5;2M";
	ASSERT_TRUE(editor_process_keypress_with_input(click, sizeof(click) - 1) == 0);
	ASSERT_EQ_INT(1, E.selection_mode_active);
	ASSERT_EQ_INT(1, E.selection_anchor_cx);

	struct editorSelectionRange range;
	ASSERT_EQ_INT(1, editorGetSelectionRange(&range));
	ASSERT_EQ_INT(1, range.start_cx);
	ASSERT_EQ_INT(4, range.end_cx);
	return 0;
}

static int test_editor_process_keypress_mouse_drag_starts_selection_without_ctrl_b(void) {
	add_row("abcdef");
	E.window_rows = 3;
	E.window_cols = 20;
	E.cy = 0;
	E.cx = 0;

	const char press[] = "\x1b[<0;2;2M";
	const char drag[] = "\x1b[<32;6;2M";
	ASSERT_TRUE(editor_process_keypress_with_input(press, sizeof(press) - 1) == 0);
	ASSERT_EQ_INT(0, E.selection_mode_active);
	ASSERT_TRUE(editor_process_keypress_with_input(drag, sizeof(drag) - 1) == 0);
	ASSERT_EQ_INT(1, E.selection_mode_active);
	ASSERT_EQ_INT(1, E.selection_anchor_cx);
	ASSERT_EQ_INT(0, E.selection_anchor_cy);

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

	const char press[] = "\x1b[<0;4;2M";
	ASSERT_TRUE(editor_process_keypress_with_input(press, sizeof(press) - 1) == 0);
	ASSERT_EQ_INT(0, E.selection_mode_active);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(3, E.cx);
	ASSERT_EQ_INT(1, E.mouse_left_button_down);

	const char release[] = "\x1b[<0;4;2m";
	ASSERT_TRUE(editor_process_keypress_with_input(release, sizeof(release) - 1) == 0);
	ASSERT_EQ_INT(0, E.mouse_left_button_down);
	ASSERT_EQ_INT(0, E.mouse_drag_started);

	struct editorSelectionRange range;
	ASSERT_EQ_INT(0, editorGetSelectionRange(&range));
	return 0;
}

static int test_editor_process_keypress_mouse_drag_resets_existing_selection_anchor(void) {
	add_row("abcdef");
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	E.cx = 4;

	const char press[] = "\x1b[<0;6;2M";
	const char drag[] = "\x1b[<32;3;2M";
	ASSERT_TRUE(editor_process_keypress_with_input(press, sizeof(press) - 1) == 0);
	ASSERT_TRUE(editor_process_keypress_with_input(drag, sizeof(drag) - 1) == 0);

	ASSERT_EQ_INT(1, E.selection_mode_active);
	ASSERT_EQ_INT(5, E.selection_anchor_cx);
	ASSERT_EQ_INT(0, E.selection_anchor_cy);

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
	E.window_cols = 5;
	E.rowoff = 2;
	E.coloff = 1;
	E.cy = 0;
	E.cx = 0;

	const char press[] = "\x1b[<0;3;3M";
	const char drag[] = "\x1b[<32;50;9M";
	ASSERT_TRUE(editor_process_keypress_with_input(press, sizeof(press) - 1) == 0);
	ASSERT_TRUE(editor_process_keypress_with_input(drag, sizeof(drag) - 1) == 0);

	ASSERT_EQ_INT(1, E.selection_mode_active);
	ASSERT_EQ_INT(3, E.selection_anchor_cy);
	ASSERT_EQ_INT(3, E.selection_anchor_cx);
	ASSERT_EQ_INT(4, E.cy);
	ASSERT_EQ_INT(5, E.cx);
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

	const char press[] = "\x1b[<0;2;2M";
	const char drag[] = "\x1b[<32;4;3M";
	ASSERT_TRUE(editor_process_keypress_with_input(press, sizeof(press) - 1) == 0);
	ASSERT_TRUE(editor_process_keypress_with_input(drag, sizeof(drag) - 1) == 0);

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

	const char press[] = "\x1b[<0;2;2M";
	const char drag[] = "\x1b[<32;5;2M";
	const char release[] = "\x1b[<0;5;2m";
	const char drag_after_release[] = "\x1b[<32;6;2M";
	ASSERT_TRUE(editor_process_keypress_with_input(press, sizeof(press) - 1) == 0);
	ASSERT_TRUE(editor_process_keypress_with_input(drag, sizeof(drag) - 1) == 0);
	ASSERT_EQ_INT(4, E.cx);
	ASSERT_TRUE(editor_process_keypress_with_input(release, sizeof(release) - 1) == 0);
	ASSERT_EQ_INT(0, E.mouse_left_button_down);
	ASSERT_TRUE(editor_process_keypress_with_input(drag_after_release,
				sizeof(drag_after_release) - 1) == 0);
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
	ASSERT_EQ_INT(0, E.selection_anchor_cy);
	ASSERT_EQ_INT(2, E.selection_anchor_cx);

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

static int test_editor_extract_range_text_rejects_oversized_operation(void) {
	ASSERT_TRUE(install_synthetic_single_row(INT_MAX, 0));

	struct editorSelectionRange range = {
		.start_cy = 0,
		.start_cx = 0,
		.end_cy = 1,
		.end_cx = 0
	};
	char *text = (char *)1;
	size_t len = 123;
	int extracted = editorExtractRangeText(&range, &text, &len);
	ASSERT_EQ_INT(-1, extracted);
	ASSERT_TRUE(text == NULL);
	ASSERT_EQ_INT(0, len);
	ASSERT_EQ_STR("Operation too large", E.statusmsg);
	return 0;
}

static int test_editor_delete_range_rejects_oversized_operation(void) {
	ASSERT_TRUE(install_synthetic_single_row(INT_MAX, 0));

	struct editorSelectionRange range = {
		.start_cy = 0,
		.start_cx = 0,
		.end_cy = 1,
		.end_cx = 0
	};
	int deleted = editorDeleteRange(&range);
	ASSERT_EQ_INT(-1, deleted);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_INT(INT_MAX, E.rows[0].size);
	ASSERT_EQ_STR("Operation too large", E.statusmsg);
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
	E.selection_anchor_cy = 0;
	E.selection_anchor_cx = 7;
	E.search_match_row = 0;
	E.search_match_start = 0;
	E.search_match_len = 6;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "prefix \x1b[7malpha\x1b[m suffix") != NULL);
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
	E.selection_anchor_cy = 0;
	E.selection_anchor_cx = 1;

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
	ASSERT_EQ_INT(0, E.search_match_row);
	ASSERT_EQ_INT(3, E.search_match_start);
	ASSERT_EQ_INT(5, E.search_match_len);
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
	ASSERT_EQ_INT(2, E.search_match_row);
	ASSERT_EQ_INT(5, E.search_match_start);
	ASSERT_EQ_INT(5, E.search_match_len);
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
	ASSERT_EQ_INT(-1, E.search_match_row);
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
	ASSERT_EQ_INT(0, E.search_match_row);
	ASSERT_EQ_INT(3, E.search_match_start);
	ASSERT_EQ_INT(5, E.search_match_len);
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
	ASSERT_EQ_INT(-1, E.search_match_row);
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
	E.search_match_row = 0;
	E.search_match_start = 7;
	E.search_match_len = 5;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "prefix \x1b[7malpha\x1b[m suffix") != NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_highlight_alignment_with_escaped_controls(void) {
	const char text[] = "A\x1b" "BC";
	add_row_bytes(text, sizeof(text) - 1);
	E.window_rows = 3;
	E.window_cols = 40;
	E.cy = 0;
	E.cx = 0;
	E.search_match_row = 0;
	E.search_match_start = 2;
	E.search_match_len = 1;

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
	E.window_cols = 32;
	E.cy = 0;
	E.cx = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[7m") != NULL);
	ASSERT_TRUE(strstr(output, "* a^[[31m.txt") != NULL);
	ASSERT_TRUE(strstr(output, "beta.txt") != NULL);
	ASSERT_TRUE(strstr(output, "gamma.txt") == NULL);
	ASSERT_TRUE(strstr(output, ">") != NULL);
	free(output);
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
	E.window_cols = 5;
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

struct testCase {
	const char *name;
	int (*run)(void);
};

int main(void) {
	setlocale(LC_CTYPE, "");

	const struct testCase tests[] = {
		{"utf8_decode_valid_sequences", test_utf8_decode_valid_sequences},
		{"utf8_decode_invalid_sequences", test_utf8_decode_invalid_sequences},
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
			{"editor_rows_to_str_rejects_oversized_total",
				test_editor_rows_to_str_rejects_oversized_total},
			{"editor_open_reads_rows_and_clears_dirty", test_editor_open_reads_rows_and_clears_dirty},
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
			{"editor_insert_char_rejects_size_overflow",
				test_editor_insert_char_rejects_size_overflow},
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
			{"editor_save_rejects_oversized_buffer",
				test_editor_save_rejects_oversized_buffer},
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
			{"editor_startup_restore_choice_ignores_cli_args",
				test_editor_startup_restore_choice_ignores_cli_args},
			{"editor_startup_discard_choice_opens_cli_args",
				test_editor_startup_discard_choice_opens_cli_args},
			{"editor_startup_invalid_recovery_discards_and_opens_cli_args",
				test_editor_startup_invalid_recovery_discards_and_opens_cli_args},
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
			{"editor_keymap_load_modifier_combo_specs_case_insensitive",
				test_editor_keymap_load_modifier_combo_specs_case_insensitive},
			{"editor_keymap_load_invalid_modifier_combos_fall_back_to_defaults",
				test_editor_keymap_load_invalid_modifier_combos_fall_back_to_defaults},
			{"editor_keymap_defaults_include_tab_actions",
				test_editor_keymap_defaults_include_tab_actions},
			{"editor_process_keypress_keymap_remap_changes_dispatch",
				test_editor_process_keypress_keymap_remap_changes_dispatch},
			{"editor_process_keypress_keymap_ctrl_alt_letter_dispatches_mapped_action",
				test_editor_process_keypress_keymap_ctrl_alt_letter_dispatches_mapped_action},
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
			{"editor_process_keypress_insert_move_and_backspace",
				test_editor_process_keypress_insert_move_and_backspace},
		{"editor_process_keypress_delete_key", test_editor_process_keypress_delete_key},
		{"editor_process_keypress_arrow_down_keeps_visual_column",
			test_editor_process_keypress_arrow_down_keeps_visual_column},
		{"editor_process_keypress_ctrl_s_saves_file", test_editor_process_keypress_ctrl_s_saves_file},
		{"editor_process_keypress_resize_event_updates_window_size",
			test_editor_process_keypress_resize_event_updates_window_size},
		{"editor_process_keypress_mouse_left_click_places_cursor_with_offsets",
			test_editor_process_keypress_mouse_left_click_places_cursor_with_offsets},
			{"editor_process_keypress_mouse_left_click_ignores_non_text_rows",
				test_editor_process_keypress_mouse_left_click_ignores_non_text_rows},
			{"editor_process_keypress_mouse_top_row_click_switches_tab",
				test_editor_process_keypress_mouse_top_row_click_switches_tab},
			{"editor_process_keypress_mouse_wheel_scrolls_three_lines_and_clamps",
				test_editor_process_keypress_mouse_wheel_scrolls_three_lines_and_clamps},
		{"editor_process_keypress_mouse_click_keeps_selection_anchor",
			test_editor_process_keypress_mouse_click_keeps_selection_anchor},
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
			{"editor_extract_range_text_rejects_oversized_operation",
				test_editor_extract_range_text_rejects_oversized_operation},
			{"editor_delete_range_rejects_oversized_operation",
				test_editor_delete_range_rejects_oversized_operation},
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
			{"editor_refresh_screen_highlights_active_search_match",
				test_editor_refresh_screen_highlights_active_search_match},
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
				{"editor_refresh_screen_highlights_active_selection_spans",
					test_editor_refresh_screen_highlights_active_selection_spans},
		{"editor_refresh_screen_hides_expired_message", test_editor_refresh_screen_hides_expired_message},
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
