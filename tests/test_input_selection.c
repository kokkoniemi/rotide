#include "test_case.h"
#include "test_support.h"
#include "editing/selection.h"
#include "workspace/drawer.h"
#include "workspace/file_search.h"
#include "workspace/layout.h"
#include "workspace/project_search.h"
#include "workspace/tabs.h"

static int test_editor_column_select_extends_rectangle_with_shift_alt_arrows(void) {
	add_row("hello world");
	add_row("foobar baz!");
	add_row("0123456789a");
	E.cy = 0;
	E.cx = 2;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;

	const char alt_shift_down[] = "\x1b[1;4B";
	const char alt_shift_right[] = "\x1b[1;4C";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_down,
				sizeof(alt_shift_down) - 1) == 0);
	ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_down,
				sizeof(alt_shift_down) - 1) == 0);
	for (int i = 0; i < 4; i++) {
		ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_right,
					sizeof(alt_shift_right) - 1) == 0);
	}
	ASSERT_EQ_INT(1, E.column_select_active);

	struct editorColumnSelectionRect rect;
	ASSERT_TRUE(editorColumnSelectionGetRect(&rect));
	ASSERT_EQ_INT(0, rect.top_cy);
	ASSERT_EQ_INT(2, rect.bottom_cy);
	ASSERT_EQ_INT(2, rect.left_rx);
	ASSERT_EQ_INT(6, rect.right_rx);
	return 0;
}

static int test_editor_column_select_copy_joins_per_row_slices_with_newlines(void) {
	add_row("hello world");
	add_row("foobar baz!");
	add_row("0123456789a");
	E.cy = 0;
	E.cx = 2;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;

	const char alt_shift_down[] = "\x1b[1;4B";
	const char alt_shift_right[] = "\x1b[1;4C";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_down,
				sizeof(alt_shift_down) - 1) == 0);
	ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_down,
				sizeof(alt_shift_down) - 1) == 0);
	for (int i = 0; i < 4; i++) {
		ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_right,
					sizeof(alt_shift_right) - 1) == 0);
	}

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('c')) == 0);
	size_t clip_len = 0;
	const char *clip = editorClipboardGet(&clip_len);
	ASSERT_TRUE(clip != NULL);
	ASSERT_EQ_INT(14, (int)clip_len);
	ASSERT_MEM_EQ("llo \nobar\n2345", clip, clip_len);
	return 0;
}

static int test_editor_column_select_delete_removes_rectangle_per_row(void) {
	add_row("hello world");
	add_row("foobar baz!");
	add_row("0123456789a");
	E.cy = 0;
	E.cx = 2;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;

	const char alt_shift_down[] = "\x1b[1;4B";
	const char alt_shift_right[] = "\x1b[1;4C";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_down,
				sizeof(alt_shift_down) - 1) == 0);
	ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_down,
				sizeof(alt_shift_down) - 1) == 0);
	for (int i = 0; i < 4; i++) {
		ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_right,
					sizeof(alt_shift_right) - 1) == 0);
	}

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('d')) == 0);
	ASSERT_EQ_INT(0, E.column_select_active);
	ASSERT_EQ_STR("heworld", E.rows[0].chars);
	ASSERT_EQ_STR("fo baz!", E.rows[1].chars);
	ASSERT_EQ_STR("016789a", E.rows[2].chars);
	return 0;
}

static int test_editor_column_select_typing_inserts_char_on_each_row(void) {
	add_row("aaaa");
	add_row("bbbb");
	add_row("cccc");
	E.cy = 0;
	E.cx = 1;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;

	const char alt_shift_down[] = "\x1b[1;4B";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_down,
				sizeof(alt_shift_down) - 1) == 0);
	ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_down,
				sizeof(alt_shift_down) - 1) == 0);

	ASSERT_TRUE(editor_process_single_key('X') == 0);
	ASSERT_EQ_STR("aXaaa", E.rows[0].chars);
	ASSERT_EQ_STR("bXbbb", E.rows[1].chars);
	ASSERT_EQ_STR("cXccc", E.rows[2].chars);
	ASSERT_EQ_INT(1, E.column_select_active);

	// After typing the rect must remain multi-row (width 0) so subsequent typing
	// continues to insert on every row in the original column-selection.
	struct editorColumnSelectionRect rect;
	ASSERT_TRUE(editorColumnSelectionGetRect(&rect));
	ASSERT_EQ_INT(0, rect.top_cy);
	ASSERT_EQ_INT(2, rect.bottom_cy);
	ASSERT_EQ_INT(rect.right_rx, rect.left_rx);

	ASSERT_TRUE(editor_process_single_key('Y') == 0);
	ASSERT_EQ_STR("aXYaaa", E.rows[0].chars);
	ASSERT_EQ_STR("bXYbbb", E.rows[1].chars);
	ASSERT_EQ_STR("cXYccc", E.rows[2].chars);
	return 0;
}

static int test_editor_column_select_toggling_linear_selection_clears_column_mode(void) {
	add_row("abcdef");
	add_row("ghijkl");
	E.cy = 0;
	E.cx = 1;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;

	const char alt_shift_down[] = "\x1b[1;4B";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_down,
				sizeof(alt_shift_down) - 1) == 0);
	ASSERT_EQ_INT(1, E.column_select_active);

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('b')) == 0);
	ASSERT_EQ_INT(0, E.column_select_active);
	ASSERT_EQ_INT(1, E.selection_mode_active);
	return 0;
}

static int test_editor_column_select_plain_arrow_clears_mode(void) {
	add_row("abcdef");
	add_row("ghijkl");
	E.cy = 0;
	E.cx = 1;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;

	const char alt_shift_down[] = "\x1b[1;4B";
	const char arrow_down[] = "\x1b[B";
	ASSERT_TRUE(editor_process_keypress_with_input(alt_shift_down,
				sizeof(alt_shift_down) - 1) == 0);
	ASSERT_EQ_INT(1, E.column_select_active);

	ASSERT_TRUE(editor_process_keypress_with_input(arrow_down,
				sizeof(arrow_down) - 1) == 0);
	ASSERT_EQ_INT(0, E.column_select_active);
	return 0;
}

static int test_editor_process_keypress_typed_char_replaces_selection(void) {
	add_row("hello world");
	E.cy = 0;
	E.cx = 0;
	E.selection_mode_active = 1;
	ASSERT_TRUE(set_selection_anchor(0, 0));
	E.cy = 0;
	E.cx = 5;

	ASSERT_TRUE(editor_process_single_key('Z') == 0);
	ASSERT_EQ_STR("Z world", E.rows[0].chars);
	ASSERT_EQ_INT(0, E.selection_mode_active);
	ASSERT_EQ_INT(1, E.cx);
	ASSERT_TRUE(assert_active_source_matches_rows() == 0);
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
	ASSERT_EQ_INT(0, assert_selection_anchor(0, 2));

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

static int test_editor_extract_range_text_uses_document_when_row_cache_corrupt(void) {
	add_row("abc");
	E.rows[0].size = INT_MAX;

	struct editorSelectionRange range = {
		.start_cy = 0,
		.start_cx = 0,
		.end_cy = 1,
		.end_cx = 0
	};
	char *text = (char *)1;
	size_t len = 123;
	int extracted = editorExtractRangeText(&range, &text, &len);
	ASSERT_EQ_INT(1, extracted);
	ASSERT_TRUE(text != NULL);
	ASSERT_EQ_INT(4, len);
	ASSERT_MEM_EQ("abc\n", text, len);
	free(text);
	return 0;
}

static int test_editor_delete_range_uses_document_when_row_cache_corrupt(void) {
	add_row("abc");
	E.rows[0].size = INT_MAX;

	struct editorSelectionRange range = {
		.start_cy = 0,
		.start_cx = 0,
		.end_cy = 1,
		.end_cx = 0
	};
	int deleted = editorDeleteRange(&range);
	ASSERT_EQ_INT(1, deleted);
	ASSERT_EQ_INT(0, assert_active_source_matches_rows());
	size_t len = 123;
	char *text = editorRowsToStr(&len);
	ASSERT_TRUE(text != NULL);
	ASSERT_EQ_INT(0, len);
	free(text);
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

static int test_editor_process_keypress_ctrl_v_auto_indents_multiline_clipboard_text(void) {
	ASSERT_TRUE(editorClipboardSet("one\ntwo\n\nthree", 14));
	add_row("    ");
	E.auto_indent_enabled = 1;
	E.indent_use_tabs = 0;
	E.indent_width = 4;
	E.cy = 0;
	E.cx = 4;

	ASSERT_TRUE(editor_process_single_key(CTRL_KEY('v')) == 0);
	ASSERT_EQ_INT(4, E.numrows);
	ASSERT_EQ_STR("    one", E.rows[0].chars);
	ASSERT_EQ_STR("    two", E.rows[1].chars);
	ASSERT_EQ_STR("", E.rows[2].chars);
	ASSERT_EQ_STR("    three", E.rows[3].chars);
	ASSERT_EQ_INT(3, E.cy);
	ASSERT_EQ_INT(9, E.cx);
	ASSERT_EQ_STR("Pasted 14 bytes", E.statusmsg);
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

static int test_editor_refresh_screen_highlights_active_selection_spans(void) {
	add_row("prefix alpha suffix");
	E.window_rows = 3;
	E.window_cols = 40;
	E.cy = 0;
	E.cx = 12;
	E.selection_mode_active = 1;
	ASSERT_TRUE(set_selection_anchor(0, 7));
	ASSERT_TRUE(set_active_search_match(0, 0, 6));

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[7malpha\x1b[m") != NULL);
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
	ASSERT_TRUE(set_selection_anchor(0, 1));

	output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "a\x1b[7mbc\x1b[m") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[7mde\x1b[m\x1b[48;5;236mf") != NULL);
	free(output);
	return 0;
}

const struct editorTestCase g_input_selection_tests[] = {
	{"editor_column_select_extends_rectangle_with_shift_alt_arrows", test_editor_column_select_extends_rectangle_with_shift_alt_arrows},
	{"editor_column_select_copy_joins_per_row_slices_with_newlines", test_editor_column_select_copy_joins_per_row_slices_with_newlines},
	{"editor_column_select_delete_removes_rectangle_per_row", test_editor_column_select_delete_removes_rectangle_per_row},
	{"editor_column_select_typing_inserts_char_on_each_row", test_editor_column_select_typing_inserts_char_on_each_row},
	{"editor_column_select_toggling_linear_selection_clears_column_mode", test_editor_column_select_toggling_linear_selection_clears_column_mode},
	{"editor_column_select_plain_arrow_clears_mode", test_editor_column_select_plain_arrow_clears_mode},
	{"editor_process_keypress_typed_char_replaces_selection", test_editor_process_keypress_typed_char_replaces_selection},
	{"editor_prompt_ignores_resize_events", test_editor_prompt_ignores_resize_events},
	{"editor_process_keypress_ctrl_b_toggles_selection_mode", test_editor_process_keypress_ctrl_b_toggles_selection_mode},
	{"editor_selection_range_tracks_cursor_movement", test_editor_selection_range_tracks_cursor_movement},
	{"editor_extract_range_text_uses_document_when_row_cache_corrupt", test_editor_extract_range_text_uses_document_when_row_cache_corrupt},
	{"editor_delete_range_uses_document_when_row_cache_corrupt", test_editor_delete_range_uses_document_when_row_cache_corrupt},
	{"editor_process_keypress_ctrl_c_copies_single_line_selection", test_editor_process_keypress_ctrl_c_copies_single_line_selection},
	{"editor_process_keypress_ctrl_c_copies_multiline_selection", test_editor_process_keypress_ctrl_c_copies_multiline_selection},
	{"editor_process_keypress_ctrl_x_cuts_selection_and_updates_clipboard", test_editor_process_keypress_ctrl_x_cuts_selection_and_updates_clipboard},
	{"editor_process_keypress_ctrl_d_deletes_selection_without_overwriting_clipboard", test_editor_process_keypress_ctrl_d_deletes_selection_without_overwriting_clipboard},
	{"editor_process_keypress_ctrl_v_pastes_clipboard_text", test_editor_process_keypress_ctrl_v_pastes_clipboard_text},
	{"editor_process_keypress_ctrl_v_pastes_multiline_clipboard_text", test_editor_process_keypress_ctrl_v_pastes_multiline_clipboard_text},
	{"editor_process_keypress_ctrl_v_auto_indents_multiline_clipboard_text", test_editor_process_keypress_ctrl_v_auto_indents_multiline_clipboard_text},
	{"editor_process_keypress_ctrl_v_empty_clipboard_is_noop", test_editor_process_keypress_ctrl_v_empty_clipboard_is_noop},
	{"editor_clipboard_sync_osc52_plain_sequence", test_editor_clipboard_sync_osc52_plain_sequence},
	{"editor_clipboard_sync_osc52_tmux_wrapped_sequence", test_editor_clipboard_sync_osc52_tmux_wrapped_sequence},
	{"editor_clipboard_sync_osc52_screen_wrapped_sequence", test_editor_clipboard_sync_osc52_screen_wrapped_sequence},
	{"editor_clipboard_sync_osc52_mode_off_emits_nothing", test_editor_clipboard_sync_osc52_mode_off_emits_nothing},
	{"editor_clipboard_sync_osc52_auto_mode_skips_non_tty", test_editor_clipboard_sync_osc52_auto_mode_skips_non_tty},
	{"editor_clipboard_sync_osc52_payload_cap_skips_external_write", test_editor_clipboard_sync_osc52_payload_cap_skips_external_write},
	{"editor_process_keypress_ctrl_v_clears_selection_mode", test_editor_process_keypress_ctrl_v_clears_selection_mode},
	{"editor_process_keypress_ctrl_v_undo_roundtrip_single_step", test_editor_process_keypress_ctrl_v_undo_roundtrip_single_step},
	{"editor_process_keypress_selection_ops_noop_without_selection", test_editor_process_keypress_selection_ops_noop_without_selection},
	{"editor_process_keypress_escape_clears_selection_mode", test_editor_process_keypress_escape_clears_selection_mode},
	{"editor_process_keypress_edit_ops_clear_selection_mode", test_editor_process_keypress_edit_ops_clear_selection_mode},
	{"editor_process_keypress_ctrl_c_oom_preserves_buffer", test_editor_process_keypress_ctrl_c_oom_preserves_buffer},
	{"editor_refresh_screen_highlights_active_selection_spans", test_editor_refresh_screen_highlights_active_selection_spans},
};

const int g_input_selection_test_count =
		(int)(sizeof(g_input_selection_tests) / sizeof(g_input_selection_tests[0]));
