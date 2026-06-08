#include "config/keymap.h"
#include "editing/history.h"
#include "editing/selection.h"
#include "editor_test_api.h"
#include "input/input_system.h"
#include "rotide.h"
#include "test_case.h"
#include "test_helpers.h"
#include "test_support.h"
#include "workspace/tabs.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static int vim_test_visual_motion(int start_cy, int start_cx, int first_key, int second_key,
                                  int end_cy, int end_cx, int range_start_cy, int range_start_cx,
                                  int range_end_cy, int range_end_cx) {
	struct editorSelectionRange range = {0};

	E.cy = start_cy;
	E.cx = start_cx;
	ASSERT_TRUE(vim_test_key('v') == 0);
	ASSERT_TRUE(vim_test_key(first_key) == 0);
	if (second_key != 0) {
		ASSERT_TRUE(vim_test_key(second_key) == 0);
	}
	ASSERT_EQ_INT(end_cy, E.cy);
	ASSERT_EQ_INT(end_cx, E.cx);
	ASSERT_EQ_INT(1, editorGetSelectionRange(&range));
	ASSERT_EQ_INT(range_start_cy, range.start_cy);
	ASSERT_EQ_INT(range_start_cx, range.start_cx);
	ASSERT_EQ_INT(range_end_cy, range.end_cy);
	ASSERT_EQ_INT(range_end_cx, range.end_cx);
	ASSERT_TRUE(vim_test_key('\x1b') == 0);
	return 0;
}

static int vim_test_clipboard_eq(const char *expected) {
	size_t len = 0;
	const char *text = editorClipboardGet(&len);

	ASSERT_TRUE(text != NULL);
	ASSERT_EQ_INT((int)strlen(expected), (int)len);
	ASSERT_TRUE(memcmp(text, expected, len) == 0);
	return 0;
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

static int test_input_vim_normal_character_line_and_document_motions(void) {
	add_row("  alpha, beta");
	add_row("xy");
	add_row("  last");
	int dirty_before = E.dirty;

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 2;
	ASSERT_TRUE(vim_test_key('l') == 0);
	ASSERT_EQ_INT(3, E.cx);
	ASSERT_TRUE(vim_test_key('h') == 0);
	ASSERT_EQ_INT(2, E.cx);

	E.cx = 9;
	ASSERT_TRUE(vim_test_key('0') == 0);
	ASSERT_EQ_INT(0, E.cx);
	ASSERT_TRUE(vim_test_key('^') == 0);
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_TRUE(vim_test_key('$') == 0);
	ASSERT_EQ_INT(12, E.cx);
	ASSERT_TRUE(vim_test_key('l') == 0);
	ASSERT_EQ_INT(12, E.cx);

	E.cy = 0;
	E.cx = 4;
	ASSERT_TRUE(vim_test_key('j') == 0);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(1, E.cx);
	ASSERT_TRUE(vim_test_key('k') == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(1, E.cx);

	E.cy = 2;
	E.cx = 4;
	ASSERT_TRUE(vim_test_key('g') == 0);
	ASSERT_TRUE(vim_test_key('g') == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_TRUE(vim_test_key('G') == 0);
	ASSERT_EQ_INT(2, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_EQ_INT(dirty_before, E.dirty);
	return 0;
}

static int test_input_vim_normal_word_motions_use_vim_boundaries(void) {
	add_row("alpha, beta");
	add_row("  gamma");
	int dirty_before = E.dirty;

	ASSERT_TRUE(vim_test_activate());
	ASSERT_TRUE(vim_test_key('w') == 0);
	ASSERT_EQ_INT(5, E.cx);
	ASSERT_TRUE(vim_test_key('w') == 0);
	ASSERT_EQ_INT(7, E.cx);
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('e') == 0);
	ASSERT_EQ_INT(4, E.cx);
	E.cy = 1;
	E.cx = 6;
	ASSERT_TRUE(vim_test_key('b') == 0);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_TRUE(vim_test_key('b') == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(7, E.cx);
	ASSERT_EQ_INT(dirty_before, E.dirty);
	return 0;
}

static int test_input_vim_motion_boundaries_and_multibyte_clusters(void) {
	static const char cluster_text[] = "e\xCC\x81x";

	add_row("a");
	add_row(cluster_text);
	int dirty_before = E.dirty;

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('h') == 0);
	ASSERT_TRUE(vim_test_key('k') == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(0, E.cx);
	ASSERT_TRUE(vim_test_key('l') == 0);
	ASSERT_EQ_INT(0, E.cx);

	E.cy = 1;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('l') == 0);
	ASSERT_EQ_INT(3, E.cx);
	ASSERT_TRUE(vim_test_key('h') == 0);
	ASSERT_EQ_INT(0, E.cx);
	ASSERT_TRUE(vim_test_key('e') == 0);
	ASSERT_EQ_INT(3, E.cx);
	ASSERT_TRUE(vim_test_key('$') == 0);
	ASSERT_EQ_INT(3, E.cx);
	ASSERT_TRUE(vim_test_key('j') == 0);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(3, E.cx);
	ASSERT_EQ_INT(dirty_before, E.dirty);
	return 0;
}

static int test_input_vim_blank_line_nonblank_motion_uses_column_zero(void) {
	add_row("   ");
	ASSERT_TRUE(vim_test_activate());
	E.cx = 2;
	ASSERT_TRUE(vim_test_key('^') == 0);
	ASSERT_EQ_INT(0, E.cx);
	ASSERT_TRUE(vim_test_key('G') == 0);
	ASSERT_EQ_INT(0, E.cx);
	return 0;
}

static int test_input_vim_visual_motions_preserve_anchor(void) {
	add_row("  alpha, beta");
	add_row("xy");
	add_row("  last");
	int dirty_before = E.dirty;

	ASSERT_TRUE(vim_test_activate());
	ASSERT_EQ_INT(0, vim_test_visual_motion(0, 3, 'h', 0, 0, 2, 0, 2, 0, 3));
	ASSERT_EQ_INT(0, vim_test_visual_motion(0, 2, 'l', 0, 0, 3, 0, 2, 0, 3));
	ASSERT_EQ_INT(0, vim_test_visual_motion(0, 2, 'w', 0, 0, 7, 0, 2, 0, 7));
	ASSERT_EQ_INT(0, vim_test_visual_motion(0, 9, 'b', 0, 0, 7, 0, 7, 0, 9));
	ASSERT_EQ_INT(0, vim_test_visual_motion(0, 2, 'e', 0, 0, 6, 0, 2, 0, 6));
	ASSERT_EQ_INT(0, vim_test_visual_motion(0, 6, '0', 0, 0, 0, 0, 0, 0, 6));
	ASSERT_EQ_INT(0, vim_test_visual_motion(0, 2, '$', 0, 0, 12, 0, 2, 0, 12));
	ASSERT_EQ_INT(0, vim_test_visual_motion(0, 6, '^', 0, 0, 2, 0, 2, 0, 6));
	ASSERT_EQ_INT(0, vim_test_visual_motion(0, 3, 'j', 0, 1, 1, 0, 3, 1, 1));
	ASSERT_EQ_INT(0, vim_test_visual_motion(1, 1, 'k', 0, 0, 1, 0, 1, 1, 1));
	ASSERT_EQ_INT(0, vim_test_visual_motion(2, 4, 'g', 'g', 0, 2, 0, 2, 2, 4));
	ASSERT_EQ_INT(0, vim_test_visual_motion(0, 4, 'G', 0, 2, 2, 0, 4, 2, 2));
	ASSERT_EQ_INT(dirty_before, E.dirty);
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

static int test_input_vim_normal_delete_and_change_operators(void) {
	add_row("abcdef");

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 2;
	ASSERT_TRUE(vim_test_key('x') == 0);
	ASSERT_ROW_TEXT_EQ(0, "abdef");
	ASSERT_EQ_INT(2, E.cx);
	ASSERT_EQ_INT(0, vim_test_clipboard_eq("c"));
	ASSERT_EQ_INT(1, editorUndo());
	ASSERT_ROW_TEXT_EQ(0, "abcdef");

	E.cx = 2;
	ASSERT_TRUE(vim_test_key('D') == 0);
	ASSERT_ROW_TEXT_EQ(0, "ab");
	ASSERT_EQ_INT(0, vim_test_clipboard_eq("cdef"));
	ASSERT_EQ_INT(1, editorUndo());
	ASSERT_ROW_TEXT_EQ(0, "abcdef");

	E.cx = 1;
	ASSERT_TRUE(vim_test_key('C') == 0);
	ASSERT_ROW_TEXT_EQ(0, "a");
	ASSERT_EQ_STR("-- INSERT --", editorVimModeLabel());
	ASSERT_TRUE(vim_test_key('\x1b') == 0);
	ASSERT_EQ_INT(1, editorUndo());
	ASSERT_ROW_TEXT_EQ(0, "abcdef");
	return 0;
}

static int test_input_vim_operator_motion_delete_yank_and_change(void) {
	add_row("alpha beta gamma");

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('d') == 0);
	ASSERT_TRUE(vim_test_key('w') == 0);
	ASSERT_ROW_TEXT_EQ(0, "beta gamma");
	ASSERT_EQ_INT(0, vim_test_clipboard_eq("alpha "));
	ASSERT_EQ_INT(1, editorUndo());
	ASSERT_ROW_TEXT_EQ(0, "alpha beta gamma");

	E.cx = 6;
	ASSERT_TRUE(vim_test_key('y') == 0);
	ASSERT_TRUE(vim_test_key('$') == 0);
	ASSERT_ROW_TEXT_EQ(0, "alpha beta gamma");
	ASSERT_EQ_INT(0, vim_test_clipboard_eq("beta gamma"));

	E.cx = 6;
	ASSERT_TRUE(vim_test_key('c') == 0);
	ASSERT_TRUE(vim_test_key('e') == 0);
	ASSERT_ROW_TEXT_EQ(0, "alpha  gamma");
	ASSERT_EQ_STR("-- INSERT --", editorVimModeLabel());
	ASSERT_TRUE(vim_test_key('\x1b') == 0);
	ASSERT_EQ_INT(1, editorUndo());
	ASSERT_ROW_TEXT_EQ(0, "alpha beta gamma");
	return 0;
}

static int test_input_vim_linewise_operators_and_paste(void) {
	add_row("one");
	add_row("two");
	add_row("three");

	ASSERT_TRUE(vim_test_activate());
	E.cy = 1;
	E.cx = 0;
	int dirty_before_yank = E.dirty;
	ASSERT_TRUE(vim_test_key('y') == 0);
	ASSERT_TRUE(vim_test_key('y') == 0);
	ASSERT_EQ_INT(0, vim_test_clipboard_eq("two\n"));
	ASSERT_EQ_INT(dirty_before_yank, E.dirty);

	ASSERT_TRUE(vim_test_key('p') == 0);
	ASSERT_EQ_INT(4, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "one");
	ASSERT_ROW_TEXT_EQ(1, "two");
	ASSERT_ROW_TEXT_EQ(2, "two");
	ASSERT_ROW_TEXT_EQ(3, "three");
	ASSERT_EQ_INT(1, editorUndo());
	ASSERT_EQ_INT(3, E.numrows);
	ASSERT_ROW_TEXT_EQ(1, "two");
	ASSERT_ROW_TEXT_EQ(2, "three");

	E.cy = 1;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('d') == 0);
	ASSERT_TRUE(vim_test_key('d') == 0);
	ASSERT_EQ_INT(2, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "one");
	ASSERT_ROW_TEXT_EQ(1, "three");
	ASSERT_EQ_INT(0, vim_test_clipboard_eq("two\n"));
	ASSERT_EQ_INT(1, editorUndo());
	ASSERT_EQ_INT(3, E.numrows);
	ASSERT_ROW_TEXT_EQ(1, "two");

	E.cy = 1;
	E.cx = 1;
	ASSERT_TRUE(vim_test_key('c') == 0);
	ASSERT_TRUE(vim_test_key('c') == 0);
	ASSERT_ROW_TEXT_EQ(1, "");
	ASSERT_EQ_STR("-- INSERT --", editorVimModeLabel());
	ASSERT_TRUE(vim_test_key('\x1b') == 0);
	ASSERT_EQ_INT(1, editorUndo());
	ASSERT_ROW_TEXT_EQ(1, "two");
	return 0;
}

static int test_input_vim_charwise_paste_and_redo(void) {
	add_row("abc");

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(vim_test_key('y') == 0);
	ASSERT_TRUE(vim_test_key('$') == 0);
	ASSERT_EQ_INT(0, vim_test_clipboard_eq("bc"));

	E.cx = 0;
	ASSERT_TRUE(vim_test_key('P') == 0);
	ASSERT_ROW_TEXT_EQ(0, "bcabc");
	ASSERT_EQ_INT(1, editorUndo());
	ASSERT_ROW_TEXT_EQ(0, "abc");
	ASSERT_EQ_INT(1, editorRedo());
	ASSERT_ROW_TEXT_EQ(0, "bcabc");
	return 0;
}

static int test_input_vim_count_prefixes_motions(void) {
	add_row("alpha beta gamma delta");
	add_row("second line here");
	add_row("third row text");
	int dirty_before = E.dirty;

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('3') == 0);
	ASSERT_TRUE(vim_test_key('l') == 0);
	ASSERT_EQ_INT(3, E.cx);

	E.cx = 0;
	ASSERT_TRUE(vim_test_key('2') == 0);
	ASSERT_TRUE(vim_test_key('w') == 0);
	ASSERT_EQ_INT(11, E.cx);

	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('2') == 0);
	ASSERT_TRUE(vim_test_key('j') == 0);
	ASSERT_EQ_INT(2, E.cy);
	ASSERT_EQ_INT(0, E.cx);

	/* Count resets after the command: a bare motion moves once. */
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('l') == 0);
	ASSERT_EQ_INT(1, E.cx);
	ASSERT_EQ_INT(dirty_before, E.dirty);
	return 0;
}

static int test_input_vim_count_line_operator(void) {
	add_row("one");
	add_row("two");
	add_row("three");
	add_row("four");

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('2') == 0);
	ASSERT_TRUE(vim_test_key('d') == 0);
	ASSERT_TRUE(vim_test_key('d') == 0);
	ASSERT_EQ_INT(2, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "three");
	ASSERT_ROW_TEXT_EQ(1, "four");
	ASSERT_EQ_INT(0, vim_test_clipboard_eq("one\ntwo\n"));
	ASSERT_EQ_INT(1, editorUndo());
	ASSERT_EQ_INT(4, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "one");
	return 0;
}

static int test_input_vim_count_operator_motion_and_delete(void) {
	add_row("alpha beta gamma delta");

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('d') == 0);
	ASSERT_TRUE(vim_test_key('2') == 0);
	ASSERT_TRUE(vim_test_key('w') == 0);
	ASSERT_ROW_TEXT_EQ(0, "gamma delta");
	ASSERT_EQ_INT(0, vim_test_clipboard_eq("alpha beta "));
	ASSERT_EQ_INT(1, editorUndo());
	ASSERT_ROW_TEXT_EQ(0, "alpha beta gamma delta");

	E.cx = 0;
	ASSERT_TRUE(vim_test_key('3') == 0);
	ASSERT_TRUE(vim_test_key('x') == 0);
	ASSERT_ROW_TEXT_EQ(0, "ha beta gamma delta");
	ASSERT_EQ_INT(0, vim_test_clipboard_eq("alp"));
	return 0;
}

static int test_input_vim_named_registers(void) {
	add_row("apple");
	add_row("banana");
	add_row("cherry");

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('"') == 0);
	ASSERT_TRUE(vim_test_key('a') == 0);
	ASSERT_TRUE(vim_test_key('y') == 0);
	ASSERT_TRUE(vim_test_key('y') == 0);

	/* The default register is independent from register a. */
	E.cy = 1;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('y') == 0);
	ASSERT_TRUE(vim_test_key('y') == 0);
	ASSERT_EQ_INT(0, vim_test_clipboard_eq("banana\n"));

	E.cy = 2;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('"') == 0);
	ASSERT_TRUE(vim_test_key('a') == 0);
	ASSERT_TRUE(vim_test_key('p') == 0);
	ASSERT_EQ_INT(4, E.numrows);
	ASSERT_ROW_TEXT_EQ(3, "apple");

	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('p') == 0);
	ASSERT_ROW_TEXT_EQ(1, "banana");
	return 0;
}

static int test_input_vim_text_object_inner_and_a_word(void) {
	add_row("alpha beta gamma");

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 7;
	ASSERT_TRUE(vim_test_key('d') == 0);
	ASSERT_TRUE(vim_test_key('i') == 0);
	ASSERT_TRUE(vim_test_key('w') == 0);
	ASSERT_ROW_TEXT_EQ(0, "alpha  gamma");
	ASSERT_EQ_INT(0, vim_test_clipboard_eq("beta"));
	ASSERT_EQ_INT(1, editorUndo());
	ASSERT_ROW_TEXT_EQ(0, "alpha beta gamma");

	E.cx = 7;
	ASSERT_TRUE(vim_test_key('d') == 0);
	ASSERT_TRUE(vim_test_key('a') == 0);
	ASSERT_TRUE(vim_test_key('w') == 0);
	ASSERT_ROW_TEXT_EQ(0, "alpha gamma");
	ASSERT_EQ_INT(0, vim_test_clipboard_eq("beta "));
	ASSERT_EQ_INT(1, editorUndo());
	ASSERT_ROW_TEXT_EQ(0, "alpha beta gamma");
	return 0;
}

static int test_input_vim_text_object_paragraph(void) {
	add_row("line one");
	add_row("line two");
	add_row("");
	add_row("line four");

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('d') == 0);
	ASSERT_TRUE(vim_test_key('i') == 0);
	ASSERT_TRUE(vim_test_key('p') == 0);
	ASSERT_EQ_INT(2, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "");
	ASSERT_ROW_TEXT_EQ(1, "line four");
	return 0;
}

static int test_input_vim_visual_text_object_selects_word(void) {
	struct editorSelectionRange range = {0};

	add_row("alpha beta gamma");
	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 7;
	ASSERT_TRUE(vim_test_key('v') == 0);
	ASSERT_TRUE(vim_test_key('i') == 0);
	ASSERT_TRUE(vim_test_key('w') == 0);
	ASSERT_EQ_INT(1, editorGetSelectionRange(&range));
	ASSERT_EQ_INT(0, range.start_cy);
	ASSERT_EQ_INT(6, range.start_cx);
	ASSERT_EQ_INT(0, range.end_cy);
	ASSERT_EQ_INT(10, range.end_cx);
	ASSERT_TRUE(vim_test_key('\x1b') == 0);
	return 0;
}

static int test_input_vim_search_next_and_prev(void) {
	add_row("foo bar foo baz foo");
	int dirty_before = E.dirty;

	ASSERT_TRUE(vim_test_activate());
	E.input_vim_search_query = strdup("foo");
	ASSERT_TRUE(E.input_vim_search_query != NULL);
	E.input_vim_search_direction = 1;
	E.cy = 0;
	E.cx = 0;

	ASSERT_TRUE(vim_test_key('n') == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(8, E.cx);
	ASSERT_TRUE(vim_test_key('n') == 0);
	ASSERT_EQ_INT(16, E.cx);
	ASSERT_TRUE(vim_test_key('N') == 0);
	ASSERT_EQ_INT(8, E.cx);

	E.cx = 0;
	ASSERT_TRUE(vim_test_key('2') == 0);
	ASSERT_TRUE(vim_test_key('n') == 0);
	ASSERT_EQ_INT(16, E.cx);
	ASSERT_EQ_INT(dirty_before, E.dirty);
	return 0;
}

static int test_input_vim_search_prompt_jumps_to_match(void) {
	char search[] = {'/', 'f', 'o', 'o', '\r'};

	ASSERT_TRUE(editorTabsInit());
	add_row("foo bar");
	add_row("baz foo");
	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 0;

	ASSERT_TRUE(editor_process_keypress_with_input(search, sizeof(search)) == 0);
	ASSERT_TRUE(E.input_vim_search_query != NULL);
	ASSERT_EQ_STR("foo", E.input_vim_search_query);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(4, E.cx);
	return 0;
}

static int test_input_vim_search_prompt_escape_restores_cursor(void) {
	char search[] = {'/', 'b', 'a', 'z', '\x1b'};

	ASSERT_TRUE(editorTabsInit());
	add_row("foo bar");
	add_row("baz here");
	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 0;

	ASSERT_TRUE(editor_process_keypress_with_input(search, sizeof(search)) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(0, E.cx);
	return 0;
}

static int test_input_vim_ex_write_saves_buffer(void) {
	char path[] = "/tmp/rotide-test-vim-ex-XXXXXX";
	char cmd[] = {':', 'w', '\r'};
	int fd = mkstemp(path);
	size_t content_len = 0;
	char *contents = NULL;

	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(close(fd) == 0);

	ASSERT_TRUE(editorTabsInit());
	add_row("hello");
	add_row("world");
	E.dirty = 5;
	E.filename = strdup(path);
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_TRUE(vim_test_activate());

	ASSERT_TRUE(editor_process_keypress_with_input(cmd, sizeof(cmd)) == 0);
	ASSERT_EQ_INT(0, E.dirty);

	contents = read_file_contents(path, &content_len);
	ASSERT_TRUE(contents != NULL);
	ASSERT_MEM_EQ("hello\nworld\n", contents, content_len);
	free(contents);
	unlink(path);
	return 0;
}

static int test_input_vim_ex_goto_line(void) {
	char cmd[] = {':', '3', '\r'};

	ASSERT_TRUE(editorTabsInit());
	add_row("one");
	add_row("two");
	add_row("  three");
	add_row("four");
	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 0;

	ASSERT_TRUE(editor_process_keypress_with_input(cmd, sizeof(cmd)) == 0);
	ASSERT_EQ_INT(2, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	return 0;
}

static int test_input_vim_ex_substitute_global(void) {
	char cmd[] = {':', '%', 's', '/', 'o', '/', '0', '/', 'g', '\r'};

	ASSERT_TRUE(editorTabsInit());
	add_row("foo boo");
	add_row("zoo");
	ASSERT_TRUE(vim_test_activate());

	ASSERT_TRUE(editor_process_keypress_with_input(cmd, sizeof(cmd)) == 0);
	ASSERT_ROW_TEXT_EQ(0, "f00 b00");
	ASSERT_ROW_TEXT_EQ(1, "z00");
	return 0;
}

static int test_input_vim_ex_substitute_first_per_line(void) {
	char cmd[] = {':', '%', 's', '/', 'o', '/', '0', '/', '\r'};

	ASSERT_TRUE(editorTabsInit());
	add_row("foo boo");
	add_row("zoo");
	ASSERT_TRUE(vim_test_activate());

	ASSERT_TRUE(editor_process_keypress_with_input(cmd, sizeof(cmd)) == 0);
	ASSERT_ROW_TEXT_EQ(0, "f0o boo");
	ASSERT_ROW_TEXT_EQ(1, "z0o");
	return 0;
}

static int test_input_vim_ex_invalid_command_is_messaged(void) {
	char cmd[] = {':', 'f', 'o', 'o', 'b', 'a', 'r', '\r'};
	int dirty_before = 0;

	ASSERT_TRUE(editorTabsInit());
	add_row("unchanged");
	dirty_before = E.dirty;
	ASSERT_TRUE(vim_test_activate());

	ASSERT_TRUE(editor_process_keypress_with_input(cmd, sizeof(cmd)) == 0);
	ASSERT_ROW_TEXT_EQ(0, "unchanged");
	ASSERT_EQ_INT(dirty_before, E.dirty);
	ASSERT_TRUE(strstr(E.statusmsg, "Not an editor command") != NULL);
	return 0;
}

static int test_input_vim_ex_quit_dirty_is_refused(void) {
	char cmd[] = {':', 'q', '\r'};

	ASSERT_TRUE(editorTabsInit());
	add_row("data");
	E.dirty = 1;
	ASSERT_TRUE(vim_test_activate());

	ASSERT_TRUE(editor_process_keypress_with_input(cmd, sizeof(cmd)) == 0);
	ASSERT_TRUE(strstr(E.statusmsg, "No write since last change") != NULL);
	ASSERT_EQ_INT(1, E.dirty);
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
        {"input_vim_normal_character_line_and_document_motions",
         test_input_vim_normal_character_line_and_document_motions},
        {"input_vim_normal_word_motions_use_vim_boundaries",
         test_input_vim_normal_word_motions_use_vim_boundaries},
        {"input_vim_motion_boundaries_and_multibyte_clusters",
         test_input_vim_motion_boundaries_and_multibyte_clusters},
        {"input_vim_blank_line_nonblank_motion_uses_column_zero",
         test_input_vim_blank_line_nonblank_motion_uses_column_zero},
        {"input_vim_visual_motions_preserve_anchor", test_input_vim_visual_motions_preserve_anchor},
        {"input_vim_mode_is_tab_local", test_input_vim_mode_is_tab_local},
        {"input_vim_normal_delete_and_change_operators",
         test_input_vim_normal_delete_and_change_operators},
        {"input_vim_operator_motion_delete_yank_and_change",
         test_input_vim_operator_motion_delete_yank_and_change},
        {"input_vim_linewise_operators_and_paste", test_input_vim_linewise_operators_and_paste},
        {"input_vim_charwise_paste_and_redo", test_input_vim_charwise_paste_and_redo},
        {"input_vim_count_prefixes_motions", test_input_vim_count_prefixes_motions},
        {"input_vim_count_line_operator", test_input_vim_count_line_operator},
        {"input_vim_count_operator_motion_and_delete",
         test_input_vim_count_operator_motion_and_delete},
        {"input_vim_named_registers", test_input_vim_named_registers},
        {"input_vim_text_object_inner_and_a_word", test_input_vim_text_object_inner_and_a_word},
        {"input_vim_text_object_paragraph", test_input_vim_text_object_paragraph},
        {"input_vim_visual_text_object_selects_word",
         test_input_vim_visual_text_object_selects_word},
        {"input_vim_search_next_and_prev", test_input_vim_search_next_and_prev},
        {"input_vim_search_prompt_jumps_to_match", test_input_vim_search_prompt_jumps_to_match},
        {"input_vim_search_prompt_escape_restores_cursor",
         test_input_vim_search_prompt_escape_restores_cursor},
        {"input_vim_ex_write_saves_buffer", test_input_vim_ex_write_saves_buffer},
        {"input_vim_ex_goto_line", test_input_vim_ex_goto_line},
        {"input_vim_ex_substitute_global", test_input_vim_ex_substitute_global},
        {"input_vim_ex_substitute_first_per_line", test_input_vim_ex_substitute_first_per_line},
        {"input_vim_ex_invalid_command_is_messaged", test_input_vim_ex_invalid_command_is_messaged},
        {"input_vim_ex_quit_dirty_is_refused", test_input_vim_ex_quit_dirty_is_refused},
};

const int g_input_vim_test_count = (int)(sizeof(g_input_vim_tests) / sizeof(g_input_vim_tests[0]));
