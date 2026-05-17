#include "test_case.h"
#include "test_support.h"

static int test_editor_syntax_undo_redo_preserves_tree(void) {
	char path[] = "/tmp/rotide-test-syntax-history-XXXXXX.c";
	ASSERT_TRUE(write_fixture_to_temp_path(path, 2,
			"tests/syntax/supported/c/history.c"));

	editorOpen(path);
	ASSERT_TRUE(editorSyntaxEnabled());
	ASSERT_TRUE(editorSyntaxTreeExists());

	E.cy = 0;
	E.cx = editor_test_row_size(0);
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
	E.cx = editor_test_row_size(1);
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

const struct editorTestCase g_syntax_state_tests[] = {
	{"editor_syntax_undo_redo_preserves_tree", test_editor_syntax_undo_redo_preserves_tree},
	{"editor_syntax_undo_redo_preserves_shell_tree", test_editor_syntax_undo_redo_preserves_shell_tree},
	{"editor_tabs_keep_independent_syntax_states", test_editor_tabs_keep_independent_syntax_states},
	{"editor_tabs_keep_shell_and_c_syntax_states", test_editor_tabs_keep_shell_and_c_syntax_states},
	{"editor_tabs_keep_web_and_c_syntax_states", test_editor_tabs_keep_web_and_c_syntax_states},
	{"editor_recovery_restore_rebuilds_c_syntax_tree", test_editor_recovery_restore_rebuilds_c_syntax_tree},
	{"editor_recovery_restore_rebuilds_shell_syntax_tree", test_editor_recovery_restore_rebuilds_shell_syntax_tree},
};

const int g_syntax_state_test_count =
		(int)(sizeof(g_syntax_state_tests) / sizeof(g_syntax_state_tests[0]));
