#include "config/keymap.h"
#include "editor_test_api.h"
#include "input/input_system.h"
#include "rotide.h"
#include "test_case.h"
#include "test_helpers.h"
#include "test_support.h"
#include "workspace/tabs.h"

#include <string.h>

static int vim_test_activate(void) {
	return editorInputSystemActivate("vim");
}

static int vim_test_key(int key) {
	const struct editorInputSystem *system = editorInputSystemActive();
	int effects = 0;

	ASSERT_TRUE(system != NULL);
	ASSERT_TRUE(system->handle_key != NULL);
	return system->handle_key(key, &effects);
}

static int test_input_vim_activation_starts_normal(void) {
	ASSERT_TRUE(vim_test_activate());
	ASSERT_EQ_STR("-- NORMAL --", editorVimModeLabel());
	return 0;
}

static int test_input_vim_reset_returns_to_normal(void) {
	const struct editorInputSystem *system = NULL;

	ASSERT_TRUE(vim_test_activate());
	ASSERT_TRUE(vim_test_key('i') == 0);
	ASSERT_EQ_STR("-- INSERT --", editorVimModeLabel());

	system = editorInputSystemActive();
	ASSERT_TRUE(system != NULL);
	ASSERT_TRUE(system->reset != NULL);
	system->reset();
	ASSERT_EQ_STR("-- NORMAL --", editorVimModeLabel());
	return 0;
}

static int test_input_vim_normal_text_does_not_insert(void) {
	ASSERT_TRUE(vim_test_activate());
	ASSERT_TRUE(vim_test_key('x') == 0);
	ASSERT_EQ_INT(0, E.numrows);
	ASSERT_EQ_STR("-- NORMAL --", editorVimModeLabel());
	return 0;
}

static int test_input_vim_insert_mode_inserts_until_escape(void) {
	ASSERT_TRUE(vim_test_activate());
	ASSERT_TRUE(vim_test_key('i') == 0);
	ASSERT_EQ_STR("-- INSERT --", editorVimModeLabel());

	ASSERT_TRUE(vim_test_key('x') == 0);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "x");

	ASSERT_TRUE(vim_test_key('\x1b') == 0);
	ASSERT_EQ_STR("-- NORMAL --", editorVimModeLabel());
	ASSERT_TRUE(vim_test_key('y') == 0);
	ASSERT_ROW_TEXT_EQ(0, "x");
	return 0;
}

static int test_input_vim_insert_mode_mapped_printable_does_not_insert(void) {
	ASSERT_TRUE(editorKeymapBindAction(&E.keymap, EDITOR_ACTION_REDRAW, 'x'));
	ASSERT_TRUE(vim_test_activate());
	ASSERT_TRUE(vim_test_key('i') == 0);
	ASSERT_EQ_STR("-- INSERT --", editorVimModeLabel());

	ASSERT_TRUE(vim_test_key('x') == 0);
	ASSERT_EQ_INT(0, E.numrows);
	return 0;
}

static int test_input_vim_append_entry_moves_then_inserts(void) {
	add_row("ab");
	E.cy = 0;
	E.cx = 0;

	ASSERT_TRUE(vim_test_activate());
	ASSERT_TRUE(vim_test_key('a') == 0);
	ASSERT_EQ_STR("-- INSERT --", editorVimModeLabel());
	ASSERT_TRUE(vim_test_key('x') == 0);
	ASSERT_ROW_TEXT_EQ(0, "axb");
	return 0;
}

static int test_input_vim_line_insert_entries_switch_to_insert(void) {
	add_row("ab");

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(vim_test_key('I') == 0);
	ASSERT_EQ_STR("-- INSERT --", editorVimModeLabel());
	ASSERT_EQ_INT(0, E.cx);

	ASSERT_TRUE(vim_test_key('\x1b') == 0);
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('A') == 0);
	ASSERT_EQ_STR("-- INSERT --", editorVimModeLabel());
	ASSERT_EQ_INT(2, E.cx);
	return 0;
}

static int test_input_vim_open_line_entries_switch_to_insert(void) {
	add_row("ab");

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('o') == 0);
	ASSERT_EQ_STR("-- INSERT --", editorVimModeLabel());
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(0, E.cx);

	ASSERT_TRUE(vim_test_key('\x1b') == 0);
	E.cy = 1;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('O') == 0);
	ASSERT_EQ_STR("-- INSERT --", editorVimModeLabel());
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(0, E.cx);
	return 0;
}

static int test_input_vim_visual_modes_set_selection_and_escape_clears(void) {
	add_row("abc");
	E.cy = 0;
	E.cx = 1;

	ASSERT_TRUE(vim_test_activate());
	ASSERT_TRUE(vim_test_key('v') == 0);
	ASSERT_EQ_STR("-- VISUAL --", editorVimModeLabel());
	ASSERT_EQ_INT(1, E.selection_mode_active);

	ASSERT_TRUE(vim_test_key('\x1b') == 0);
	ASSERT_EQ_STR("-- NORMAL --", editorVimModeLabel());
	ASSERT_EQ_INT(0, E.selection_mode_active);

	ASSERT_TRUE(vim_test_key('V') == 0);
	ASSERT_EQ_STR("-- VISUAL LINE --", editorVimModeLabel());
	ASSERT_EQ_INT(1, E.selection_mode_active);
	return 0;
}

static int test_input_vim_mode_is_tab_local(void) {
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(vim_test_activate());
	ASSERT_TRUE(vim_test_key('i') == 0);
	ASSERT_EQ_STR("-- INSERT --", editorVimModeLabel());

	ASSERT_TRUE(editorTabNewEmpty());
	ASSERT_EQ_STR("-- NORMAL --", editorVimModeLabel());

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_EQ_STR("-- INSERT --", editorVimModeLabel());
	return 0;
}

const struct editorTestCase g_input_vim_tests[] = {
        {"input_vim_activation_starts_normal", test_input_vim_activation_starts_normal},
        {"input_vim_reset_returns_to_normal", test_input_vim_reset_returns_to_normal},
        {"input_vim_normal_text_does_not_insert", test_input_vim_normal_text_does_not_insert},
        {"input_vim_insert_mode_inserts_until_escape",
         test_input_vim_insert_mode_inserts_until_escape},
        {"input_vim_insert_mode_mapped_printable_does_not_insert",
         test_input_vim_insert_mode_mapped_printable_does_not_insert},
        {"input_vim_append_entry_moves_then_inserts",
         test_input_vim_append_entry_moves_then_inserts},
        {"input_vim_line_insert_entries_switch_to_insert",
         test_input_vim_line_insert_entries_switch_to_insert},
        {"input_vim_open_line_entries_switch_to_insert",
         test_input_vim_open_line_entries_switch_to_insert},
        {"input_vim_visual_modes_set_selection_and_escape_clears",
         test_input_vim_visual_modes_set_selection_and_escape_clears},
        {"input_vim_mode_is_tab_local", test_input_vim_mode_is_tab_local},
};

const int g_input_vim_test_count = (int)(sizeof(g_input_vim_tests) / sizeof(g_input_vim_tests[0]));
