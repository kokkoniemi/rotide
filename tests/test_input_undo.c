#include "input/dispatch.h"
#include "test_case.h"
#include "test_helpers.h"
#include "test_support.h"

static int input_undo_dispatch(enum editorAction action) {
	int effects = 0;
	(void)editorDispatchProcessMappedAction(action, &effects);
	return 0;
}

static int test_editor_process_keypress_ctrl_z_ctrl_y_roundtrip_after_cut(void) {
	add_row("abcde");
	E.cy = 0;
	E.cx = 1;
	begin_selection();
	E.cx = 3;
	ASSERT_TRUE(input_undo_dispatch(EDITOR_ACTION_CUT_SELECTION) == 0);
	ASSERT_ROW_TEXT_EQ(0, "ade");

	ASSERT_TRUE(input_undo_dispatch(EDITOR_ACTION_UNDO) == 0);
	ASSERT_ROW_TEXT_EQ(0, "abcde");
	ASSERT_EQ_INT(0, E.selection_mode_active);

	ASSERT_TRUE(input_undo_dispatch(EDITOR_ACTION_REDO) == 0);
	ASSERT_ROW_TEXT_EQ(0, "ade");
	ASSERT_EQ_INT(0, E.selection_mode_active);
	return 0;
}

static int test_editor_process_keypress_ctrl_z_ctrl_y_roundtrip_typed_run(void) {
	ASSERT_TRUE(editor_process_single_key('i') == 0);
	ASSERT_TRUE(editor_process_single_key('a') == 0);
	ASSERT_TRUE(editor_process_single_key('b') == 0);
	ASSERT_TRUE(editor_process_single_key('c') == 0);

	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "abc");
	int dirty_after_insert = E.dirty;

	ASSERT_TRUE(input_undo_dispatch(EDITOR_ACTION_UNDO) == 0);
	ASSERT_EQ_INT(0, E.numrows);
	ASSERT_EQ_INT(0, E.dirty);

	ASSERT_TRUE(input_undo_dispatch(EDITOR_ACTION_REDO) == 0);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "abc");
	ASSERT_EQ_INT(dirty_after_insert, E.dirty);
	return 0;
}

static int test_editor_process_keypress_ctrl_z_group_break_on_navigation(void) {
	ASSERT_TRUE(editor_process_single_key('i') == 0);
	ASSERT_TRUE(editor_process_single_key('a') == 0);
	ASSERT_TRUE(editor_process_single_key('b') == 0);

	char arrow_left[] = "\x1b[D";
	ASSERT_TRUE(editor_process_keypress_with_input(arrow_left, sizeof(arrow_left) - 1) == 0);
	ASSERT_TRUE(editor_process_single_key('c') == 0);
	ASSERT_ROW_TEXT_EQ(0, "acb");

	ASSERT_TRUE(input_undo_dispatch(EDITOR_ACTION_UNDO) == 0);
	ASSERT_ROW_TEXT_EQ(0, "ab");
	ASSERT_EQ_INT(1, E.cx);

	ASSERT_TRUE(input_undo_dispatch(EDITOR_ACTION_UNDO) == 0);
	ASSERT_EQ_INT(0, E.numrows);
	return 0;
}

static int test_editor_process_keypress_ctrl_z_for_delete_and_newline_steps(void) {
	add_row("ab");
	E.cy = 0;
	E.cx = 2;
	ASSERT_TRUE(editor_process_single_key('i') == 0);

	ASSERT_TRUE(editor_process_single_key(BACKSPACE) == 0);
	ASSERT_ROW_TEXT_EQ(0, "a");
	ASSERT_TRUE(input_undo_dispatch(EDITOR_ACTION_UNDO) == 0);
	ASSERT_ROW_TEXT_EQ(0, "ab");
	ASSERT_EQ_INT(2, E.cx);

	E.cx = 1;
	ASSERT_TRUE(editor_process_single_key('\r') == 0);
	ASSERT_EQ_INT(2, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "a");
	ASSERT_ROW_TEXT_EQ(1, "b");
	ASSERT_TRUE(input_undo_dispatch(EDITOR_ACTION_UNDO) == 0);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "ab");
	ASSERT_EQ_INT(1, E.cx);
	return 0;
}

static int test_editor_process_keypress_ctrl_y_clears_after_new_edit(void) {
	ASSERT_TRUE(editor_process_single_key('i') == 0);
	ASSERT_TRUE(editor_process_single_key('a') == 0);
	ASSERT_TRUE(editor_process_single_key('b') == 0);
	ASSERT_TRUE(editor_process_single_key('c') == 0);
	ASSERT_ROW_TEXT_EQ(0, "abc");

	ASSERT_TRUE(input_undo_dispatch(EDITOR_ACTION_UNDO) == 0);
	ASSERT_EQ_INT(0, E.numrows);

	ASSERT_TRUE(editor_process_single_key('x') == 0);
	ASSERT_ROW_TEXT_EQ(0, "x");

	ASSERT_TRUE(input_undo_dispatch(EDITOR_ACTION_REDO) == 0);
	ASSERT_ROW_TEXT_EQ(0, "x");
	ASSERT_EQ_STR("Nothing to redo", E.statusmsg);
	return 0;
}

static int test_editor_process_keypress_ctrl_z_ctrl_y_empty_stack_status(void) {
	ASSERT_TRUE(input_undo_dispatch(EDITOR_ACTION_UNDO) == 0);
	ASSERT_EQ_STR("Nothing to undo", E.statusmsg);
	ASSERT_EQ_INT(0, E.numrows);

	ASSERT_TRUE(input_undo_dispatch(EDITOR_ACTION_REDO) == 0);
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
	ASSERT_TRUE(editor_process_single_key('i') == 0);

	for (int i = 0; i < ROTIDE_UNDO_HISTORY_LIMIT + 1; i++) {
		ASSERT_TRUE(editor_process_single_key(BACKSPACE) == 0);
	}
	ASSERT_ROW_TEXT_EQ(0, "");

	for (int i = 0; i < ROTIDE_UNDO_HISTORY_LIMIT; i++) {
		ASSERT_TRUE(input_undo_dispatch(EDITOR_ACTION_UNDO) == 0);
	}
	ASSERT_EQ_INT(ROTIDE_UNDO_HISTORY_LIMIT, editor_test_row_size(0));

	ASSERT_TRUE(input_undo_dispatch(EDITOR_ACTION_UNDO) == 0);
	ASSERT_EQ_INT(ROTIDE_UNDO_HISTORY_LIMIT, editor_test_row_size(0));
	ASSERT_EQ_STR("Nothing to undo", E.statusmsg);
	return 0;
}

static int test_editor_process_keypress_ctrl_z_capture_oom_preserves_state(void) {
	add_row("hello");
	E.cy = 0;
	E.cx = 5;
	ASSERT_TRUE(editor_process_single_key('i') == 0);

	ASSERT_TRUE(editor_process_single_key(BACKSPACE) == 0);
	ASSERT_ROW_TEXT_EQ(0, "hell");

	editorTestAllocFailAfter(0);
	ASSERT_TRUE(input_undo_dispatch(EDITOR_ACTION_UNDO) == 0);
	ASSERT_ROW_TEXT_EQ(0, "hell");
	ASSERT_EQ_STR("Out of memory", E.statusmsg);

	editorTestAllocReset();
	ASSERT_TRUE(input_undo_dispatch(EDITOR_ACTION_UNDO) == 0);
	ASSERT_ROW_TEXT_EQ(0, "hello");
	return 0;
}

static int test_editor_process_keypress_ctrl_z_restore_oom_preserves_state(void) {
	add_row("hello");
	E.cy = 0;
	E.cx = 5;
	ASSERT_TRUE(editor_process_single_key('i') == 0);

	ASSERT_TRUE(editor_process_single_key(BACKSPACE) == 0);
	ASSERT_ROW_TEXT_EQ(0, "hell");

	editorTestAllocFailAfter(1);
	ASSERT_TRUE(input_undo_dispatch(EDITOR_ACTION_UNDO) == 0);
	ASSERT_ROW_TEXT_EQ(0, "hell");
	ASSERT_EQ_STR("Out of memory", E.statusmsg);

	editorTestAllocReset();
	ASSERT_TRUE(input_undo_dispatch(EDITOR_ACTION_UNDO) == 0);
	ASSERT_ROW_TEXT_EQ(0, "hello");
	return 0;
}

const struct editorTestCase g_input_undo_tests[] = {
        {"editor_process_keypress_ctrl_z_ctrl_y_roundtrip_after_cut",
         test_editor_process_keypress_ctrl_z_ctrl_y_roundtrip_after_cut},
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
};

const int g_input_undo_test_count =
        (int)(sizeof(g_input_undo_tests) / sizeof(g_input_undo_tests[0]));
#include "alloc_test_hooks.h"
#include "rotide.h"

#include <string.h>
