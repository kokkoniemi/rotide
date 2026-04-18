#include "test_case.h"
#include "test_support.h"

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

const struct editorTestCase g_save_recovery_tests[] = {
	{"editor_save_writes_file_and_clears_dirty", test_editor_save_writes_file_and_clears_dirty},
	{"editor_save_prompts_for_filename", test_editor_save_prompts_for_filename},
	{"editor_save_aborts_when_prompt_cancelled", test_editor_save_aborts_when_prompt_cancelled},
	{"editor_save_rejects_empty_filename_prompt", test_editor_save_rejects_empty_filename_prompt},
	{"editor_save_truncates_existing_file_with_empty_buffer", test_editor_save_truncates_existing_file_with_empty_buffer},
	{"editor_save_preserves_existing_file_mode", test_editor_save_preserves_existing_file_mode},
	{"editor_save_removes_temp_file_on_success", test_editor_save_removes_temp_file_on_success},
	{"editor_save_failure_cleans_temp_file", test_editor_save_failure_cleans_temp_file},
	{"editor_save_temp_fsync_failure_cleans_temp_file", test_editor_save_temp_fsync_failure_cleans_temp_file},
	{"editor_save_temp_close_failure_cleans_temp_file", test_editor_save_temp_close_failure_cleans_temp_file},
	{"editor_save_rename_failure_cleans_temp_file_deterministic", test_editor_save_rename_failure_cleans_temp_file_deterministic},
	{"editor_save_rename_failure_reports_permission_denied_class", test_editor_save_rename_failure_reports_permission_denied_class},
	{"editor_save_rename_failure_reports_missing_path_class", test_editor_save_rename_failure_reports_missing_path_class},
	{"editor_save_rename_failure_reports_read_only_fs_class", test_editor_save_rename_failure_reports_read_only_fs_class},
	{"editor_save_reports_temp_cleanup_failure", test_editor_save_reports_temp_cleanup_failure},
	{"editor_prompt_accept_and_cancel", test_editor_prompt_accept_and_cancel},
	{"editor_prompt_accepts_utf8_input", test_editor_prompt_accepts_utf8_input},
	{"editor_prompt_filters_ascii_controls_but_keeps_non_ascii_bytes", test_editor_prompt_filters_ascii_controls_but_keeps_non_ascii_bytes},
	{"editor_prompt_backspace_removes_entire_utf8_codepoint", test_editor_prompt_backspace_removes_entire_utf8_codepoint},
	{"editor_prompt_delete_key_removes_entire_utf8_codepoint", test_editor_prompt_delete_key_removes_entire_utf8_codepoint},
	{"editor_prompt_fails_on_initial_alloc", test_editor_prompt_fails_on_initial_alloc},
	{"editor_prompt_fails_on_growth_alloc", test_editor_prompt_fails_on_growth_alloc},
	{"editor_insert_row_render_alloc_failure_preserves_state", test_editor_insert_row_render_alloc_failure_preserves_state},
	{"editor_insert_char_render_alloc_failure_preserves_state", test_editor_insert_char_render_alloc_failure_preserves_state},
	{"editor_insert_char_uses_document_when_row_cache_corrupt", test_editor_insert_char_uses_document_when_row_cache_corrupt},
	{"editor_insert_row_rejects_size_overflow", test_editor_insert_row_rejects_size_overflow},
	{"editor_del_char_merge_alloc_failure_preserves_state", test_editor_del_char_merge_alloc_failure_preserves_state},
	{"editor_insert_newline_alloc_failure_preserves_state", test_editor_insert_newline_alloc_failure_preserves_state},
	{"editor_save_preserves_prompt_oom_status", test_editor_save_preserves_prompt_oom_status},
	{"editor_save_rows_to_str_alloc_failure_preserves_state", test_editor_save_rows_to_str_alloc_failure_preserves_state},
	{"editor_save_uses_document_when_row_cache_corrupt", test_editor_save_uses_document_when_row_cache_corrupt},
	{"editor_save_tmp_path_alloc_failure_preserves_state", test_editor_save_tmp_path_alloc_failure_preserves_state},
	{"editor_save_parent_dir_fsync_failure_after_rename_reports_failure", test_editor_save_parent_dir_fsync_failure_after_rename_reports_failure},
	{"editor_save_parent_dir_open_failure_reports_failure", test_editor_save_parent_dir_open_failure_reports_failure},
	{"editor_save_parent_dir_close_failure_reports_failure", test_editor_save_parent_dir_close_failure_reports_failure},
	{"editor_save_parent_dir_fsync_success_clears_dirty", test_editor_save_parent_dir_fsync_success_clears_dirty},
	{"editor_recovery_autosave_activity_debounce", test_editor_recovery_autosave_activity_debounce},
	{"editor_recovery_cleans_snapshot_when_all_tabs_clean", test_editor_recovery_cleans_snapshot_when_all_tabs_clean},
	{"editor_recovery_roundtrip_restores_tabs_and_cursor_state", test_editor_recovery_roundtrip_restores_tabs_and_cursor_state},
	{"editor_recovery_rejects_unsupported_snapshot_version", test_editor_recovery_rejects_unsupported_snapshot_version},
	{"editor_startup_restore_choice_ignores_cli_args", test_editor_startup_restore_choice_ignores_cli_args},
	{"editor_startup_discard_choice_opens_cli_args", test_editor_startup_discard_choice_opens_cli_args},
	{"editor_startup_invalid_recovery_discards_and_opens_cli_args", test_editor_startup_invalid_recovery_discards_and_opens_cli_args},
};

const int g_save_recovery_test_count =
		(int)(sizeof(g_save_recovery_tests) / sizeof(g_save_recovery_tests[0]));
