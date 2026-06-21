#include "config/keymap.h"
#include "editing/history.h"
#include "editing/selection.h"
#include "editor_test_api.h"
#include "input/input_system.h"
#include "render/popup.h"
#include "rotide.h"
#include "test_case.h"
#include "test_helpers.h"
#include "test_support.h"
#include "workspace/layout.h"
#include "workspace/tabs.h"

#include <stdio.h>
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

static int vim_test_ex_command(const char *cmd) {
	size_t len = strlen(cmd);
	char *input = malloc(len + 3);
	int result;

	ASSERT_TRUE(input != NULL);
	input[0] = ':';
	memcpy(input + 1, cmd, len);
	input[len + 1] = '\r';
	input[len + 2] = '\0';
	result = editor_process_keypress_with_input(input, len + 2);
	free(input);
	ASSERT_TRUE(result == 0);
	return 0;
}

static int test_input_vim_activation_starts_normal(void) {
	ASSERT_TRUE(vim_test_activate());
	ASSERT_EQ_STR("NORMAL", editorVimModeLabel());
	return 0;
}

static int test_input_vim_reset_returns_to_normal(void) {
	const struct editorInputSystem *system = NULL;

	ASSERT_TRUE(vim_test_activate());
	ASSERT_TRUE(vim_test_key('i') == 0);
	ASSERT_EQ_STR("INSERT", editorVimModeLabel());

	system = editorInputSystemActive();
	ASSERT_TRUE(system != NULL);
	ASSERT_TRUE(system->reset != NULL);
	system->reset();
	ASSERT_EQ_STR("NORMAL", editorVimModeLabel());
	return 0;
}

static int test_input_vim_cursor_style_is_block_outside_insert(void) {
	const struct editorInputSystem *system = NULL;

	add_row("alpha");
	ASSERT_TRUE(vim_test_activate());
	system = editorInputSystemActive();
	ASSERT_TRUE(system != NULL);
	ASSERT_TRUE(system->cursor_style != NULL);

	ASSERT_EQ_STR("NORMAL", editorVimModeLabel());
	ASSERT_EQ_INT(EDITOR_CURSOR_STYLE_BLOCK, system->cursor_style());

	ASSERT_TRUE(vim_test_key('v') == 0);
	ASSERT_EQ_STR("VISUAL", editorVimModeLabel());
	ASSERT_EQ_INT(EDITOR_CURSOR_STYLE_BLOCK, system->cursor_style());

	ASSERT_TRUE(vim_test_key('\x1b') == 0);
	ASSERT_TRUE(vim_test_key('i') == 0);
	ASSERT_EQ_STR("INSERT", editorVimModeLabel());
	ASSERT_TRUE(system->cursor_style() < 0);

	ASSERT_TRUE(vim_test_key('\x1b') == 0);
	ASSERT_EQ_STR("NORMAL", editorVimModeLabel());
	ASSERT_EQ_INT(EDITOR_CURSOR_STYLE_BLOCK, system->cursor_style());
	return 0;
}

static int test_input_vim_normal_text_does_not_insert(void) {
	ASSERT_TRUE(vim_test_activate());
	ASSERT_TRUE(vim_test_key('x') == 0);
	ASSERT_EQ_INT(0, E.numrows);
	ASSERT_EQ_STR("NORMAL", editorVimModeLabel());
	return 0;
}

static int test_input_vim_insert_mode_inserts_until_escape(void) {
	ASSERT_TRUE(vim_test_activate());
	ASSERT_TRUE(vim_test_key('i') == 0);
	ASSERT_EQ_STR("INSERT", editorVimModeLabel());

	ASSERT_TRUE(vim_test_key('x') == 0);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "x");

	ASSERT_TRUE(vim_test_key('\x1b') == 0);
	ASSERT_EQ_STR("NORMAL", editorVimModeLabel());
	ASSERT_TRUE(vim_test_key('y') == 0);
	ASSERT_ROW_TEXT_EQ(0, "x");
	return 0;
}

static int test_input_vim_insert_mode_mapped_printable_does_not_insert(void) {
	ASSERT_TRUE(editorKeymapBindAction(&E.keymap, EDITOR_ACTION_REDRAW, 'x'));
	ASSERT_TRUE(vim_test_activate());
	ASSERT_TRUE(vim_test_key('i') == 0);
	ASSERT_EQ_STR("INSERT", editorVimModeLabel());

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
	ASSERT_EQ_STR("INSERT", editorVimModeLabel());
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
	ASSERT_EQ_STR("INSERT", editorVimModeLabel());
	ASSERT_EQ_INT(0, E.cx);

	ASSERT_TRUE(vim_test_key('\x1b') == 0);
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('A') == 0);
	ASSERT_EQ_STR("INSERT", editorVimModeLabel());
	ASSERT_EQ_INT(2, E.cx);
	return 0;
}

static int test_input_vim_open_line_entries_switch_to_insert(void) {
	add_row("ab");

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('o') == 0);
	ASSERT_EQ_STR("INSERT", editorVimModeLabel());
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(0, E.cx);

	ASSERT_TRUE(vim_test_key('\x1b') == 0);
	E.cy = 1;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('O') == 0);
	ASSERT_EQ_STR("INSERT", editorVimModeLabel());
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
	ASSERT_EQ_STR("VISUAL", editorVimModeLabel());
	ASSERT_EQ_INT(1, E.selection_mode_active);

	ASSERT_TRUE(vim_test_key('\x1b') == 0);
	ASSERT_EQ_STR("NORMAL", editorVimModeLabel());
	ASSERT_EQ_INT(0, E.selection_mode_active);

	ASSERT_TRUE(vim_test_key('V') == 0);
	ASSERT_EQ_STR("VISUAL LINE", editorVimModeLabel());
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
	ASSERT_EQ_INT(0, vim_test_visual_motion(0, 3, 'h', 0, 0, 2, 0, 2, 0, 4));
	ASSERT_EQ_INT(0, vim_test_visual_motion(0, 2, 'l', 0, 0, 3, 0, 2, 0, 4));
	ASSERT_EQ_INT(0, vim_test_visual_motion(0, 2, 'w', 0, 0, 7, 0, 2, 0, 8));
	ASSERT_EQ_INT(0, vim_test_visual_motion(0, 9, 'b', 0, 0, 7, 0, 7, 0, 10));
	ASSERT_EQ_INT(0, vim_test_visual_motion(0, 2, 'e', 0, 0, 6, 0, 2, 0, 7));
	ASSERT_EQ_INT(0, vim_test_visual_motion(0, 6, '0', 0, 0, 0, 0, 0, 0, 7));
	ASSERT_EQ_INT(0, vim_test_visual_motion(0, 2, '$', 0, 0, 12, 0, 2, 0, 13));
	ASSERT_EQ_INT(0, vim_test_visual_motion(0, 6, '^', 0, 0, 2, 0, 2, 0, 7));
	ASSERT_EQ_INT(0, vim_test_visual_motion(0, 3, 'j', 0, 1, 1, 0, 3, 1, 2));
	ASSERT_EQ_INT(0, vim_test_visual_motion(1, 1, 'k', 0, 0, 1, 0, 1, 1, 2));
	ASSERT_EQ_INT(0, vim_test_visual_motion(2, 4, 'g', 'g', 0, 2, 0, 2, 2, 5));
	ASSERT_EQ_INT(0, vim_test_visual_motion(0, 4, 'G', 0, 2, 2, 0, 4, 2, 3));
	ASSERT_EQ_INT(dirty_before, E.dirty);
	return 0;
}

static int test_input_vim_visual_arrow_keys_grow_selection(void) {
	add_row("  alpha, beta");
	add_row("xy");
	add_row("  last");
	int dirty_before = E.dirty;

	ASSERT_TRUE(vim_test_activate());
	ASSERT_EQ_INT(0, vim_test_visual_motion(0, 3, ARROW_LEFT, 0, 0, 2, 0, 2, 0, 4));
	ASSERT_EQ_INT(0, vim_test_visual_motion(0, 2, ARROW_RIGHT, 0, 0, 3, 0, 2, 0, 4));
	ASSERT_EQ_INT(0, vim_test_visual_motion(0, 3, ARROW_DOWN, 0, 1, 1, 0, 3, 1, 2));
	ASSERT_EQ_INT(0, vim_test_visual_motion(1, 1, ARROW_UP, 0, 0, 1, 0, 1, 1, 2));
	ASSERT_EQ_INT(dirty_before, E.dirty);
	return 0;
}

static int test_input_vim_normal_arrow_keys_move_as_motions(void) {
	add_row("alpha");
	add_row("beta");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key(ARROW_RIGHT) == 0);
	ASSERT_EQ_INT(1, E.cx);
	ASSERT_TRUE(vim_test_key(ARROW_DOWN) == 0);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_TRUE(vim_test_key(ARROW_UP) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_TRUE(vim_test_key(ARROW_LEFT) == 0);
	ASSERT_EQ_INT(0, E.cx);
	ASSERT_TRUE(vim_test_key(ARROW_LEFT) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(0, E.cx);
	return 0;
}

static int test_input_vim_operator_arrow_down_deletes_lines(void) {
	add_row("one");
	add_row("two");
	add_row("three");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('d') == 0);
	ASSERT_TRUE(vim_test_key(ARROW_DOWN) == 0);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "three");
	return 0;
}

static int test_input_vim_mode_is_tab_local(void) {
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(vim_test_activate());
	ASSERT_TRUE(vim_test_key('i') == 0);
	ASSERT_EQ_STR("INSERT", editorVimModeLabel());

	ASSERT_TRUE(editorTabNewEmpty());
	ASSERT_EQ_STR("NORMAL", editorVimModeLabel());

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_EQ_STR("INSERT", editorVimModeLabel());
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
	ASSERT_EQ_STR("INSERT", editorVimModeLabel());
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
	ASSERT_EQ_STR("INSERT", editorVimModeLabel());
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
	ASSERT_EQ_STR("INSERT", editorVimModeLabel());
	ASSERT_TRUE(vim_test_key('\x1b') == 0);
	ASSERT_EQ_INT(1, editorUndo());
	ASSERT_ROW_TEXT_EQ(1, "two");
	return 0;
}

static int test_input_vim_read_only_tab_rejects_vim_mutations(void) {
	char path[] = "/tmp/rotide-test-vim-read-only-XXXXXX";
	const char bytes[] = {'r', 'o', 't', 'i', 'd', 'e', '\0', 'b', 'i', 'n'};
	int fd = mkstemp(path);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(write_all(fd, bytes, sizeof(bytes)) == 0);
	ASSERT_TRUE(close(fd) == 0);

	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorTabOpenOrSwitchToPreviewFile(path));
	ASSERT_TRUE(editorActiveTabIsUnsupportedFile());
	ASSERT_TRUE(editorActiveTabIsReadOnly());
	ASSERT_ROW_TEXT_EQ(0, "File is unsupported");
	int dirty_before = E.dirty;

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('x') == 0);
	ASSERT_ROW_TEXT_EQ(0, "File is unsupported");
	ASSERT_EQ_INT(dirty_before, E.dirty);
	ASSERT_EQ_STR("File is unsupported", E.statusmsg);

	ASSERT_TRUE(vim_test_key('d') == 0);
	ASSERT_TRUE(vim_test_key('d') == 0);
	ASSERT_ROW_TEXT_EQ(0, "File is unsupported");
	ASSERT_EQ_INT(dirty_before, E.dirty);

	ASSERT_TRUE(editorClipboardSet("paste", 5));
	ASSERT_TRUE(vim_test_key('p') == 0);
	ASSERT_ROW_TEXT_EQ(0, "File is unsupported");
	ASSERT_EQ_INT(dirty_before, E.dirty);

	char substitute[] = {':', '%', 's', '/', 'F', '/', 'X', '/', '\r'};
	ASSERT_TRUE(editor_process_keypress_with_input(substitute, sizeof(substitute)) == 0);
	ASSERT_ROW_TEXT_EQ(0, "File is unsupported");
	ASSERT_EQ_INT(dirty_before, E.dirty);
	ASSERT_EQ_STR("File is unsupported", E.statusmsg);

	ASSERT_TRUE(unlink(path) == 0);
	return 0;
}

static int test_input_vim_visual_charwise_operations_include_cursor(void) {
	add_row("abcd");

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(vim_test_key('v') == 0);
	ASSERT_TRUE(vim_test_key('l') == 0);
	ASSERT_TRUE(vim_test_key('d') == 0);
	ASSERT_ROW_TEXT_EQ(0, "ad");
	ASSERT_EQ_INT(0, vim_test_clipboard_eq("bc"));
	ASSERT_EQ_INT(1, editorUndo());
	ASSERT_ROW_TEXT_EQ(0, "abcd");

	E.cy = 0;
	E.cx = 2;
	ASSERT_TRUE(vim_test_key('v') == 0);
	ASSERT_TRUE(vim_test_key('h') == 0);
	ASSERT_TRUE(vim_test_key('y') == 0);
	ASSERT_ROW_TEXT_EQ(0, "abcd");
	ASSERT_EQ_INT(0, vim_test_clipboard_eq("bc"));

	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(vim_test_key('v') == 0);
	ASSERT_TRUE(vim_test_key('d') == 0);
	ASSERT_ROW_TEXT_EQ(0, "acd");
	ASSERT_EQ_INT(0, vim_test_clipboard_eq("b"));
	return 0;
}

static int test_input_vim_visual_single_cluster_delete_handles_multibyte(void) {
	static const char cluster_text[] = "e\xCC\x81x";
	static const char cluster_only[] = "e\xCC\x81";

	add_row(cluster_text);

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('v') == 0);
	ASSERT_TRUE(vim_test_key('d') == 0);
	ASSERT_ROW_TEXT_EQ(0, "x");
	ASSERT_EQ_INT(0, vim_test_clipboard_eq(cluster_only));
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

static int test_input_vim_default_register_linewise_persists_across_tabs(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("one");
	add_row("two");

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('y') == 0);
	ASSERT_TRUE(vim_test_key('y') == 0);
	ASSERT_EQ_INT(0, vim_test_clipboard_eq("one\n"));

	ASSERT_TRUE(editorTabNewEmpty());
	add_row("target");
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('p') == 0);
	ASSERT_EQ_INT(2, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "target");
	ASSERT_ROW_TEXT_EQ(1, "one");
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

static int test_input_vim_ex_window_aliases(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("hello");
	ASSERT_TRUE(vim_test_activate());

	ASSERT_TRUE(vim_test_ex_command("split") == 0);
	ASSERT_EQ_INT(2, editorPaneTreeLeafCount(E.layout_root));
	ASSERT_TRUE(E.layout_root->is_split);
	ASSERT_EQ_INT(EDITOR_SPLIT_HORIZONTAL, E.layout_root->as.split.orientation);

	ASSERT_TRUE(vim_test_ex_command("close") == 0);
	ASSERT_EQ_INT(1, editorPaneTreeLeafCount(E.layout_root));

	ASSERT_TRUE(vim_test_ex_command("vsplit") == 0);
	ASSERT_EQ_INT(2, editorPaneTreeLeafCount(E.layout_root));
	ASSERT_TRUE(E.layout_root->is_split);
	ASSERT_EQ_INT(EDITOR_SPLIT_VERTICAL, E.layout_root->as.split.orientation);

	ASSERT_TRUE(vim_test_ex_command("only") == 0);
	ASSERT_EQ_INT(1, editorPaneTreeLeafCount(E.layout_root));
	return 0;
}

static int test_input_vim_ex_file_argument_commands(void) {
	char edit_path[256];
	char split_path[256];
	char vsplit_path[256];

	ASSERT_TRUE(write_temp_text_file(edit_path, sizeof(edit_path), "edit\n"));
	ASSERT_TRUE(write_temp_text_file(split_path, sizeof(split_path), "split\n"));
	ASSERT_TRUE(write_temp_text_file(vsplit_path, sizeof(vsplit_path), "vsplit\n"));

	ASSERT_TRUE(editorTabsInit());
	add_row("start");
	ASSERT_TRUE(vim_test_activate());

	char edit_cmd[320];
	ASSERT_TRUE(snprintf(edit_cmd, sizeof(edit_cmd), "e %s", edit_path) > 0);
	ASSERT_TRUE(vim_test_ex_command(edit_cmd) == 0);
	ASSERT_EQ_INT(1, editorPaneTreeLeafCount(E.layout_root));
	ASSERT_EQ_STR(edit_path, editorTabFilenameAt(editorTabActiveIndex()));

	char split_cmd[320];
	ASSERT_TRUE(snprintf(split_cmd, sizeof(split_cmd), "sp %s", split_path) > 0);
	ASSERT_TRUE(vim_test_ex_command(split_cmd) == 0);
	ASSERT_EQ_INT(2, editorPaneTreeLeafCount(E.layout_root));
	ASSERT_EQ_STR(split_path, editorTabFilenameAt(editorTabActiveIndex()));

	char vsplit_cmd[320];
	ASSERT_TRUE(snprintf(vsplit_cmd, sizeof(vsplit_cmd), "vs %s", vsplit_path) > 0);
	ASSERT_TRUE(vim_test_ex_command(vsplit_cmd) == 0);
	ASSERT_EQ_INT(3, editorPaneTreeLeafCount(E.layout_root));
	ASSERT_EQ_STR(vsplit_path, editorTabFilenameAt(editorTabActiveIndex()));

	unlink(edit_path);
	unlink(split_path);
	unlink(vsplit_path);
	return 0;
}

static int test_input_vim_ex_tab_and_terminal_aliases(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("hello");
	ASSERT_TRUE(editorTabNewEmpty());
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_TRUE(vim_test_activate());

	ASSERT_TRUE(vim_test_ex_command("bd") == 0);
	ASSERT_EQ_INT(1, editorTabCount());

	ASSERT_TRUE(vim_test_ex_command("tabnew") == 0);
	ASSERT_EQ_INT(2, editorTabCount());

	ASSERT_TRUE(vim_test_ex_command("tabc") == 0);
	ASSERT_EQ_INT(1, editorTabCount());

	ASSERT_TRUE(vim_test_ex_command("term") == 0);
	ASSERT_EQ_INT(2, editorPaneTreeLeafCount(E.layout_root));
	ASSERT_EQ_INT(EDITOR_PANE_KIND_TERMINAL, editorPaneActiveKind(E.focused_leaf));

	ASSERT_TRUE(vim_test_ex_command("close") == 0);
	ASSERT_TRUE(vim_test_ex_command("vterm") == 0);
	ASSERT_EQ_INT(2, editorPaneTreeLeafCount(E.layout_root));
	ASSERT_EQ_INT(EDITOR_PANE_KIND_TERMINAL, editorPaneActiveKind(E.focused_leaf));
	return 0;
}

static int test_input_vim_ex_completion_cycles_commands(void) {
	char *first = vimSystemExCompletionTest("sp", 0);
	char *second = vimSystemExCompletionTest("sp", 1);
	char *third = vimSystemExCompletionTest("sp", 2);
	char *tab0 = vimSystemExCompletionTest("tab", 0);
	char *tab1 = vimSystemExCompletionTest("tab", 1);
	char *tab2 = vimSystemExCompletionTest("tab", 2);
	char *wrap = vimSystemExCompletionTest("tab", 3);
	char *builtin = vimSystemExCompletionTest("x", 0);
	char *missing = vimSystemExCompletionTest("zz", 0);

	ASSERT_EQ_STR("split", first);
	ASSERT_EQ_STR("sp", second);
	ASSERT_EQ_STR("split", third);
	ASSERT_EQ_STR("tabclose", tab0);
	ASSERT_EQ_STR("tabc", tab1);
	ASSERT_EQ_STR("tabnew", tab2);
	ASSERT_EQ_STR("tabclose", wrap);
	ASSERT_EQ_STR("x", builtin);
	ASSERT_TRUE(missing == NULL);
	free(first);
	free(second);
	free(third);
	free(tab0);
	free(tab1);
	free(tab2);
	free(wrap);
	free(builtin);
	return 0;
}

static int test_input_vim_ctrl_w_split_focus_and_close(void) {
	struct editorPaneNode *first = NULL;
	struct editorPaneNode *second = NULL;

	ASSERT_TRUE(editorTabsInit());
	add_row("hello");
	ASSERT_TRUE(vim_test_activate());
	first = E.focused_leaf;

	ASSERT_TRUE(vim_test_key(CTRL_KEY('w')) == 0);
	ASSERT_EQ_INT(1, E.input_vim_pending_ctrl_w);
	ASSERT_TRUE(vim_test_key('s') == 0);
	ASSERT_EQ_INT(2, editorPaneTreeLeafCount(E.layout_root));
	ASSERT_TRUE(E.layout_root->is_split);
	ASSERT_EQ_INT(EDITOR_SPLIT_HORIZONTAL, E.layout_root->as.split.orientation);
	second = E.focused_leaf;
	ASSERT_TRUE(second != first);

	ASSERT_TRUE(vim_test_key(CTRL_KEY('w')) == 0);
	ASSERT_TRUE(vim_test_key('k') == 0);
	ASSERT_TRUE(E.focused_leaf == first);

	ASSERT_TRUE(vim_test_key(CTRL_KEY('w')) == 0);
	ASSERT_TRUE(vim_test_key('j') == 0);
	ASSERT_TRUE(E.focused_leaf == second);

	ASSERT_TRUE(vim_test_key(CTRL_KEY('w')) == 0);
	ASSERT_TRUE(vim_test_key('q') == 0);
	ASSERT_EQ_INT(1, editorPaneTreeLeafCount(E.layout_root));
	return 0;
}

static int test_input_vim_ctrl_w_cycle_only_and_cancel(void) {
	struct editorPaneNode *a = NULL;
	struct editorPaneNode *b = NULL;
	struct editorPaneNode *c = NULL;

	ASSERT_TRUE(editorTabsInit());
	add_row("hello");
	ASSERT_TRUE(vim_test_activate());
	a = E.focused_leaf;

	ASSERT_TRUE(vim_test_key(CTRL_KEY('w')) == 0);
	ASSERT_TRUE(vim_test_key('s') == 0);
	b = E.focused_leaf;
	ASSERT_TRUE(vim_test_key(CTRL_KEY('w')) == 0);
	ASSERT_TRUE(vim_test_key('v') == 0);
	c = E.focused_leaf;
	ASSERT_EQ_INT(3, editorPaneTreeLeafCount(E.layout_root));

	ASSERT_TRUE(vim_test_key(CTRL_KEY('w')) == 0);
	ASSERT_TRUE(vim_test_key('\x1b') == 0);
	ASSERT_EQ_INT(0, E.input_vim_pending_ctrl_w);
	ASSERT_EQ_INT(3, editorPaneTreeLeafCount(E.layout_root));

	ASSERT_TRUE(vim_test_key(CTRL_KEY('w')) == 0);
	ASSERT_TRUE(vim_test_key('w') == 0);
	ASSERT_TRUE(E.focused_leaf != c);
	ASSERT_TRUE(vim_test_key(CTRL_KEY('w')) == 0);
	ASSERT_TRUE(vim_test_key('W') == 0);
	ASSERT_TRUE(E.focused_leaf == c);
	ASSERT_TRUE(vim_test_key(CTRL_KEY('w')) == 0);
	ASSERT_TRUE(vim_test_key(CTRL_KEY('w')) == 0);
	ASSERT_TRUE(E.focused_leaf != c);

	(void)editorLayoutSetFocusedLeaf(b);
	ASSERT_TRUE(vim_test_key(CTRL_KEY('w')) == 0);
	ASSERT_TRUE(vim_test_key('o') == 0);
	ASSERT_EQ_INT(1, editorPaneTreeLeafCount(E.layout_root));
	ASSERT_TRUE(E.focused_leaf == b);
	(void)a;
	return 0;
}

static int test_input_vim_leader_find_file(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("hello");
	ASSERT_TRUE(vim_test_activate());

	ASSERT_TRUE(vim_test_key(' ') == 0);
	ASSERT_EQ_INT(1, E.input_vim_pending_leader);
	(void)vim_test_key('p');
	ASSERT_EQ_INT(0, E.input_vim_pending_leader);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_FILE_SEARCH, E.drawer_mode);
	return 0;
}

static int test_input_vim_leader_project_search(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("hello");
	ASSERT_TRUE(vim_test_activate());

	ASSERT_TRUE(vim_test_key(' ') == 0);
	(void)vim_test_key('f');
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_PROJECT_SEARCH, E.drawer_mode);
	return 0;
}

static int test_input_vim_leader_toggle_drawer(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("hello");
	ASSERT_TRUE(vim_test_activate());

	ASSERT_EQ_INT(0, E.drawer_collapsed);
	ASSERT_TRUE(vim_test_key(' ') == 0);
	(void)vim_test_key('e');
	ASSERT_EQ_INT(1, E.drawer_collapsed);
	return 0;
}

static int test_input_vim_leader_main_menu(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("hello");
	ASSERT_TRUE(vim_test_activate());

	ASSERT_TRUE(vim_test_key(' ') == 0);
	(void)vim_test_key('m');
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_MAIN_MENU, E.drawer_mode);
	return 0;
}

static int test_input_vim_leader_unknown_key_is_noop(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("hello");
	ASSERT_TRUE(vim_test_activate());

	ASSERT_TRUE(vim_test_key(' ') == 0);
	ASSERT_EQ_INT(1, E.input_vim_pending_leader);
	ASSERT_TRUE(vim_test_key('z') == 0);
	ASSERT_EQ_INT(0, E.input_vim_pending_leader);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_TREE, E.drawer_mode);
	return 0;
}

static int test_input_vim_leader_escape_cancels(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("hello");
	ASSERT_TRUE(vim_test_activate());

	ASSERT_TRUE(vim_test_key(' ') == 0);
	ASSERT_EQ_INT(1, E.input_vim_pending_leader);
	ASSERT_TRUE(vim_test_key('\x1b') == 0);
	ASSERT_EQ_INT(0, E.input_vim_pending_leader);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_TREE, E.drawer_mode);
	return 0;
}

/* A leader sub-key only fires after the leader; the same key alone keeps its
 * normal-mode meaning (here 'm' is unbound, so it must not open the menu). */
static int test_input_vim_leader_subkey_inert_without_leader(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("hello");
	ASSERT_TRUE(vim_test_activate());

	ASSERT_TRUE(vim_test_key('m') == 0);
	ASSERT_EQ_INT(0, E.input_vim_pending_leader);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_TREE, E.drawer_mode);
	return 0;
}

static int test_input_vim_g_prefix_lsp_drawer(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("hello");
	ASSERT_TRUE(vim_test_activate());

	ASSERT_TRUE(vim_test_key('g') == 0);
	(void)vim_test_key('S');
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_LSP, E.drawer_mode);
	return 0;
}

static int test_input_vim_gg_goes_to_first_line(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("one");
	add_row("two");
	add_row("three");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 2;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('g') == 0);
	ASSERT_TRUE(vim_test_key('g') == 0);
	ASSERT_EQ_INT(0, E.cy);
	return 0;
}

/* `gd` dispatches go-to-definition; it must not be read as `g` then a `d`
 * delete operator (which would leave a pending operator / mutate the buffer). */
static int test_input_vim_gd_does_not_start_operator(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("abc");
	add_row("def");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('g') == 0);
	(void)vim_test_key('d');
	/* No pending operator: VIM_SYSTEM_OPERATOR_NONE is 0 (file-local enum). */
	ASSERT_EQ_INT(0, E.input_vim_pending_operator);
	ASSERT_EQ_STR("NORMAL", editorVimModeLabel());
	ASSERT_ROW_TEXT_EQ(0, "abc");
	ASSERT_ROW_TEXT_EQ(1, "def");
	return 0;
}

static int test_input_vim_operator_gg_still_deletes_to_top(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("one");
	add_row("two");
	add_row("three");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 1;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('d') == 0);
	ASSERT_TRUE(vim_test_key('g') == 0);
	ASSERT_TRUE(vim_test_key('g') == 0);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "three");
	return 0;
}

static int test_input_vim_count_capital_g_goes_to_line(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("one");
	add_row("two");
	add_row("  three");
	add_row("four");
	add_row("five");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('3') == 0);
	ASSERT_TRUE(vim_test_key('G') == 0);
	ASSERT_EQ_INT(2, E.cy);
	ASSERT_EQ_INT(2, E.cx);
	return 0;
}

static int test_input_vim_count_capital_g_clamps_past_end(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("one");
	add_row("two");
	add_row("three");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('1') == 0);
	ASSERT_TRUE(vim_test_key('9') == 0);
	ASSERT_TRUE(vim_test_key('G') == 0);
	ASSERT_EQ_INT(2, E.cy);
	return 0;
}

static int test_input_vim_capital_g_goes_to_last_line(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("one");
	add_row("two");
	add_row("three");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('G') == 0);
	ASSERT_EQ_INT(2, E.cy);
	return 0;
}

static int test_input_vim_count_gg_goes_to_line(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("one");
	add_row("two");
	add_row("three");
	add_row("four");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 3;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('2') == 0);
	ASSERT_TRUE(vim_test_key('g') == 0);
	ASSERT_TRUE(vim_test_key('g') == 0);
	ASSERT_EQ_INT(1, E.cy);
	return 0;
}

static int test_input_vim_operator_count_g_deletes_to_line(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("one");
	add_row("two");
	add_row("three");
	add_row("four");
	add_row("five");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 1;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('d') == 0);
	ASSERT_TRUE(vim_test_key('3') == 0);
	ASSERT_TRUE(vim_test_key('G') == 0);
	ASSERT_EQ_INT(3, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "one");
	ASSERT_ROW_TEXT_EQ(1, "four");
	ASSERT_ROW_TEXT_EQ(2, "five");
	return 0;
}

static int test_input_vim_delete_inner_paren(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("foo(bar)baz");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 5; /* on 'a' inside (bar) */
	(void)vim_test_key('d');
	(void)vim_test_key('i');
	(void)vim_test_key('(');
	ASSERT_ROW_TEXT_EQ(0, "foo()baz");
	return 0;
}

static int test_input_vim_delete_around_paren(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("foo(bar)baz");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 5;
	(void)vim_test_key('d');
	(void)vim_test_key('a');
	(void)vim_test_key('(');
	ASSERT_ROW_TEXT_EQ(0, "foobaz");
	return 0;
}

static int test_input_vim_change_inner_brace_enters_insert(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("x{a}y");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 2; /* on 'a' */
	(void)vim_test_key('c');
	(void)vim_test_key('i');
	(void)vim_test_key('{');
	ASSERT_ROW_TEXT_EQ(0, "x{}y");
	ASSERT_EQ_STR("INSERT", editorVimModeLabel());
	return 0;
}

static int test_input_vim_delete_inner_quote(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("say \"hi\" now");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 5; /* on 'h' inside the quotes */
	(void)vim_test_key('d');
	(void)vim_test_key('i');
	(void)vim_test_key('"');
	ASSERT_ROW_TEXT_EQ(0, "say \"\" now");
	return 0;
}

static int test_input_vim_delete_inner_paren_on_open_bracket(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("(ab)cd");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 0; /* on the opening '(' */
	(void)vim_test_key('d');
	(void)vim_test_key('i');
	(void)vim_test_key('(');
	ASSERT_ROW_TEXT_EQ(0, "()cd");
	return 0;
}

static int test_input_vim_visual_inner_paren_selects(void) {
	struct editorSelectionRange range = {0};

	ASSERT_TRUE(editorTabsInit());
	add_row("ab(cd)ef");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 3; /* on 'c' inside (cd) */
	ASSERT_TRUE(vim_test_key('v') == 0);
	ASSERT_TRUE(vim_test_key('i') == 0);
	ASSERT_TRUE(vim_test_key('(') == 0);
	ASSERT_EQ_INT(1, editorGetSelectionRange(&range));
	ASSERT_EQ_INT(0, range.start_cy);
	ASSERT_EQ_INT(3, range.start_cx);
	ASSERT_EQ_INT(0, range.end_cy);
	ASSERT_EQ_INT(5, range.end_cx);
	ASSERT_TRUE(vim_test_key('\x1b') == 0);
	return 0;
}

static int test_input_vim_find_char_forward(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("alpha beta gamma");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('f') == 0);
	ASSERT_TRUE(vim_test_key('b') == 0);
	ASSERT_EQ_INT(6, E.cx); /* on the 'b' of beta */
	return 0;
}

static int test_input_vim_find_char_count_and_repeat(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("a.b.c.d");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 0;
	/* 2f. lands on the second dot. */
	ASSERT_TRUE(vim_test_key('2') == 0);
	ASSERT_TRUE(vim_test_key('f') == 0);
	ASSERT_TRUE(vim_test_key('.') == 0);
	ASSERT_EQ_INT(3, E.cx);
	/* ; repeats to the next dot. */
	ASSERT_TRUE(vim_test_key(';') == 0);
	ASSERT_EQ_INT(5, E.cx);
	/* , reverses to the previous dot. */
	ASSERT_TRUE(vim_test_key(',') == 0);
	ASSERT_EQ_INT(3, E.cx);
	return 0;
}

static int test_input_vim_till_char_forward(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("alpha beta");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('t') == 0);
	ASSERT_TRUE(vim_test_key('b') == 0);
	ASSERT_EQ_INT(5, E.cx); /* the space just before 'b' */
	return 0;
}

static int test_input_vim_find_backward(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("abcabc");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 5; /* last 'c' */
	ASSERT_TRUE(vim_test_key('F') == 0);
	ASSERT_TRUE(vim_test_key('a') == 0);
	ASSERT_EQ_INT(3, E.cx); /* the second 'a' */
	return 0;
}

static int test_input_vim_delete_to_find_char_inclusive(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("hello, world");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 0;
	/* df, deletes through the comma (inclusive). */
	ASSERT_TRUE(vim_test_key('d') == 0);
	(void)vim_test_key('f');
	(void)vim_test_key(',');
	ASSERT_ROW_TEXT_EQ(0, " world");
	return 0;
}

static int test_input_vim_delete_till_char(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("hello, world");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 0;
	/* dt, deletes up to but not including the comma. */
	ASSERT_TRUE(vim_test_key('d') == 0);
	(void)vim_test_key('t');
	(void)vim_test_key(',');
	ASSERT_ROW_TEXT_EQ(0, ", world");
	return 0;
}

static int test_input_vim_paragraph_forward(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("a");
	add_row("b");
	add_row("");
	add_row("c");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('}') == 0);
	ASSERT_EQ_INT(2, E.cy); /* the blank line */
	ASSERT_EQ_INT(0, E.cx);
	return 0;
}

static int test_input_vim_paragraph_backward(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("a");
	add_row("");
	add_row("b");
	add_row("c");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 3;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('{') == 0);
	ASSERT_EQ_INT(1, E.cy); /* the blank line above */
	return 0;
}

static int test_input_vim_delete_paragraph_forward(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("a");
	add_row("b");
	add_row("");
	add_row("c");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('d') == 0);
	(void)vim_test_key('}');
	ASSERT_EQ_INT(2, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "");
	ASSERT_ROW_TEXT_EQ(1, "c");
	return 0;
}

static int test_input_vim_dot_repeat_delete_char(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("abcdef");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('x') == 0);
	ASSERT_ROW_TEXT_EQ(0, "bcdef");
	(void)vim_test_key('.');
	ASSERT_ROW_TEXT_EQ(0, "cdef");
	(void)vim_test_key('.');
	ASSERT_ROW_TEXT_EQ(0, "def");
	return 0;
}

static int test_input_vim_dot_repeat_operator_motion(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("one two three four");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 0;
	/* dw deletes a word; . repeats it. */
	(void)vim_test_key('d');
	(void)vim_test_key('w');
	ASSERT_ROW_TEXT_EQ(0, "two three four");
	(void)vim_test_key('.');
	ASSERT_ROW_TEXT_EQ(0, "three four");
	return 0;
}

static int test_input_vim_dot_repeat_insert_change(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("foo");
	add_row("foo");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 0;
	/* ciw + "bar" + Esc, then repeat on the next line's word. */
	(void)vim_test_key('c');
	(void)vim_test_key('i');
	(void)vim_test_key('w');
	(void)vim_test_key('b');
	(void)vim_test_key('a');
	(void)vim_test_key('r');
	(void)vim_test_key('\x1b');
	ASSERT_ROW_TEXT_EQ(0, "bar");
	E.cy = 1;
	E.cx = 0;
	(void)vim_test_key('.');
	ASSERT_ROW_TEXT_EQ(1, "bar");
	return 0;
}

static int test_input_vim_dot_repeat_ignores_navigation(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("abcdef");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('x') == 0); /* delete 'a' */
	ASSERT_ROW_TEXT_EQ(0, "bcdef");
	(void)vim_test_key('l'); /* navigation must not replace the dot command */
	(void)vim_test_key('.');
	ASSERT_ROW_TEXT_EQ(0, "bdef");
	return 0;
}

static int test_input_vim_delete_inner_tag(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("<p>hello</p>");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 4; /* inside "hello" */
	(void)vim_test_key('d');
	(void)vim_test_key('i');
	(void)vim_test_key('t');
	ASSERT_ROW_TEXT_EQ(0, "<p></p>");
	return 0;
}

static int test_input_vim_delete_around_tag(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("x<b>hi</b>y");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 4; /* inside "hi" */
	(void)vim_test_key('d');
	(void)vim_test_key('a');
	(void)vim_test_key('t');
	ASSERT_ROW_TEXT_EQ(0, "xy");
	return 0;
}

static int test_input_vim_inner_tag_nested(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("<a><b>z</b></a>");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 6; /* on 'z', inside <b> */
	(void)vim_test_key('d');
	(void)vim_test_key('i');
	(void)vim_test_key('t');
	ASSERT_ROW_TEXT_EQ(0, "<a><b></b></a>");
	return 0;
}

static int test_input_vim_reflow_line(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("aaa bbb ccc ddd eee fff");
	ASSERT_TRUE(vim_test_activate());

	E.text_width = 20;
	E.cy = 0;
	E.cx = 0;
	/* gqq re-wraps the current line at text_width. */
	(void)vim_test_key('g');
	(void)vim_test_key('q');
	(void)vim_test_key('q');
	ASSERT_EQ_INT(2, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "aaa bbb ccc ddd eee");
	ASSERT_ROW_TEXT_EQ(1, "fff");
	return 0;
}

static int test_input_vim_reflow_joins_paragraph(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("hello");
	add_row("world");
	add_row("foo");
	ASSERT_TRUE(vim_test_activate());

	E.text_width = 80;
	E.cy = 0;
	E.cx = 0;
	/* gqip joins the paragraph's short lines (well under text_width). */
	(void)vim_test_key('g');
	(void)vim_test_key('q');
	(void)vim_test_key('i');
	(void)vim_test_key('p');
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "hello world foo");
	return 0;
}

static int test_input_vim_reflow_preserves_indent(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("  alpha beta gamma delta");
	ASSERT_TRUE(vim_test_activate());

	E.text_width = 12;
	E.cy = 0;
	E.cx = 0;
	(void)vim_test_key('g');
	(void)vim_test_key('q');
	(void)vim_test_key('q');
	ASSERT_EQ_INT(3, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "  alpha beta");
	ASSERT_ROW_TEXT_EQ(1, "  gamma");
	ASSERT_ROW_TEXT_EQ(2, "  delta");
	return 0;
}

static int test_input_vim_visual_reflow(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("one two");
	add_row("three four");
	ASSERT_TRUE(vim_test_activate());

	E.text_width = 80;
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('V') == 0);
	ASSERT_TRUE(vim_test_key('j') == 0);
	(void)vim_test_key('g');
	(void)vim_test_key('q');
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "one two three four");
	ASSERT_EQ_STR("NORMAL", editorVimModeLabel());
	return 0;
}

static int test_input_vim_bracket_prefix_diagnostic(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("hello");
	ASSERT_TRUE(vim_test_activate());

	/* `]` arms the bracket prefix; `g` consumes it (no diagnostics -> message). */
	ASSERT_TRUE(vim_test_key(']') == 0);
	ASSERT_EQ_INT(']', E.input_vim_pending_bracket);
	(void)vim_test_key('g');
	ASSERT_EQ_INT(0, E.input_vim_pending_bracket);
	ASSERT_TRUE(strstr(E.statusmsg, "No diagnostics") != NULL);
	/* A non-`g` second key cancels the prefix without acting. */
	ASSERT_TRUE(vim_test_key('[') == 0);
	ASSERT_TRUE(vim_test_key('x') == 0);
	ASSERT_EQ_INT(0, E.input_vim_pending_bracket);
	return 0;
}

static int test_input_vim_gb_git_blame_details_reports_no_repo(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("hello");
	E.filename = strdup("/tmp/rotide-vim-gb.c");
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 0;
	ASSERT_TRUE(vim_test_activate());

	ASSERT_TRUE(vim_test_key('g') == 0);
	ASSERT_EQ_INT(1, E.input_vim_pending_g);
	ASSERT_TRUE(vim_test_key('b') == 0);
	ASSERT_EQ_INT(0, E.input_vim_pending_g);
	ASSERT_EQ_STR("No Git repository", E.statusmsg);
	ASSERT_TRUE(!editorPopupIsVisible());
	return 0;
}

static int test_input_vim_mark_set_and_jump(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("first line");
	add_row("second line");
	add_row("third line");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 1;
	E.cx = 4;
	ASSERT_TRUE(vim_test_key('m') == 0);
	ASSERT_TRUE(vim_test_key('a') == 0);
	/* Move away, then jump back to the exact mark with backtick. */
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('`') == 0);
	ASSERT_TRUE(vim_test_key('a') == 0);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(4, E.cx);
	/* Line-jump with quote lands on the first non-blank column. */
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('\'') == 0);
	ASSERT_TRUE(vim_test_key('a') == 0);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(0, E.cx);
	return 0;
}

static int test_input_vim_indent_line(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("foo");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('>') == 0);
	ASSERT_TRUE(vim_test_key('>') == 0);
	ASSERT_ROW_TEXT_EQ(0, "    foo");
	/* << removes one indent level. */
	ASSERT_TRUE(vim_test_key('<') == 0);
	ASSERT_TRUE(vim_test_key('<') == 0);
	ASSERT_ROW_TEXT_EQ(0, "foo");
	return 0;
}

static int test_input_vim_indent_count(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("a");
	add_row("b");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('2') == 0);
	ASSERT_TRUE(vim_test_key('>') == 0);
	ASSERT_TRUE(vim_test_key('>') == 0);
	ASSERT_ROW_TEXT_EQ(0, "    a");
	ASSERT_ROW_TEXT_EQ(1, "    b");
	return 0;
}

static int test_input_vim_visual_indent(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("a");
	add_row("b");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('V') == 0);
	ASSERT_TRUE(vim_test_key('j') == 0);
	ASSERT_TRUE(vim_test_key('>') == 0);
	ASSERT_ROW_TEXT_EQ(0, "    a");
	ASSERT_ROW_TEXT_EQ(1, "    b");
	ASSERT_EQ_STR("NORMAL", editorVimModeLabel());
	return 0;
}

static int test_input_vim_screen_motions(void) {
	int h_row;
	int m_row;
	int l_row;

	ASSERT_TRUE(editorTabsInit());
	for (int i = 0; i < 20; i++) {
		add_row("line");
	}
	ASSERT_TRUE(vim_test_activate());

	E.rowoff = 4;
	E.cy = 12;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('H') == 0);
	h_row = E.cy;
	ASSERT_EQ_INT(4, h_row); /* top visible row == rowoff */

	E.cy = 12;
	ASSERT_TRUE(vim_test_key('L') == 0);
	l_row = E.cy;
	E.cy = 12;
	ASSERT_TRUE(vim_test_key('M') == 0);
	m_row = E.cy;

	ASSERT_TRUE(h_row <= m_row);
	ASSERT_TRUE(m_row <= l_row);
	ASSERT_TRUE(l_row <= E.numrows - 1);
	return 0;
}

static int test_input_vim_match_bracket_motion(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("a(bc)d");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 1; /* on '(' */
	ASSERT_TRUE(vim_test_key('%') == 0);
	ASSERT_EQ_INT(4, E.cx); /* on ')' */
	ASSERT_TRUE(vim_test_key('%') == 0);
	ASSERT_EQ_INT(1, E.cx); /* back on '(' */
	return 0;
}

static int test_input_vim_match_bracket_scans_forward(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("ab(cd)");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 0; /* before the bracket; % scans to '(' first */
	ASSERT_TRUE(vim_test_key('%') == 0);
	ASSERT_EQ_INT(5, E.cx);
	return 0;
}

static int test_input_vim_delete_to_match_bracket(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("a(bc)d");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(vim_test_key('d') == 0);
	(void)vim_test_key('%');
	ASSERT_ROW_TEXT_EQ(0, "ad");
	return 0;
}

static int test_input_vim_search_word_under_cursor(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("foo bar foo");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('*') == 0);
	ASSERT_EQ_INT(8, E.cx); /* second foo */
	/* # searches back to the first occurrence. */
	ASSERT_TRUE(vim_test_key('#') == 0);
	ASSERT_EQ_INT(0, E.cx);
	return 0;
}

static int test_input_vim_replace_char(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("abc");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(vim_test_key('r') == 0);
	(void)vim_test_key('x');
	ASSERT_ROW_TEXT_EQ(0, "axc");
	ASSERT_EQ_INT(1, E.cx);
	return 0;
}

static int test_input_vim_replace_char_count(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("abcde");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('3') == 0);
	ASSERT_TRUE(vim_test_key('r') == 0);
	(void)vim_test_key('z');
	ASSERT_ROW_TEXT_EQ(0, "zzzde");
	ASSERT_EQ_INT(2, E.cx);
	return 0;
}

static int test_input_vim_replace_char_large_count(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("abcdefghijkl");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('9') == 0);
	ASSERT_TRUE(vim_test_key('r') == 0);
	(void)vim_test_key('z');
	ASSERT_ROW_TEXT_EQ(0, "zzzzzzzzzjkl");
	ASSERT_EQ_INT(8, E.cx);
	return 0;
}

static int test_input_vim_toggle_case(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("aBc");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('~') == 0);
	ASSERT_ROW_TEXT_EQ(0, "ABc");
	ASSERT_EQ_INT(1, E.cx);
	return 0;
}

static int test_input_vim_join_lines(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("foo");
	add_row("   bar");
	ASSERT_TRUE(vim_test_activate());

	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('J') == 0);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "foo bar");
	return 0;
}

static int test_input_vim_normal_u_undoes_and_ctrl_r_redoes(void) {
	add_row("abc");

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('x') == 0);
	ASSERT_ROW_TEXT_EQ(0, "bc");

	ASSERT_TRUE(vim_test_key('u') == 0);
	ASSERT_ROW_TEXT_EQ(0, "abc");
	ASSERT_EQ_STR("NORMAL", editorVimModeLabel());

	ASSERT_TRUE(vim_test_key(CTRL_KEY('r')) == 0);
	ASSERT_ROW_TEXT_EQ(0, "bc");
	return 0;
}

static int test_input_vim_undo_is_not_dot_repeatable(void) {
	add_row("abcd");

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('x') == 0);
	ASSERT_ROW_TEXT_EQ(0, "bcd");
	ASSERT_TRUE(vim_test_key('u') == 0);
	ASSERT_ROW_TEXT_EQ(0, "abcd");

	E.cx = 0;
	ASSERT_TRUE(vim_test_key('.') == 0);
	ASSERT_ROW_TEXT_EQ(0, "bcd");
	return 0;
}

static int test_input_vim_ctrl_keys_do_not_trigger_cua(void) {
	add_row("abc");

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('x') == 0);
	ASSERT_ROW_TEXT_EQ(0, "bc");
	ASSERT_TRUE(vim_test_key('u') == 0);
	ASSERT_ROW_TEXT_EQ(0, "abc");

	ASSERT_TRUE(vim_test_key(CTRL_KEY('y')) == 0);
	ASSERT_ROW_TEXT_EQ(0, "abc");
	return 0;
}

static int test_input_vim_visual_linewise_selection_spans_full_lines(void) {
	struct editorSelectionRange range = {0};

	add_row("  alpha");
	add_row("beta");
	add_row("gamma");

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 4;
	ASSERT_TRUE(vim_test_key('V') == 0);
	ASSERT_TRUE(vim_test_key('j') == 0);
	ASSERT_EQ_INT(1, editorGetSelectionRange(&range));
	ASSERT_EQ_INT(0, range.start_cy);
	ASSERT_EQ_INT(0, range.start_cx);
	ASSERT_EQ_INT(1, range.end_cy);
	ASSERT_EQ_INT(4, range.end_cx);
	ASSERT_TRUE(vim_test_key('\x1b') == 0);
	ASSERT_EQ_INT(0, E.selection_mode_active);
	return 0;
}

static int test_input_vim_visual_charwise_single_cell_is_selected(void) {
	struct editorSelectionRange range = {0};

	add_row("abc");

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(vim_test_key('v') == 0);
	ASSERT_EQ_INT(1, editorGetSelectionRange(&range));
	ASSERT_EQ_INT(0, range.start_cy);
	ASSERT_EQ_INT(1, range.start_cx);
	ASSERT_EQ_INT(0, range.end_cy);
	ASSERT_EQ_INT(2, range.end_cx);
	ASSERT_TRUE(vim_test_key('\x1b') == 0);
	return 0;
}

static int test_input_vim_insert_ctrl_w_deletes_word(void) {
	add_row("foo bar");

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = (int)strlen("foo bar");
	ASSERT_TRUE(vim_test_key('i') == 0);
	ASSERT_TRUE(vim_test_key(CTRL_KEY('w')) == 0);
	ASSERT_ROW_TEXT_EQ(0, "foo ");
	ASSERT_TRUE(vim_test_key(CTRL_KEY('w')) == 0);
	ASSERT_ROW_TEXT_EQ(0, "");
	return 0;
}

static int test_input_vim_insert_ctrl_u_deletes_to_line_start(void) {
	add_row("  hello world");

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = (int)strlen("  hello world");
	ASSERT_TRUE(vim_test_key('i') == 0);
	ASSERT_TRUE(vim_test_key(CTRL_KEY('u')) == 0);
	ASSERT_ROW_TEXT_EQ(0, "  ");
	ASSERT_TRUE(vim_test_key(CTRL_KEY('u')) == 0);
	ASSERT_ROW_TEXT_EQ(0, "");
	return 0;
}

static int test_input_vim_insert_ctrl_c_returns_to_normal(void) {
	add_row("ab");

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('i') == 0);
	ASSERT_EQ_STR("INSERT", editorVimModeLabel());
	ASSERT_TRUE(vim_test_key(CTRL_KEY('c')) == 0);
	ASSERT_EQ_STR("NORMAL", editorVimModeLabel());
	return 0;
}

static int test_input_vim_insert_ctrl_key_does_not_insert_or_trigger_cua(void) {
	add_row("");

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key('i') == 0);
	ASSERT_TRUE(vim_test_key(CTRL_KEY('p')) == 0);
	ASSERT_ROW_TEXT_EQ(0, "");
	ASSERT_EQ_STR("INSERT", editorVimModeLabel());
	return 0;
}

static int test_input_vim_ctrl_d_moves_cursor_down(void) {
	for (int i = 0; i < 40; i++) {
		add_row("line");
	}

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key(CTRL_KEY('d')) == 0);
	ASSERT_TRUE(E.cy > 0);
	int after_down = E.cy;
	ASSERT_TRUE(vim_test_key(CTRL_KEY('u')) == 0);
	ASSERT_TRUE(E.cy < after_down);
	return 0;
}

static int test_input_vim_visual_block_delete(void) {
	add_row("hello");
	add_row("world");

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(vim_test_key(CTRL_KEY('v')) == 0);
	ASSERT_EQ_STR("VISUAL BLOCK", editorVimModeLabel());
	ASSERT_TRUE(vim_test_key('j') == 0);
	ASSERT_TRUE(vim_test_key('l') == 0);
	ASSERT_TRUE(vim_test_key('d') == 0);
	ASSERT_ROW_TEXT_EQ(0, "hlo");
	ASSERT_ROW_TEXT_EQ(1, "wld");
	ASSERT_EQ_STR("NORMAL", editorVimModeLabel());
	ASSERT_EQ_INT(0, E.column_select_active);
	return 0;
}

static int test_input_vim_visual_block_yank(void) {
	add_row("hello");
	add_row("world");

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(vim_test_key(CTRL_KEY('v')) == 0);
	ASSERT_TRUE(vim_test_key('j') == 0);
	ASSERT_TRUE(vim_test_key('l') == 0);
	ASSERT_TRUE(vim_test_key('y') == 0);
	ASSERT_EQ_INT(0, vim_test_clipboard_eq("el\nor"));
	ASSERT_ROW_TEXT_EQ(0, "hello");
	ASSERT_EQ_STR("NORMAL", editorVimModeLabel());
	return 0;
}

static int test_input_vim_visual_block_yank_named_register(void) {
	add_row("hello");
	add_row("world");

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(vim_test_key(CTRL_KEY('v')) == 0);
	ASSERT_TRUE(vim_test_key('j') == 0);
	ASSERT_TRUE(vim_test_key('l') == 0);
	ASSERT_TRUE(vim_test_key('"') == 0);
	ASSERT_TRUE(vim_test_key('a') == 0);
	ASSERT_TRUE(vim_test_key('y') == 0);
	ASSERT_TRUE(E.vim_registers[0].text != NULL);
	ASSERT_EQ_INT(5, (int)E.vim_registers[0].len);
	ASSERT_TRUE(memcmp(E.vim_registers[0].text, "el\nor", 5) == 0);
	ASSERT_EQ_INT(0, E.vim_registers[0].linewise);
	ASSERT_EQ_STR("NORMAL", editorVimModeLabel());
	return 0;
}

static int test_input_vim_visual_block_change_enters_insert(void) {
	add_row("hello");
	add_row("world");

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 1;
	ASSERT_TRUE(vim_test_key(CTRL_KEY('v')) == 0);
	ASSERT_TRUE(vim_test_key('j') == 0);
	ASSERT_TRUE(vim_test_key('c') == 0);
	ASSERT_ROW_TEXT_EQ(0, "hllo");
	ASSERT_ROW_TEXT_EQ(1, "wrld");
	ASSERT_EQ_STR("INSERT", editorVimModeLabel());
	return 0;
}

static int test_input_vim_visual_block_escape_clears(void) {
	add_row("hello");
	add_row("world");

	ASSERT_TRUE(vim_test_activate());
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(vim_test_key(CTRL_KEY('v')) == 0);
	ASSERT_EQ_INT(1, E.column_select_active);
	ASSERT_TRUE(vim_test_key('\x1b') == 0);
	ASSERT_EQ_INT(0, E.column_select_active);
	ASSERT_EQ_STR("NORMAL", editorVimModeLabel());
	return 0;
}

const struct editorTestCase g_input_vim_tests[] = {
        {"input_vim_visual_block_delete", test_input_vim_visual_block_delete},
        {"input_vim_visual_block_yank", test_input_vim_visual_block_yank},
        {"input_vim_visual_block_yank_named_register",
         test_input_vim_visual_block_yank_named_register},
        {"input_vim_visual_block_change_enters_insert",
         test_input_vim_visual_block_change_enters_insert},
        {"input_vim_visual_block_escape_clears", test_input_vim_visual_block_escape_clears},
        {"input_vim_normal_u_undoes_and_ctrl_r_redoes",
         test_input_vim_normal_u_undoes_and_ctrl_r_redoes},
        {"input_vim_undo_is_not_dot_repeatable", test_input_vim_undo_is_not_dot_repeatable},
        {"input_vim_ctrl_keys_do_not_trigger_cua", test_input_vim_ctrl_keys_do_not_trigger_cua},
        {"input_vim_visual_linewise_selection_spans_full_lines",
         test_input_vim_visual_linewise_selection_spans_full_lines},
        {"input_vim_visual_charwise_single_cell_is_selected",
         test_input_vim_visual_charwise_single_cell_is_selected},
        {"input_vim_insert_ctrl_w_deletes_word", test_input_vim_insert_ctrl_w_deletes_word},
        {"input_vim_insert_ctrl_u_deletes_to_line_start",
         test_input_vim_insert_ctrl_u_deletes_to_line_start},
        {"input_vim_insert_ctrl_c_returns_to_normal",
         test_input_vim_insert_ctrl_c_returns_to_normal},
        {"input_vim_insert_ctrl_key_does_not_insert_or_trigger_cua",
         test_input_vim_insert_ctrl_key_does_not_insert_or_trigger_cua},
        {"input_vim_ctrl_d_moves_cursor_down", test_input_vim_ctrl_d_moves_cursor_down},
        {"input_vim_activation_starts_normal", test_input_vim_activation_starts_normal},
        {"input_vim_reset_returns_to_normal", test_input_vim_reset_returns_to_normal},
        {"input_vim_cursor_style_is_block_outside_insert",
         test_input_vim_cursor_style_is_block_outside_insert},
        {"input_vim_leader_find_file", test_input_vim_leader_find_file},
        {"input_vim_leader_project_search", test_input_vim_leader_project_search},
        {"input_vim_leader_toggle_drawer", test_input_vim_leader_toggle_drawer},
        {"input_vim_leader_main_menu", test_input_vim_leader_main_menu},
        {"input_vim_leader_unknown_key_is_noop", test_input_vim_leader_unknown_key_is_noop},
        {"input_vim_leader_escape_cancels", test_input_vim_leader_escape_cancels},
        {"input_vim_leader_subkey_inert_without_leader",
         test_input_vim_leader_subkey_inert_without_leader},
        {"input_vim_g_prefix_lsp_drawer", test_input_vim_g_prefix_lsp_drawer},
        {"input_vim_gg_goes_to_first_line", test_input_vim_gg_goes_to_first_line},
        {"input_vim_gd_does_not_start_operator", test_input_vim_gd_does_not_start_operator},
        {"input_vim_operator_gg_still_deletes_to_top",
         test_input_vim_operator_gg_still_deletes_to_top},
        {"input_vim_count_capital_g_goes_to_line", test_input_vim_count_capital_g_goes_to_line},
        {"input_vim_count_capital_g_clamps_past_end",
         test_input_vim_count_capital_g_clamps_past_end},
        {"input_vim_capital_g_goes_to_last_line", test_input_vim_capital_g_goes_to_last_line},
        {"input_vim_count_gg_goes_to_line", test_input_vim_count_gg_goes_to_line},
        {"input_vim_operator_count_g_deletes_to_line",
         test_input_vim_operator_count_g_deletes_to_line},
        {"input_vim_delete_inner_paren", test_input_vim_delete_inner_paren},
        {"input_vim_delete_around_paren", test_input_vim_delete_around_paren},
        {"input_vim_change_inner_brace_enters_insert",
         test_input_vim_change_inner_brace_enters_insert},
        {"input_vim_delete_inner_quote", test_input_vim_delete_inner_quote},
        {"input_vim_delete_inner_paren_on_open_bracket",
         test_input_vim_delete_inner_paren_on_open_bracket},
        {"input_vim_visual_inner_paren_selects", test_input_vim_visual_inner_paren_selects},
        {"input_vim_find_char_forward", test_input_vim_find_char_forward},
        {"input_vim_find_char_count_and_repeat", test_input_vim_find_char_count_and_repeat},
        {"input_vim_till_char_forward", test_input_vim_till_char_forward},
        {"input_vim_find_backward", test_input_vim_find_backward},
        {"input_vim_delete_to_find_char_inclusive", test_input_vim_delete_to_find_char_inclusive},
        {"input_vim_delete_till_char", test_input_vim_delete_till_char},
        {"input_vim_paragraph_forward", test_input_vim_paragraph_forward},
        {"input_vim_paragraph_backward", test_input_vim_paragraph_backward},
        {"input_vim_delete_paragraph_forward", test_input_vim_delete_paragraph_forward},
        {"input_vim_dot_repeat_delete_char", test_input_vim_dot_repeat_delete_char},
        {"input_vim_dot_repeat_operator_motion", test_input_vim_dot_repeat_operator_motion},
        {"input_vim_dot_repeat_insert_change", test_input_vim_dot_repeat_insert_change},
        {"input_vim_dot_repeat_ignores_navigation", test_input_vim_dot_repeat_ignores_navigation},
        {"input_vim_delete_inner_tag", test_input_vim_delete_inner_tag},
        {"input_vim_delete_around_tag", test_input_vim_delete_around_tag},
        {"input_vim_inner_tag_nested", test_input_vim_inner_tag_nested},
        {"input_vim_reflow_line", test_input_vim_reflow_line},
        {"input_vim_reflow_joins_paragraph", test_input_vim_reflow_joins_paragraph},
        {"input_vim_reflow_preserves_indent", test_input_vim_reflow_preserves_indent},
        {"input_vim_visual_reflow", test_input_vim_visual_reflow},
        {"input_vim_bracket_prefix_diagnostic", test_input_vim_bracket_prefix_diagnostic},
        {"input_vim_gb_git_blame_details_reports_no_repo",
         test_input_vim_gb_git_blame_details_reports_no_repo},
        {"input_vim_mark_set_and_jump", test_input_vim_mark_set_and_jump},
        {"input_vim_indent_line", test_input_vim_indent_line},
        {"input_vim_indent_count", test_input_vim_indent_count},
        {"input_vim_visual_indent", test_input_vim_visual_indent},
        {"input_vim_screen_motions", test_input_vim_screen_motions},
        {"input_vim_match_bracket_motion", test_input_vim_match_bracket_motion},
        {"input_vim_match_bracket_scans_forward", test_input_vim_match_bracket_scans_forward},
        {"input_vim_delete_to_match_bracket", test_input_vim_delete_to_match_bracket},
        {"input_vim_search_word_under_cursor", test_input_vim_search_word_under_cursor},
        {"input_vim_replace_char", test_input_vim_replace_char},
        {"input_vim_replace_char_count", test_input_vim_replace_char_count},
        {"input_vim_replace_char_large_count", test_input_vim_replace_char_large_count},
        {"input_vim_toggle_case", test_input_vim_toggle_case},
        {"input_vim_join_lines", test_input_vim_join_lines},
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
        {"input_vim_visual_arrow_keys_grow_selection",
         test_input_vim_visual_arrow_keys_grow_selection},
        {"input_vim_normal_arrow_keys_move_as_motions",
         test_input_vim_normal_arrow_keys_move_as_motions},
        {"input_vim_operator_arrow_down_deletes_lines",
         test_input_vim_operator_arrow_down_deletes_lines},
        {"input_vim_mode_is_tab_local", test_input_vim_mode_is_tab_local},
        {"input_vim_normal_delete_and_change_operators",
         test_input_vim_normal_delete_and_change_operators},
        {"input_vim_operator_motion_delete_yank_and_change",
         test_input_vim_operator_motion_delete_yank_and_change},
        {"input_vim_linewise_operators_and_paste", test_input_vim_linewise_operators_and_paste},
        {"input_vim_read_only_tab_rejects_vim_mutations",
         test_input_vim_read_only_tab_rejects_vim_mutations},
        {"input_vim_visual_charwise_operations_include_cursor",
         test_input_vim_visual_charwise_operations_include_cursor},
        {"input_vim_visual_single_cluster_delete_handles_multibyte",
         test_input_vim_visual_single_cluster_delete_handles_multibyte},
        {"input_vim_charwise_paste_and_redo", test_input_vim_charwise_paste_and_redo},
        {"input_vim_default_register_linewise_persists_across_tabs",
         test_input_vim_default_register_linewise_persists_across_tabs},
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
        {"input_vim_ex_window_aliases", test_input_vim_ex_window_aliases},
        {"input_vim_ex_file_argument_commands", test_input_vim_ex_file_argument_commands},
        {"input_vim_ex_tab_and_terminal_aliases", test_input_vim_ex_tab_and_terminal_aliases},
        {"input_vim_ex_completion_cycles_commands", test_input_vim_ex_completion_cycles_commands},
        {"input_vim_ctrl_w_split_focus_and_close", test_input_vim_ctrl_w_split_focus_and_close},
        {"input_vim_ctrl_w_cycle_only_and_cancel", test_input_vim_ctrl_w_cycle_only_and_cancel},
};

const int g_input_vim_test_count = (int)(sizeof(g_input_vim_tests) / sizeof(g_input_vim_tests[0]));
