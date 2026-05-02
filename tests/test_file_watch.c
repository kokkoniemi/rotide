#include "test_case.h"
#include "test_helpers.h"

#include "editing/edit.h"
#include "input/dispatch.h"
#include "workspace/tabs.h"
#include "workspace/task.h"
#include "workspace/watch.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int write_text_file(const char *path, const char *text) {
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd == -1) {
		return 0;
	}
	int ok = write_all(fd, text, strlen(text)) == 0;
	if (close(fd) == -1) {
		ok = 0;
	}
	return ok;
}

static int make_temp_path(char *template) {
	int fd = mkstemp(template);
	if (fd == -1) {
		return 0;
	}
	return close(fd) == 0;
}

static int test_file_watch_reloads_clean_active_tab(void) {
	char path[] = "/tmp/rotide-watch-active-XXXXXX";
	ASSERT_TRUE(make_temp_path(path));
	ASSERT_TRUE(write_text_file(path, "alpha\n"));
	ASSERT_TRUE(editorOpen(path));

	ASSERT_TRUE(write_text_file(path, "beta gamma\n"));
	ASSERT_TRUE(editorWatchPollNow());

	ASSERT_EQ_INT(0, E.dirty);
	ASSERT_EQ_INT(0, E.disk_conflict);
	ASSERT_EQ_STR("beta gamma", E.rows[0].chars);

	unlink(path);
	return 0;
}

static int test_file_watch_reloads_clean_inactive_tab(void) {
	char first[] = "/tmp/rotide-watch-inactive-a-XXXXXX";
	char second[] = "/tmp/rotide-watch-inactive-b-XXXXXX";
	ASSERT_TRUE(make_temp_path(first));
	ASSERT_TRUE(make_temp_path(second));
	ASSERT_TRUE(write_text_file(first, "one\n"));
	ASSERT_TRUE(write_text_file(second, "two\n"));
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorTabOpenOrSwitchToFile(first));
	ASSERT_TRUE(editorTabOpenOrSwitchToFile(second));

	ASSERT_TRUE(write_text_file(first, "one changed\n"));
	ASSERT_TRUE(editorWatchPollNow());
	ASSERT_TRUE(editorTabSwitchToIndex(0));

	ASSERT_EQ_INT(0, E.dirty);
	ASSERT_EQ_STR("one changed", E.rows[0].chars);

	unlink(first);
	unlink(second);
	return 0;
}

static int test_file_watch_marks_dirty_conflict_without_reload(void) {
	char path[] = "/tmp/rotide-watch-conflict-XXXXXX";
	ASSERT_TRUE(make_temp_path(path));
	ASSERT_TRUE(write_text_file(path, "alpha\n"));
	ASSERT_TRUE(editorOpen(path));

	editorInsertChar('X');
	ASSERT_TRUE(write_text_file(path, "external changed\n"));
	ASSERT_TRUE(editorWatchPollNow());

	ASSERT_TRUE(E.dirty != 0);
	ASSERT_EQ_INT(1, E.disk_conflict);
	ASSERT_EQ_STR("Xalpha", E.rows[0].chars);

	unlink(path);
	return 0;
}

static int test_file_watch_save_prompt_can_abort_conflict_overwrite(void) {
	char path[] = "/tmp/rotide-watch-save-no-XXXXXX";
	size_t len = 0;
	char *contents = NULL;
	int saved_stdin = -1;

	ASSERT_TRUE(make_temp_path(path));
	ASSERT_TRUE(write_text_file(path, "alpha\n"));
	ASSERT_TRUE(editorOpen(path));
	editorInsertChar('X');
	ASSERT_TRUE(write_text_file(path, "external changed\n"));
	ASSERT_TRUE(editorWatchPollNow());
	ASSERT_EQ_INT(1, E.disk_conflict);

	ASSERT_TRUE(setup_stdin_bytes("n\r", 2, &saved_stdin) == 0);
	editorSave();
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);

	contents = read_file_contents(path, &len);
	ASSERT_TRUE(contents != NULL);
	ASSERT_EQ_STR("external changed\n", contents);
	free(contents);
	ASSERT_TRUE(E.dirty != 0);
	ASSERT_EQ_INT(1, E.disk_conflict);

	unlink(path);
	return 0;
}

static int test_file_watch_save_prompt_can_overwrite_conflict(void) {
	char path[] = "/tmp/rotide-watch-save-yes-XXXXXX";
	size_t len = 0;
	char *contents = NULL;
	int saved_stdin = -1;

	ASSERT_TRUE(make_temp_path(path));
	ASSERT_TRUE(write_text_file(path, "alpha\n"));
	ASSERT_TRUE(editorOpen(path));
	editorInsertChar('X');
	ASSERT_TRUE(write_text_file(path, "external changed\n"));
	ASSERT_TRUE(editorWatchPollNow());
	ASSERT_EQ_INT(1, E.disk_conflict);

	ASSERT_TRUE(setup_stdin_bytes("y\r", 2, &saved_stdin) == 0);
	editorSave();
	ASSERT_TRUE(restore_stdin(saved_stdin) == 0);

	contents = read_file_contents(path, &len);
	ASSERT_TRUE(contents != NULL);
	ASSERT_EQ_STR("Xalpha\n", contents);
	free(contents);
	ASSERT_EQ_INT(0, E.dirty);
	ASSERT_EQ_INT(0, E.disk_conflict);

	unlink(path);
	return 0;
}

static int test_file_watch_deleted_clean_file_keeps_buffer(void) {
	char path[] = "/tmp/rotide-watch-delete-XXXXXX";
	ASSERT_TRUE(make_temp_path(path));
	ASSERT_TRUE(write_text_file(path, "alpha\n"));
	ASSERT_TRUE(editorOpen(path));

	ASSERT_TRUE(unlink(path) == 0);
	ASSERT_TRUE(editorWatchPollNow());

	ASSERT_EQ_INT(0, E.dirty);
	ASSERT_EQ_INT(0, E.disk_conflict);
	ASSERT_EQ_INT(0, E.disk_state.exists);
	ASSERT_EQ_STR("alpha", E.rows[0].chars);
	ASSERT_TRUE(strstr(E.statusmsg, "File deleted on disk") != NULL);
	return 0;
}

static int test_file_watch_ignores_task_log_tabs(void) {
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorTaskShowMessage("Task: Note", "hello\n", "shown"));
	ASSERT_TRUE(editorActiveTabIsTaskLog());

	ASSERT_TRUE(!editorWatchPollNow());
	ASSERT_EQ_INT(0, E.dirty);
	ASSERT_EQ_INT(0, E.disk_conflict);
	ASSERT_TRUE(editorActiveTabIsTaskLog());
	return 0;
}

const struct editorTestCase g_file_watch_tests[] = {
	{"file_watch_reloads_clean_active_tab", test_file_watch_reloads_clean_active_tab},
	{"file_watch_reloads_clean_inactive_tab", test_file_watch_reloads_clean_inactive_tab},
	{"file_watch_marks_dirty_conflict_without_reload",
			test_file_watch_marks_dirty_conflict_without_reload},
	{"file_watch_save_prompt_can_abort_conflict_overwrite",
			test_file_watch_save_prompt_can_abort_conflict_overwrite},
	{"file_watch_save_prompt_can_overwrite_conflict",
			test_file_watch_save_prompt_can_overwrite_conflict},
	{"file_watch_deleted_clean_file_keeps_buffer",
			test_file_watch_deleted_clean_file_keeps_buffer},
	{"file_watch_ignores_task_log_tabs", test_file_watch_ignores_task_log_tabs},
};

const int g_file_watch_test_count =
		(int)(sizeof(g_file_watch_tests) / sizeof(g_file_watch_tests[0]));
