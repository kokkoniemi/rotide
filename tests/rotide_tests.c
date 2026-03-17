#include "rotide.h"

#include "buffer.h"
#include "input.h"
#include "output.h"
#include "terminal.h"
#include "alloc_test_hooks.h"
#include "save_syscalls_test_hooks.h"
#include "test_helpers.h"
#include <dirent.h>
#include <errno.h>
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

	int buflen = 0;
	char *joined = editorRowsToStr(&buflen);
	ASSERT_TRUE(joined != NULL);
	ASSERT_EQ_INT(6, buflen);
	ASSERT_MEM_EQ("a\nbc\n\n", joined, (size_t)buflen);
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

	char esc_input[] = "\x1b";
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

	char input[] = "\r\x1b";
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

	unlink(path);
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
	char esc_input[] = "\x1b";

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
	char incomplete[] = "\x1b[";

	ASSERT_TRUE(editor_read_key_with_input(plain, sizeof(plain) - 1, &key) == 0);
	ASSERT_EQ_INT('x', key);

	ASSERT_TRUE(editor_read_key_with_input(up, sizeof(up) - 1, &key) == 0);
	ASSERT_EQ_INT(ARROW_UP, key);

	ASSERT_TRUE(editor_read_key_with_input(pgup, sizeof(pgup) - 1, &key) == 0);
	ASSERT_EQ_INT(PAGE_UP, key);

	ASSERT_TRUE(editor_read_key_with_input(end_key, sizeof(end_key) - 1, &key) == 0);
	ASSERT_EQ_INT(END_KEY, key);

	ASSERT_TRUE(editor_read_key_with_input(incomplete, sizeof(incomplete) - 1, &key) == 0);
	ASSERT_EQ_INT('\x1b', key);
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

	ASSERT_EQ_INT(7, E.window_rows);
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

	const char click[] = "\x1b[<0;4;1M";
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

	const char click_status_bar[] = "\x1b[<0;2;5M";
	ASSERT_TRUE(editor_process_keypress_with_input(click_status_bar,
				sizeof(click_status_bar) - 1) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(1, E.cx);

	const char click_filler_row[] = "\x1b[<0;2;3M";
	ASSERT_TRUE(editor_process_keypress_with_input(click_filler_row,
				sizeof(click_filler_row) - 1) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(1, E.cx);
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

	const char click[] = "\x1b[<0;5;1M";
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

	const char press[] = "\x1b[<0;2;1M";
	const char drag[] = "\x1b[<32;6;1M";
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

	const char press[] = "\x1b[<0;4;1M";
	ASSERT_TRUE(editor_process_keypress_with_input(press, sizeof(press) - 1) == 0);
	ASSERT_EQ_INT(0, E.selection_mode_active);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(3, E.cx);
	ASSERT_EQ_INT(1, E.mouse_left_button_down);

	const char release[] = "\x1b[<0;4;1m";
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

	const char press[] = "\x1b[<0;6;1M";
	const char drag[] = "\x1b[<32;3;1M";
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

	const char press[] = "\x1b[<0;3;2M";
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

	const char press[] = "\x1b[<0;2;1M";
	const char drag[] = "\x1b[<32;4;2M";
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

	const char press[] = "\x1b[<0;2;1M";
	const char drag[] = "\x1b[<32;5;1M";
	const char release[] = "\x1b[<0;5;1m";
	const char drag_after_release[] = "\x1b[<32;6;1M";
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

	const char input[] = "\x1b[<0;6;1M\x1b[<32;6;1M\x1b[<0;6;1m\x1b";
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
	ASSERT_EQ_INT(6, E.window_rows);
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

static int test_editor_process_keypress_ctrl_c_copies_single_line_selection(void) {
	add_row("hello");
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	E.cx = 5;
	int dirty_before = E.dirty;

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('c')) == 0);

	int clip_len = 0;
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

	int clip_len = 0;
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

	int clip_len = 0;
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

	int clip_len = 0;
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

	int payload_len = ROTIDE_OSC52_MAX_COPY_BYTES + 1;
	char *payload = malloc((size_t)payload_len);
	ASSERT_TRUE(payload != NULL);
	memset(payload, 'a', (size_t)payload_len);

	editorClipboardSetExternalSink(editorClipboardSyncOsc52);
	struct stdoutCapture capture;
	ASSERT_TRUE(start_stdout_capture(&capture) == 0);
	ASSERT_TRUE(editorClipboardSet(payload, payload_len));

	size_t output_len = 0;
	char *output = stop_stdout_capture(&capture, &output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_EQ_INT(0, output_len);

	int clip_len = 0;
	const char *clip = editorClipboardGet(&clip_len);
	ASSERT_EQ_INT(payload_len, clip_len);
	ASSERT_MEM_EQ(payload, clip, (size_t)payload_len);

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

	ASSERT_TRUE(editor_process_single_key('\x1b') == 0);
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

	const char input[] = {CTRL_KEY('g'), '1', '2', '\x1b'};
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

	const char input[] = {CTRL_KEY('f'), 'a', 'l', 'p', 'h', 'a', '\x1b'};
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
	ASSERT_TRUE(strstr(output, "\x1b[1;1H") != NULL);
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
		{"editor_del_char_merge_alloc_failure_preserves_state",
			test_editor_del_char_merge_alloc_failure_preserves_state},
		{"editor_insert_newline_alloc_failure_preserves_state",
			test_editor_insert_newline_alloc_failure_preserves_state},
		{"editor_save_preserves_prompt_oom_status",
			test_editor_save_preserves_prompt_oom_status},
		{"editor_save_rows_to_str_alloc_failure_preserves_state",
			test_editor_save_rows_to_str_alloc_failure_preserves_state},
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
		{"editor_read_key_sequences", test_editor_read_key_sequences},
		{"editor_read_key_sgr_mouse_events", test_editor_read_key_sgr_mouse_events},
		{"editor_read_key_returns_resize_event_when_queued",
			test_editor_read_key_returns_resize_event_when_queued},
		{"read_cursor_position_and_window_size_fallback", test_read_cursor_position_and_window_size_fallback},
		{"read_cursor_position_rejects_malformed_responses",
			test_read_cursor_position_rejects_malformed_responses},
		{"editor_refresh_window_size_clamps_tiny_terminal",
			test_editor_refresh_window_size_clamps_tiny_terminal},
		{"editor_refresh_window_size_failure_keeps_previous_dimensions",
			test_editor_refresh_window_size_failure_keeps_previous_dimensions},
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
