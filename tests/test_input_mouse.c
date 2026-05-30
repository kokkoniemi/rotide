#include "editing/selection.h"
#include "input/mouse.h"
#include "language/syntax.h"
#include "render/popup.h"
#include "rotide.h"
#include "test_case.h"
#include "test_helpers.h"
#include "test_support.h"
#include "workspace/drawer.h"
#include "workspace/file_search.h"
#include "workspace/layout.h"
#include "workspace/tabs.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int test_editor_column_select_alt_mouse_drag_starts_column_selection(void) {
	add_row("abcdef");
	add_row("ghijkl");
	add_row("mnopqr");
	E.window_rows = 4;
	E.window_cols = 30;
	E.cy = 0;
	E.cx = 0;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char press[32];
	char drag[32];
	// SGR press code for left button with Alt+Shift modifier (cb=12 = 8|4)
	int written = snprintf(press, sizeof(press), "\x1b[<12;%d;%dM", text_start + 2, 2);
	ASSERT_TRUE(written > 0 && (size_t)written < sizeof(press));
	// Drag with motion bit (cb=32+12=44)
	written = snprintf(drag, sizeof(drag), "\x1b[<44;%d;%dM", text_start + 5, 4);
	ASSERT_TRUE(written > 0 && (size_t)written < sizeof(drag));

	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_EQ_INT(1, E.column_select_active);
	ASSERT_EQ_INT(0, E.selection_mode_active);

	ASSERT_TRUE(editor_process_keypress_with_input(drag, strlen(drag)) == 0);
	struct editorColumnSelectionRect rect;
	ASSERT_TRUE(editorColumnSelectionGetRect(&rect));
	ASSERT_EQ_INT(0, rect.top_cy);
	ASSERT_EQ_INT(2, rect.bottom_cy);
	ASSERT_EQ_INT(1, rect.left_rx);
	ASSERT_EQ_INT(4, rect.right_rx);
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

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char click[32];
	ASSERT_TRUE(format_sgr_mouse_event(click, sizeof(click), 0, text_start + 4, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click, strlen(click)) == 0);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(5, E.cx);
	return 0;
}

static int test_editor_process_keypress_mouse_click_places_cursor_in_created_pane(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("0123456789");
	E.window_rows = 6;
	E.window_cols = 80;
	E.line_numbers_enabled = 0;
	E.cy = 0;
	E.cx = 0;

	struct editorPaneNode *created = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	ASSERT_TRUE(created != NULL);
	ASSERT_TRUE(E.focused_leaf == created);

	struct editorRect rect = {0};
	ASSERT_TRUE(editorLayoutFocusedLeafRect(&rect));
	int text_cols = rect.w;
	int text_start = rect.x;
	if (text_cols >= 3) {
		text_start++;
		text_cols -= 2;
	}
	ASSERT_TRUE(text_cols > 5);

	char click[32];
	ASSERT_TRUE(
	        format_sgr_mouse_event(click, sizeof(click), 0, text_start + 5, rect.y + 1, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click, strlen(click)) == 0);
	ASSERT_TRUE(E.focused_leaf == created);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(4, E.cx);
	return 0;
}

static int test_editor_process_keypress_mouse_click_maps_same_column_with_line_numbers(void) {
	add_row("0123456789");
	E.window_rows = 4;
	E.window_cols = 24;
	E.rowoff = 0;
	E.coloff = 0;
	ASSERT_TRUE(editorDrawerSetWidthForCols(1, E.window_cols));

	char click[32];
	E.line_numbers_enabled = 0;
	int text_start = editorTextBodyStartColForCols(E.window_cols);
	ASSERT_TRUE(format_sgr_mouse_event(click, sizeof(click), 0, text_start + 5, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click, strlen(click)) == 0);
	ASSERT_EQ_INT(4, E.cx);

	E.cy = 0;
	E.cx = 0;
	E.text_last_click_ms = 0;
	E.line_numbers_enabled = 1;
	text_start = editorTextBodyStartColForCols(E.window_cols);
	ASSERT_TRUE(format_sgr_mouse_event(click, sizeof(click), 0, text_start + 5, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click, strlen(click)) == 0);
	ASSERT_EQ_INT(4, E.cx);
	return 0;
}

static int test_editor_process_keypress_mouse_left_click_places_cursor_on_wrapped_segment(void) {
	add_row("abcdefghijklmn");
	E.window_rows = 4;
	E.window_cols = 10;
	E.line_wrap_enabled = 1;
	E.line_numbers_enabled = 0;
	E.rowoff = 0;
	E.wrapoff = 0;
	E.coloff = 0;
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(editorDrawerSetWidthForCols(1, E.window_cols));

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char click[32];
	ASSERT_TRUE(format_sgr_mouse_event(click, sizeof(click), 0, text_start + 4, 3, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click, strlen(click)) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(9, E.cx);
	return 0;
}

static int test_editor_process_keypress_mouse_ctrl_click_does_not_start_drag_selection(void) {
	add_row("abcdef");
	E.window_rows = 4;
	E.window_cols = 20;
	E.rowoff = 0;
	E.coloff = 0;
	E.syntax_language = EDITOR_SYNTAX_NONE;
	E.cy = 0;
	E.cx = 0;

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char click[32];
	ASSERT_TRUE(format_sgr_mouse_event(click, sizeof(click), 16, text_start + 4, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click, strlen(click)) == 0);
	ASSERT_EQ_INT(0, E.mouse_left_button_down);
	ASSERT_EQ_INT(0, E.mouse_drag_started);
	ASSERT_EQ_INT(0, E.selection_mode_active);
	ASSERT_EQ_INT(3, E.cx);
	return 0;
}

static int test_editor_process_keypress_mouse_ctrl_hover_marks_word_as_hover_link(void) {
	add_row("hello world");
	E.window_rows = 4;
	E.window_cols = 20;
	E.rowoff = 0;
	E.coloff = 0;
	E.syntax_language = EDITOR_SYNTAX_C;
	E.lsp_clangd_enabled = 1;
	(void)snprintf(E.lsp_clangd_command, sizeof(E.lsp_clangd_command), "clangd");
	E.filename = strdup("/tmp/hover.c");
	ASSERT_TRUE(E.filename != NULL);
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	E.cy = 0;
	E.cx = 0;

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char motion[32];
	// cb = motion(32) + Ctrl(16) + button=3 (no button held) = 51
	ASSERT_TRUE(format_sgr_mouse_event(motion, sizeof(motion), 51, text_start + 3, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(motion, strlen(motion)) == 0);
	ASSERT_EQ_INT(1, E.hover_link_active);
	ASSERT_EQ_INT(0, E.hover_link_row);
	ASSERT_EQ_INT(0, E.hover_link_cx_start);
	ASSERT_EQ_INT(5, E.hover_link_cx_end);
	return 0;
}

static int test_editor_process_keypress_mouse_motion_without_ctrl_does_not_mark_hover(void) {
	add_row("hello world");
	E.window_rows = 4;
	E.window_cols = 20;
	E.syntax_language = EDITOR_SYNTAX_C;
	E.lsp_clangd_enabled = 1;
	(void)snprintf(E.lsp_clangd_command, sizeof(E.lsp_clangd_command), "clangd");
	E.filename = strdup("/tmp/hover.c");
	ASSERT_TRUE(E.filename != NULL);
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char motion[32];
	// cb = motion(32) + button=3 (no button, no Ctrl) = 35
	ASSERT_TRUE(format_sgr_mouse_event(motion, sizeof(motion), 35, text_start + 3, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(motion, strlen(motion)) == 0);
	ASSERT_EQ_INT(0, E.hover_link_active);
	return 0;
}

static int test_editor_keypress_clears_hover_link(void) {
	add_row("hello world");
	E.hover_link_active = 1;
	E.hover_link_row = 0;
	E.hover_link_cx_start = 0;
	E.hover_link_cx_end = 5;

	const char letter = 'a';
	ASSERT_TRUE(editor_process_keypress_with_input(&letter, 1) == 0);
	ASSERT_EQ_INT(0, E.hover_link_active);
	ASSERT_EQ_INT(-1, E.hover_link_row);
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

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char click_filler_row[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_filler_row, sizeof(click_filler_row), 0,
	                                   text_start + 2, 4, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_filler_row,
	                                               strlen(click_filler_row)) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(1, E.cx);
	return 0;
}

static int test_editor_process_keypress_mouse_left_click_ignores_indicator_padding_columns(void) {
	add_row("0123456789");
	E.window_rows = 4;
	E.window_cols = 24;
	E.rowoff = 0;
	E.coloff = 1;
	E.cy = 0;
	E.cx = 3;
	ASSERT_TRUE(editorDrawerSetWidthForCols(1, E.window_cols));

	int text_start = editorDrawerTextStartColForCols(E.window_cols);
	int text_cols = editorDrawerTextViewportCols(E.window_cols);

	char click_left_padding[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_left_padding, sizeof(click_left_padding), 0,
	                                   text_start + 1, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_left_padding,
	                                               strlen(click_left_padding)) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(3, E.cx);

	char click_right_padding[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_right_padding, sizeof(click_right_padding), 0,
	                                   text_start + text_cols, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_right_padding,
	                                               strlen(click_right_padding)) == 0);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(3, E.cx);
	return 0;
}

static int test_editor_process_keypress_mouse_drawer_click_selects_and_toggles_directory(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char src_dir[512];
	char child_file[512];
	ASSERT_TRUE(path_join(src_dir, sizeof(src_dir), env.project_dir, "src"));
	ASSERT_TRUE(path_join(child_file, sizeof(child_file), src_dir, "child.txt"));
	ASSERT_TRUE(make_dir(src_dir));
	ASSERT_TRUE(write_text_file(child_file, "child\n"));

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows));
	int src_idx = -1;
	ASSERT_TRUE(find_drawer_entry("src", &src_idx, NULL));
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_TEXT, E.primary_focus);

	int row = src_idx - E.drawer_rowoff + 2;
	char click_src[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_src, sizeof(click_src), 0, 2, row, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_src, strlen(click_src)) == 0);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);
	ASSERT_TRUE(find_drawer_entry("child.txt", NULL, NULL));

	ASSERT_TRUE(editor_process_keypress_with_input(click_src, strlen(click_src)) == 0);
	ASSERT_EQ_INT(0, find_drawer_entry("child.txt", NULL, NULL));

	ASSERT_TRUE(unlink(child_file) == 0);
	ASSERT_TRUE(rmdir(src_dir) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_mouse_click_expands_collapsed_drawer(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));

	char click_collapse[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_collapse, sizeof(click_collapse), 0, 1, 1, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_collapse, strlen(click_collapse)) ==
	            0);
	ASSERT_TRUE(editorDrawerIsCollapsed());
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_TEXT, E.primary_focus);
	ASSERT_EQ_STR("Drawer collapsed", E.statusmsg);

	char click_toggle[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_toggle, sizeof(click_toggle), 0, 1, 1, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_toggle, strlen(click_toggle)) == 0);
	ASSERT_TRUE(!editorDrawerIsCollapsed());
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);
	ASSERT_EQ_STR("Drawer expanded", E.statusmsg);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_mouse_drawer_header_mode_buttons(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));

	E.window_rows = 6;
	E.window_cols = 60;

	char click_file_search[32];
	ASSERT_TRUE(
	        format_sgr_mouse_event(click_file_search, sizeof(click_file_search), 0, 7, 1, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_file_search,
	                                               strlen(click_file_search)) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_FILE_SEARCH, E.drawer_mode);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);

	char query_char[] = {'a'};
	ASSERT_TRUE(editor_process_keypress_with_input(query_char, sizeof(query_char)) == 0);
	ASSERT_EQ_STR("a", editorFileSearchQuery());
	ASSERT_TRUE(editor_process_keypress_with_input(click_file_search,
	                                               strlen(click_file_search)) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_FILE_SEARCH, E.drawer_mode);
	ASSERT_EQ_STR("a", editorFileSearchQuery());

	char click_project_search[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_project_search, sizeof(click_project_search), 0,
	                                   10, 1, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_project_search,
	                                               strlen(click_project_search)) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_PROJECT_SEARCH, E.drawer_mode);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);

	char click_git[32];
	char click_dap[32];
	char click_lsp[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_lsp, sizeof(click_lsp), 0, 13, 1, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_lsp, strlen(click_lsp)) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_LSP, E.drawer_mode);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);

	ASSERT_TRUE(format_sgr_mouse_event(click_dap, sizeof(click_dap), 0, 16, 1, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_dap, strlen(click_dap)) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_DAP, E.drawer_mode);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);

	ASSERT_TRUE(format_sgr_mouse_event(click_git, sizeof(click_git), 0, 19, 1, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_git, strlen(click_git)) == 0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_GIT, E.drawer_mode);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);

	char click_main_menu[32];
	ASSERT_TRUE(
	        format_sgr_mouse_event(click_main_menu, sizeof(click_main_menu), 0, 22, 1, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_main_menu, strlen(click_main_menu)) ==
	            0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_MAIN_MENU, E.drawer_mode);
	ASSERT_EQ_INT(-1, E.drawer_selected_index);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);

	char click_explorer[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_explorer, sizeof(click_explorer), 0, 4, 1, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_explorer, strlen(click_explorer)) ==
	            0);
	ASSERT_EQ_INT(EDITOR_DRAWER_MODE_TREE, E.drawer_mode);
	ASSERT_EQ_INT(-1, E.drawer_selected_index);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_mouse_collapsed_drawer_body_click_edits_text_pane(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));

	add_row("abc");
	E.window_rows = 4;
	E.window_cols = 20;
	E.line_numbers_enabled = 0;
	E.cy = 0;
	E.cx = 2;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;
	ASSERT_TRUE(editorDrawerSetCollapsed(1));

	int text_x = editorTextBodyStartColForCols(E.window_cols) + 1;
	char click_body[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_body, sizeof(click_body), 0, text_x, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_body, strlen(click_body)) == 0);
	ASSERT_TRUE(editorDrawerIsCollapsed());
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_TEXT, E.primary_focus);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(0, E.cx);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_mouse_drawer_single_file_click_opens_preview_tab(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char open_file[512];
	ASSERT_TRUE(path_join(open_file, sizeof(open_file), env.project_dir, "single.txt"));
	ASSERT_TRUE(write_text_file(open_file, "single\n"));

	ASSERT_TRUE(editorTabsInit());
	add_row("keep");
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));
	int file_idx = -1;
	ASSERT_TRUE(find_drawer_entry("single.txt", &file_idx, NULL));

	int row = file_idx - E.drawer_rowoff + 2;
	ASSERT_TRUE(row >= 2);
	char click_file[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_file, sizeof(click_file), 0, 2, row, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_file, strlen(click_file)) == 0);
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_INT(file_idx, E.drawer_selected_index);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);
	ASSERT_TRUE(editorActiveTabIsPreview());
	ASSERT_EQ_STR("Preview tab opened. Double-click to keep it open", E.statusmsg);
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR(open_file, E.filename);
	ASSERT_ROW_TEXT_EQ(0, "single");

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	ASSERT_ROW_TEXT_EQ(0, "keep");

	ASSERT_TRUE(unlink(open_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_mouse_drawer_double_click_file_pins_preview_tab(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char open_file[512];
	ASSERT_TRUE(path_join(open_file, sizeof(open_file), env.project_dir, "double.txt"));
	ASSERT_TRUE(write_text_file(open_file, "double\n"));

	ASSERT_TRUE(editorTabsInit());
	add_row("orig");
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));
	int file_idx = -1;
	ASSERT_TRUE(find_drawer_entry("double.txt", &file_idx, NULL));

	int row = file_idx - E.drawer_rowoff + 2;
	ASSERT_TRUE(row >= 2);
	char click_file[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_file, sizeof(click_file), 0, 2, row, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_file, strlen(click_file)) == 0);
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);
	ASSERT_TRUE(editorActiveTabIsPreview());
	ASSERT_EQ_STR("Preview tab opened. Double-click to keep it open", E.statusmsg);
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR(open_file, E.filename);

	ASSERT_TRUE(editor_process_keypress_with_input(click_file, strlen(click_file)) == 0);
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_TEXT, E.primary_focus);
	ASSERT_TRUE(!editorActiveTabIsPreview());
	ASSERT_EQ_STR("Tab kept open", E.statusmsg);
	ASSERT_TRUE(E.filename != NULL);
	ASSERT_EQ_STR(open_file, E.filename);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "double");

	ASSERT_TRUE(unlink(open_file) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_mouse_top_row_double_click_pins_preview_tab(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char open_file[512];
	ASSERT_TRUE(path_join(open_file, sizeof(open_file), env.project_dir, "tab-double.txt"));
	ASSERT_TRUE(write_text_file(open_file, "content\n"));

	ASSERT_TRUE(editorTabsInit());
	add_row("orig");
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));
	int file_idx = -1;
	ASSERT_TRUE(find_drawer_entry("tab-double.txt", &file_idx, NULL));

	int drawer_row = file_idx - E.drawer_rowoff + 2;
	ASSERT_TRUE(drawer_row >= 2);
	char drawer_click[32];
	ASSERT_TRUE(
	        format_sgr_mouse_event(drawer_click, sizeof(drawer_click), 0, 2, drawer_row, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(drawer_click, strlen(drawer_click)) == 0);
	ASSERT_EQ_INT(2, editorTabCount());
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_TRUE(editorActiveTabIsPreview());

	int text_start = editorDrawerTextStartColForCols(E.window_cols);
	struct editorTabLayoutEntry layout[ROTIDE_MAX_TABS];
	int layout_count = 0;
	ASSERT_TRUE(editorTabBuildLayoutForWidth(editorDrawerTextViewportCols(E.window_cols),
	                                         layout, ROTIDE_MAX_TABS, &layout_count));
	int preview_tab_col = -1;
	for (int i = 0; i < layout_count; i++) {
		if (layout[i].tab_idx == 1) {
			preview_tab_col = layout[i].start_col + 3;
			break;
		}
	}
	ASSERT_TRUE(preview_tab_col >= 0);

	char tab_click[32];
	ASSERT_TRUE(format_sgr_mouse_event(tab_click, sizeof(tab_click), 0,
	                                   text_start + preview_tab_col + 1, 1, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(tab_click, strlen(tab_click)) == 0);
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_TRUE(editorActiveTabIsPreview());

	ASSERT_TRUE(editor_process_keypress_with_input(tab_click, strlen(tab_click)) == 0);
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_TRUE(!editorActiveTabIsPreview());
	ASSERT_EQ_STR("Tab kept open", E.statusmsg);

	ASSERT_TRUE(unlink(open_file) == 0);
	cleanup_recovery_test_env(&env);
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

	E.window_cols = 80;
	int text_start = editorDrawerTextStartColForCols(E.window_cols);
	char click_first_tab[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_first_tab, sizeof(click_first_tab), 0,
	                                   text_start + 1, 1, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_first_tab, strlen(click_first_tab)) ==
	            0);
	ASSERT_EQ_INT(0, editorTabActiveIndex());
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_ROW_TEXT_EQ(0, "zero");
	ASSERT_EQ_INT(0, E.mouse_left_button_down);
	ASSERT_EQ_INT(0, E.mouse_drag_started);
	return 0;
}

static int test_editor_process_keypress_mouse_top_row_click_uses_variable_tab_layout(void) {
	ASSERT_TRUE(editorTabsInit());
	free(E.filename);
	E.filename = strdup("/tmp/aaaaaaaaaaabbbbbbbbbbbccccccccccc");
	ASSERT_TRUE(E.filename != NULL);
	add_row("zero");

	ASSERT_TRUE(editorTabNewEmpty());
	free(E.filename);
	E.filename = strdup("/tmp/one.txt");
	ASSERT_TRUE(E.filename != NULL);
	add_row("one");

	ASSERT_TRUE(editorTabNewEmpty());
	free(E.filename);
	E.filename = strdup("/tmp/two.txt");
	ASSERT_TRUE(E.filename != NULL);
	add_row("two");

	E.window_cols = 90;
	int text_cols = editorDrawerTextViewportCols(E.window_cols);
	struct editorTabLayoutEntry layout[ROTIDE_MAX_TABS];
	int layout_count = 0;
	ASSERT_TRUE(
	        editorTabBuildLayoutForWidth(text_cols, layout, ROTIDE_MAX_TABS, &layout_count));
	ASSERT_TRUE(layout_count >= 2);

	int second_tab_col = -1;
	for (int i = 0; i < layout_count; i++) {
		if (layout[i].tab_idx == 1) {
			second_tab_col = layout[i].start_col + 1;
			break;
		}
	}
	ASSERT_TRUE(second_tab_col >= 0);

	int text_start = editorDrawerTextStartColForCols(E.window_cols);
	char click_second_tab[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_second_tab, sizeof(click_second_tab), 0,
	                                   text_start + second_tab_col + 1, 1, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_second_tab,
	                                               strlen(click_second_tab)) == 0);
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_ROW_TEXT_EQ(0, "one");
	return 0;
}

static int test_editor_process_keypress_mouse_tab_bar_carets_switch_hidden_tabs(void) {
	ASSERT_TRUE(editorTabsInit());
	free(E.filename);
	E.filename = strdup("/tmp/first_tab_with_a_long_name_001.txt");
	ASSERT_TRUE(E.filename != NULL);
	add_row("zero");

	ASSERT_TRUE(editorTabNewEmpty());
	free(E.filename);
	E.filename = strdup("/tmp/second_tab_with_a_long_name_002.txt");
	ASSERT_TRUE(E.filename != NULL);
	add_row("one");

	ASSERT_TRUE(editorTabNewEmpty());
	free(E.filename);
	E.filename = strdup("/tmp/third_tab_with_a_long_name_003.txt");
	ASSERT_TRUE(E.filename != NULL);
	add_row("two");

	ASSERT_TRUE(editorTabNewEmpty());
	free(E.filename);
	E.filename = strdup("/tmp/fourth_tab_with_a_long_name_004.txt");
	ASSERT_TRUE(E.filename != NULL);
	add_row("three");

	E.window_cols = 44;
	ASSERT_TRUE(editorDrawerSetCollapsed(1));

	ASSERT_TRUE(editorTabSwitchToIndex(0));
	int tab_start = editorDrawerCollapsedToggleWidthForCols(E.window_cols);
	int tab_cols = E.window_cols - tab_start;
	ASSERT_TRUE(tab_cols > 0);
	struct editorTabLayoutEntry layout[ROTIDE_MAX_TABS];
	int layout_count = 0;
	ASSERT_TRUE(editorTabBuildLayoutForWidth(tab_cols, layout, ROTIDE_MAX_TABS, &layout_count));
	ASSERT_TRUE(layout_count > 0);
	ASSERT_TRUE(layout[layout_count - 1].show_right_overflow);
	int right_caret_col =
	        layout[layout_count - 1].start_col + layout[layout_count - 1].width_cols - 1;
	int right_target = layout[layout_count - 1].tab_idx + 1;
	ASSERT_TRUE(right_target < editorTabCount());

	char click_right_caret[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_right_caret, sizeof(click_right_caret), 0,
	                                   tab_start + right_caret_col + 1, 1, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_right_caret,
	                                               strlen(click_right_caret)) == 0);
	ASSERT_EQ_INT(right_target, editorTabActiveIndex());
	ASSERT_ROW_TEXT_EQ(0, "one");

	ASSERT_TRUE(editorTabSwitchToIndex(editorTabCount() - 1));
	ASSERT_TRUE(editorTabBuildLayoutForWidth(tab_cols, layout, ROTIDE_MAX_TABS, &layout_count));
	ASSERT_TRUE(layout_count > 0);
	ASSERT_TRUE(layout[0].show_left_overflow);
	int left_caret_col = layout[0].start_col;
	int left_target = layout[0].tab_idx - 1;
	ASSERT_TRUE(left_target >= 0);

	char click_left_caret[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_left_caret, sizeof(click_left_caret), 0,
	                                   tab_start + left_caret_col + 1, 1, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_left_caret,
	                                               strlen(click_left_caret)) == 0);
	ASSERT_EQ_INT(left_target, editorTabActiveIndex());
	ASSERT_ROW_TEXT_EQ(0, "two");
	return 0;
}

static int setup_four_tab_split(enum editorSplitOrientation orientation,
                                struct editorPaneNode **first_out,
                                struct editorPaneNode **second_out) {
	ASSERT_TRUE(editorTabsInit());
	add_row("zero");
	ASSERT_TRUE(editorTabNewEmpty());
	add_row("one");
	ASSERT_TRUE(editorTabNewEmpty());
	add_row("two");
	ASSERT_TRUE(editorTabNewEmpty());
	add_row("three");
	ASSERT_TRUE(editorTabSwitchToIndex(0));

	E.window_rows = 10;
	E.window_cols = 80;
	E.line_numbers_enabled = 0;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;

	struct editorPaneNode *first = E.focused_leaf;
	struct editorPaneNode *second = editorLayoutSplitFocused(orientation, 0.5);
	ASSERT_TRUE(first != NULL);
	ASSERT_TRUE(second != NULL);

	first->as.leaf.view.pane_tab_count = 0;
	ASSERT_TRUE(editorPaneViewAddTab(&first->as.leaf.view, 0));
	ASSERT_TRUE(editorPaneViewAddTab(&first->as.leaf.view, 1));
	first->as.leaf.view.active_tab_idx = 0;
	second->as.leaf.view.pane_tab_count = 0;
	ASSERT_TRUE(editorPaneViewAddTab(&second->as.leaf.view, 2));
	ASSERT_TRUE(editorPaneViewAddTab(&second->as.leaf.view, 3));
	second->as.leaf.view.active_tab_idx = 2;
	ASSERT_TRUE(editorLayoutSetFocusedLeaf(first));
	editorResetTabClickTracking();

	*first_out = first;
	*second_out = second;
	return 0;
}

static int pane_tab_sgr_point(struct editorPaneNode *leaf, int tab_idx, int *x_out, int *y_out) {
	struct editorRect viewport = {0};
	struct editorRect rect = {0};
	ASSERT_TRUE(editorLayoutEditorViewport(&viewport));
	ASSERT_TRUE(editorLayoutLeafRectBordered(E.layout_root, viewport, ROTIDE_PANE_BORDER_SIZE,
	                                         leaf, &rect));

	struct editorTabLayoutEntry layout[ROTIDE_MAX_TABS];
	int layout_count = 0;
	ASSERT_TRUE(editorTabBuildLayoutForPane(&leaf->as.leaf.view, rect.w, layout,
	                                        ROTIDE_MAX_TABS, &layout_count));
	for (int i = 0; i < layout_count; i++) {
		if (layout[i].tab_idx != tab_idx) {
			continue;
		}
		int local_col = layout[i].start_col;
		if (layout[i].width_cols > 2) {
			local_col++;
		}
		*x_out = rect.x + local_col + 1;
		*y_out = rect.y;
		return 0;
	}
	ASSERT_TRUE(0);
	return 1;
}

static int send_mouse_drag_sequence(int press_x, int press_y, int drag_x, int drag_y, int release_x,
                                    int release_y) {
	char press[32];
	char drag[32];
	char release[32];
	ASSERT_TRUE(format_sgr_mouse_event(press, sizeof(press), 0, press_x, press_y, 'M'));
	ASSERT_TRUE(format_sgr_mouse_event(drag, sizeof(drag), 32, drag_x, drag_y, 'M'));
	ASSERT_TRUE(format_sgr_mouse_event(release, sizeof(release), 0, release_x, release_y, 'm'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_TRUE(editor_process_keypress_with_input(drag, strlen(drag)) == 0);
	ASSERT_TRUE(editor_process_keypress_with_input(release, strlen(release)) == 0);
	return 0;
}

static int test_editor_process_keypress_mouse_pane_strip_click_activates_pane_tab(void) {
	struct editorPaneNode *top = NULL;
	struct editorPaneNode *bottom = NULL;
	ASSERT_TRUE(setup_four_tab_split(EDITOR_SPLIT_HORIZONTAL, &top, &bottom) == 0);

	int click_x = 0;
	int click_y = 0;
	ASSERT_TRUE(pane_tab_sgr_point(top, 1, &click_x, &click_y) == 0);
	char click_top[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_top, sizeof(click_top), 0, click_x, click_y, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_top, strlen(click_top)) == 0);
	ASSERT_TRUE(E.focused_leaf == top);
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_INT(1, top->as.leaf.view.active_tab_idx);

	ASSERT_TRUE(pane_tab_sgr_point(bottom, 3, &click_x, &click_y) == 0);
	char click_bottom[32];
	ASSERT_TRUE(format_sgr_mouse_event(click_bottom, sizeof(click_bottom), 0, click_x, click_y,
	                                   'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click_bottom, strlen(click_bottom)) == 0);
	ASSERT_TRUE(E.focused_leaf == bottom);
	ASSERT_EQ_INT(3, editorTabActiveIndex());
	ASSERT_EQ_INT(3, bottom->as.leaf.view.active_tab_idx);
	ASSERT_EQ_INT(1, top->as.leaf.view.active_tab_idx);
	ASSERT_EQ_INT(0, E.split_resize_active);
	return 0;
}

static int test_editor_process_keypress_mouse_strip_blank_border_arms_resize(void) {
	struct editorPaneNode *top = NULL;
	struct editorPaneNode *bottom = NULL;
	ASSERT_TRUE(setup_four_tab_split(EDITOR_SPLIT_HORIZONTAL, &top, &bottom) == 0);

	struct editorRect viewport = {0};
	struct editorRect bottom_rect = {0};
	ASSERT_TRUE(editorLayoutEditorViewport(&viewport));
	ASSERT_TRUE(editorLayoutLeafRectBordered(E.layout_root, viewport, ROTIDE_PANE_BORDER_SIZE,
	                                         bottom, &bottom_rect));
	int local_col = bottom_rect.w - 1;
	ASSERT_EQ_INT(
	        -1, editorTabHitTestColumnForPane(&bottom->as.leaf.view, local_col, bottom_rect.w));
	struct editorPaneNode *parent_split = editorPaneTreeFindParent(E.layout_root, bottom);
	ASSERT_TRUE(parent_split != NULL);

	char press[32];
	ASSERT_TRUE(format_sgr_mouse_event(press, sizeof(press), 0, bottom_rect.x + local_col + 1,
	                                   bottom_rect.y, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_EQ_INT(1, E.split_resize_active);
	ASSERT_TRUE(E.split_resize_node == parent_split);
	ASSERT_TRUE(E.focused_leaf == top);
	return 0;
}

static int test_editor_process_keypress_mouse_top_strip_vborder_arms_resize(void) {
	struct editorPaneNode *left = NULL;
	struct editorPaneNode *right = NULL;
	ASSERT_TRUE(setup_four_tab_split(EDITOR_SPLIT_VERTICAL, &left, &right) == 0);

	struct editorRect viewport = {0};
	ASSERT_TRUE(editorLayoutEditorViewport(&viewport));
	int border_x = viewport.x + (int)((double)(viewport.w - ROTIDE_PANE_BORDER_SIZE) * 0.5);
	struct editorPaneNode *parent_split = editorPaneTreeFindParent(E.layout_root, right);
	ASSERT_TRUE(parent_split != NULL);

	char press[32];
	ASSERT_TRUE(format_sgr_mouse_event(press, sizeof(press), 0, border_x + 1, 1, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_EQ_INT(1, E.split_resize_active);
	ASSERT_TRUE(E.split_resize_node == parent_split);
	ASSERT_TRUE(E.focused_leaf == left);
	return 0;
}

static int test_editor_process_keypress_mouse_pane_strip_double_click_pins_preview_tab(void) {
	struct editorPaneNode *top = NULL;
	struct editorPaneNode *bottom = NULL;
	ASSERT_TRUE(setup_four_tab_split(EDITOR_SPLIT_HORIZONTAL, &top, &bottom) == 0);
	bottom->as.leaf.view.active_tab_idx = 3;
	E.tabs[3].is_preview = 1;
	editorResetTabClickTracking();

	int click_x = 0;
	int click_y = 0;
	ASSERT_TRUE(pane_tab_sgr_point(bottom, 3, &click_x, &click_y) == 0);
	char click[32];
	ASSERT_TRUE(format_sgr_mouse_event(click, sizeof(click), 0, click_x, click_y, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click, strlen(click)) == 0);
	ASSERT_TRUE(E.focused_leaf == bottom);
	ASSERT_EQ_INT(3, editorTabActiveIndex());
	ASSERT_TRUE(editorActiveTabIsPreview());

	ASSERT_TRUE(editor_process_keypress_with_input(click, strlen(click)) == 0);
	ASSERT_EQ_INT(3, editorTabActiveIndex());
	ASSERT_TRUE(!editorActiveTabIsPreview());
	ASSERT_EQ_STR("Tab kept open", E.statusmsg);
	return 0;
}

static int test_editor_process_keypress_mouse_tab_drag_reorders_within_pane(void) {
	struct editorPaneNode *top = NULL;
	struct editorPaneNode *bottom = NULL;
	ASSERT_TRUE(setup_four_tab_split(EDITOR_SPLIT_HORIZONTAL, &top, &bottom) == 0);
	ASSERT_TRUE(editorPaneViewAddTab(&top->as.leaf.view, 2));
	editorPaneViewRemoveTab(&bottom->as.leaf.view, 2);
	bottom->as.leaf.view.active_tab_idx = 3;

	int source_x = 0;
	int source_y = 0;
	int target_x = 0;
	int target_y = 0;
	ASSERT_TRUE(pane_tab_sgr_point(top, 0, &source_x, &source_y) == 0);
	ASSERT_TRUE(pane_tab_sgr_point(top, 2, &target_x, &target_y) == 0);
	ASSERT_TRUE(send_mouse_drag_sequence(source_x, source_y, target_x, target_y, target_x,
	                                     target_y) == 0);

	ASSERT_TRUE(E.focused_leaf == top);
	ASSERT_EQ_INT(3, top->as.leaf.view.pane_tab_count);
	ASSERT_EQ_INT(1, top->as.leaf.view.pane_tabs[0]);
	ASSERT_EQ_INT(2, top->as.leaf.view.pane_tabs[1]);
	ASSERT_EQ_INT(0, top->as.leaf.view.pane_tabs[2]);
	ASSERT_EQ_INT(0, top->as.leaf.view.active_tab_idx);
	ASSERT_EQ_INT(0, E.tab_drag_armed);
	ASSERT_EQ_INT(0, E.tab_drag_active);
	return 0;
}

static int test_editor_process_keypress_mouse_tab_drag_moves_across_panes(void) {
	struct editorPaneNode *top = NULL;
	struct editorPaneNode *bottom = NULL;
	ASSERT_TRUE(setup_four_tab_split(EDITOR_SPLIT_HORIZONTAL, &top, &bottom) == 0);

	int source_x = 0;
	int source_y = 0;
	int target_x = 0;
	int target_y = 0;
	ASSERT_TRUE(pane_tab_sgr_point(top, 1, &source_x, &source_y) == 0);
	ASSERT_TRUE(pane_tab_sgr_point(bottom, 3, &target_x, &target_y) == 0);
	ASSERT_TRUE(send_mouse_drag_sequence(source_x, source_y, target_x, target_y, target_x,
	                                     target_y) == 0);

	ASSERT_TRUE(E.focused_leaf == bottom);
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_INT(1, bottom->as.leaf.view.active_tab_idx);
	ASSERT_EQ_INT(0, top->as.leaf.view.active_tab_idx);
	ASSERT_EQ_INT(1, top->as.leaf.view.pane_tab_count);
	ASSERT_EQ_INT(0, top->as.leaf.view.pane_tabs[0]);
	ASSERT_EQ_INT(3, bottom->as.leaf.view.pane_tab_count);
	ASSERT_EQ_INT(2, bottom->as.leaf.view.pane_tabs[0]);
	ASSERT_EQ_INT(1, bottom->as.leaf.view.pane_tabs[1]);
	ASSERT_EQ_INT(3, bottom->as.leaf.view.pane_tabs[2]);
	ASSERT_EQ_INT(0, E.tab_drag_armed);
	ASSERT_EQ_INT(0, E.tab_drag_active);
	return 0;
}

static int test_editor_process_keypress_mouse_tab_drag_last_tab_repopulates_source_pane(void) {
	struct editorPaneNode *top = NULL;
	struct editorPaneNode *bottom = NULL;
	ASSERT_TRUE(setup_four_tab_split(EDITOR_SPLIT_HORIZONTAL, &top, &bottom) == 0);
	editorPaneViewRemoveTab(&top->as.leaf.view, 1);
	top->as.leaf.view.active_tab_idx = 0;
	ASSERT_EQ_INT(1, top->as.leaf.view.pane_tab_count);
	ASSERT_TRUE(editorLayoutSetFocusedLeaf(top));
	ASSERT_TRUE(editorTabSwitchToIndex(0));
	int tab_count_before = editorTabCount();

	int source_x = 0;
	int source_y = 0;
	int target_x = 0;
	int target_y = 0;
	ASSERT_TRUE(pane_tab_sgr_point(top, 0, &source_x, &source_y) == 0);
	ASSERT_TRUE(pane_tab_sgr_point(bottom, 3, &target_x, &target_y) == 0);
	ASSERT_TRUE(send_mouse_drag_sequence(source_x, source_y, target_x, target_y, target_x,
	                                     target_y) == 0);

	ASSERT_TRUE(E.focused_leaf == bottom);
	ASSERT_EQ_INT(tab_count_before + 1, editorTabCount());
	ASSERT_EQ_INT(1, top->as.leaf.view.pane_tab_count);
	ASSERT_TRUE(top->as.leaf.view.active_tab_idx >= 0);
	ASSERT_TRUE(top->as.leaf.view.active_tab_idx != 0);
	ASSERT_EQ_INT(top->as.leaf.view.active_tab_idx, top->as.leaf.view.pane_tabs[0]);
	ASSERT_EQ_INT(3, bottom->as.leaf.view.pane_tab_count);
	ASSERT_EQ_INT(0, bottom->as.leaf.view.active_tab_idx);
	ASSERT_EQ_INT(0, E.tab_drag_armed);
	ASSERT_EQ_INT(0, E.tab_drag_active);
	return 0;
}

static int test_editor_process_keypress_mouse_tab_drag_release_off_strip_cancels(void) {
	struct editorPaneNode *top = NULL;
	struct editorPaneNode *bottom = NULL;
	ASSERT_TRUE(setup_four_tab_split(EDITOR_SPLIT_HORIZONTAL, &top, &bottom) == 0);

	int source_x = 0;
	int source_y = 0;
	ASSERT_TRUE(pane_tab_sgr_point(top, 1, &source_x, &source_y) == 0);
	struct editorRect viewport = {0};
	struct editorRect top_rect = {0};
	ASSERT_TRUE(editorLayoutEditorViewport(&viewport));
	ASSERT_TRUE(editorLayoutLeafRectBordered(E.layout_root, viewport, ROTIDE_PANE_BORDER_SIZE,
	                                         top, &top_rect));
	int body_x = top_rect.x + 1;
	int body_y = top_rect.y + 1;
	ASSERT_TRUE(send_mouse_drag_sequence(source_x, source_y, body_x + 4, body_y, body_x + 4,
	                                     body_y) == 0);

	ASSERT_TRUE(E.focused_leaf == top);
	ASSERT_EQ_INT(2, top->as.leaf.view.pane_tab_count);
	ASSERT_EQ_INT(0, top->as.leaf.view.pane_tabs[0]);
	ASSERT_EQ_INT(1, top->as.leaf.view.pane_tabs[1]);
	ASSERT_EQ_INT(2, bottom->as.leaf.view.pane_tab_count);
	ASSERT_EQ_INT(2, bottom->as.leaf.view.pane_tabs[0]);
	ASSERT_EQ_INT(3, bottom->as.leaf.view.pane_tabs[1]);
	ASSERT_EQ_INT(0, E.tab_drag_armed);
	ASSERT_EQ_INT(0, E.tab_drag_active);
	return 0;
}

static int test_editor_process_keypress_mouse_tab_click_without_drag_clears_drag(void) {
	struct editorPaneNode *top = NULL;
	struct editorPaneNode *bottom = NULL;
	ASSERT_TRUE(setup_four_tab_split(EDITOR_SPLIT_HORIZONTAL, &top, &bottom) == 0);

	int click_x = 0;
	int click_y = 0;
	ASSERT_TRUE(pane_tab_sgr_point(top, 1, &click_x, &click_y) == 0);
	char press[32];
	char release[32];
	ASSERT_TRUE(format_sgr_mouse_event(press, sizeof(press), 0, click_x, click_y, 'M'));
	ASSERT_TRUE(format_sgr_mouse_event(release, sizeof(release), 0, click_x, click_y, 'm'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_EQ_INT(1, E.tab_drag_armed);
	ASSERT_EQ_INT(0, E.tab_drag_active);
	ASSERT_TRUE(editor_process_keypress_with_input(release, strlen(release)) == 0);

	ASSERT_TRUE(E.focused_leaf == top);
	ASSERT_EQ_INT(1, editorTabActiveIndex());
	ASSERT_EQ_INT(2, top->as.leaf.view.pane_tab_count);
	ASSERT_EQ_INT(0, top->as.leaf.view.pane_tabs[0]);
	ASSERT_EQ_INT(1, top->as.leaf.view.pane_tabs[1]);
	ASSERT_EQ_INT(0, E.tab_drag_armed);
	ASSERT_EQ_INT(0, E.tab_drag_active);
	return 0;
}

static int test_editor_process_keypress_mouse_press_on_pane_border_arms_split_resize(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("abcdef");
	E.window_rows = 8;
	E.window_cols = 80;
	E.line_numbers_enabled = 0;
	E.cy = 0;
	E.cx = 3;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;

	struct editorPaneNode *sibling = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	ASSERT_TRUE(sibling != NULL);

	struct editorRect viewport = {0};
	ASSERT_TRUE(editorLayoutEditorViewport(&viewport));
	/* Vertical split, ratio 0.5, border_size=1 → border lives at
	 * viewport.x + (viewport.w - 1) * 0.5 (truncated). */
	int border_x = viewport.x + (int)((double)(viewport.w - ROTIDE_PANE_BORDER_SIZE) * 0.5);
	struct editorPaneNode *parent_split = editorPaneTreeFindParent(E.layout_root, sibling);
	ASSERT_TRUE(parent_split != NULL);
	int cx_before = E.cx;
	int cy_before = E.cy;
	struct editorPaneNode *focused_before = E.focused_leaf;

	char press[32];
	ASSERT_TRUE(
	        format_sgr_mouse_event(press, sizeof(press), 0, border_x + 1, viewport.y + 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_EQ_INT(1, E.split_resize_active);
	ASSERT_TRUE(E.split_resize_node == parent_split);
	ASSERT_EQ_INT(0, E.mouse_left_button_down);
	ASSERT_EQ_INT(0, E.mouse_drag_started);
	ASSERT_EQ_INT(cx_before, E.cx);
	ASSERT_EQ_INT(cy_before, E.cy);
	ASSERT_TRUE(E.focused_leaf == focused_before);
	return 0;
}

static int test_editor_process_keypress_mouse_press_off_border_acts_as_text_click(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("abcdefghij");
	E.window_rows = 8;
	E.window_cols = 80;
	E.line_numbers_enabled = 0;
	E.cy = 0;
	E.cx = 0;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;

	struct editorPaneNode *sibling = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	ASSERT_TRUE(sibling != NULL);

	struct editorRect viewport = {0};
	ASSERT_TRUE(editorLayoutEditorViewport(&viewport));
	int border_x = viewport.x + (int)((double)(viewport.w - ROTIDE_PANE_BORDER_SIZE) * 0.5);
	/* Click in the left pane, two columns left of the border — never on it. */
	int text_x = border_x - 2;

	char press[32];
	ASSERT_TRUE(
	        format_sgr_mouse_event(press, sizeof(press), 0, text_x + 1, viewport.y + 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_EQ_INT(0, E.split_resize_active);
	ASSERT_TRUE(E.split_resize_node == NULL);
	return 0;
}

static int test_editor_process_keypress_mouse_press_single_pane_never_arms_split(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("abcdef");
	E.window_rows = 8;
	E.window_cols = 80;
	E.line_numbers_enabled = 0;
	E.cy = 0;
	E.cx = 0;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;

	/* No split → no borders, no possible arm. */
	struct editorRect viewport = {0};
	ASSERT_TRUE(editorLayoutEditorViewport(&viewport));
	int mid_x = viewport.x + viewport.w / 2;

	char press[32];
	ASSERT_TRUE(
	        format_sgr_mouse_event(press, sizeof(press), 0, mid_x + 1, viewport.y + 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_EQ_INT(0, E.split_resize_active);
	ASSERT_TRUE(E.split_resize_node == NULL);
	return 0;
}

static int drag_vertical_split_helper(int drag_x, double *ratio_out) {
	ASSERT_TRUE(editorTabsInit());
	add_row("abcdef");
	E.window_rows = 8;
	E.window_cols = 80;
	E.line_numbers_enabled = 0;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;

	struct editorPaneNode *sibling = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	ASSERT_TRUE(sibling != NULL);

	struct editorRect viewport = {0};
	ASSERT_TRUE(editorLayoutEditorViewport(&viewport));
	int border_x = viewport.x + (int)((double)(viewport.w - ROTIDE_PANE_BORDER_SIZE) * 0.5);

	char press[32];
	ASSERT_TRUE(
	        format_sgr_mouse_event(press, sizeof(press), 0, border_x + 1, viewport.y + 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_EQ_INT(1, E.split_resize_active);

	char drag[32];
	ASSERT_TRUE(
	        format_sgr_mouse_event(drag, sizeof(drag), 32, drag_x + 1, viewport.y + 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(drag, strlen(drag)) == 0);
	*ratio_out = E.split_resize_node->as.split.ratio;
	return 0;
}

static int test_editor_process_keypress_mouse_drag_vertical_split_decreases_ratio(void) {
	double ratio = 0.0;
	struct editorRect viewport = {0};
	ASSERT_TRUE(editorTabsInit());
	add_row("a");
	E.window_rows = 8;
	E.window_cols = 80;
	E.line_numbers_enabled = 0;
	ASSERT_TRUE(editorLayoutEditorViewport(&viewport));
	int drag_x = viewport.x + (int)((double)(viewport.w - ROTIDE_PANE_BORDER_SIZE) * 0.25);
	ASSERT_TRUE(drag_vertical_split_helper(drag_x, &ratio) == 0);
	ASSERT_TRUE(ratio < 0.5 - 1e-9);
	ASSERT_TRUE(ratio > 0.20);
	return 0;
}

static int test_editor_process_keypress_mouse_drag_vertical_split_increases_ratio(void) {
	double ratio = 0.0;
	struct editorRect viewport = {0};
	ASSERT_TRUE(editorTabsInit());
	add_row("a");
	E.window_rows = 8;
	E.window_cols = 80;
	E.line_numbers_enabled = 0;
	ASSERT_TRUE(editorLayoutEditorViewport(&viewport));
	int drag_x = viewport.x + (int)((double)(viewport.w - ROTIDE_PANE_BORDER_SIZE) * 0.75);
	ASSERT_TRUE(drag_vertical_split_helper(drag_x, &ratio) == 0);
	ASSERT_TRUE(ratio > 0.5 + 1e-9);
	ASSERT_TRUE(ratio < 0.80);
	return 0;
}

static int test_editor_process_keypress_mouse_drag_horizontal_split_updates_ratio(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("a");
	E.window_rows = 20;
	E.window_cols = 80;
	E.line_numbers_enabled = 0;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;

	struct editorPaneNode *sibling = editorLayoutSplitFocused(EDITOR_SPLIT_HORIZONTAL, 0.5);
	ASSERT_TRUE(sibling != NULL);

	struct editorRect viewport = {0};
	ASSERT_TRUE(editorLayoutEditorViewport(&viewport));
	int border_y = viewport.y + (int)((double)(viewport.h - ROTIDE_PANE_BORDER_SIZE) * 0.5);
	int mid_x = viewport.x + viewport.w / 2;

	char press[32];
	ASSERT_TRUE(format_sgr_mouse_event(press, sizeof(press), 0, mid_x + 1, border_y + 1, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_EQ_INT(1, E.split_resize_active);

	/* Drag down: ratio increases. */
	int drag_y = viewport.y + (int)((double)(viewport.h - ROTIDE_PANE_BORDER_SIZE) * 0.75);
	char drag[32];
	ASSERT_TRUE(format_sgr_mouse_event(drag, sizeof(drag), 32, mid_x + 1, drag_y + 1, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(drag, strlen(drag)) == 0);
	double down_ratio = E.split_resize_node->as.split.ratio;
	ASSERT_TRUE(down_ratio > 0.5 + 1e-9);

	/* Drag up: ratio decreases. */
	int up_drag_y = viewport.y + (int)((double)(viewport.h - ROTIDE_PANE_BORDER_SIZE) * 0.25);
	ASSERT_TRUE(format_sgr_mouse_event(drag, sizeof(drag), 32, mid_x + 1, up_drag_y + 1, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(drag, strlen(drag)) == 0);
	ASSERT_TRUE(E.split_resize_node->as.split.ratio < down_ratio);
	return 0;
}

static int test_editor_process_keypress_mouse_drag_clamps_to_min_and_max(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("a");
	E.window_rows = 8;
	E.window_cols = 80;
	E.line_numbers_enabled = 0;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;

	struct editorPaneNode *sibling = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	ASSERT_TRUE(sibling != NULL);

	struct editorRect viewport = {0};
	ASSERT_TRUE(editorLayoutEditorViewport(&viewport));
	int border_x = viewport.x + (int)((double)(viewport.w - ROTIDE_PANE_BORDER_SIZE) * 0.5);

	char press[32];
	ASSERT_TRUE(
	        format_sgr_mouse_event(press, sizeof(press), 0, border_x + 1, viewport.y + 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);

	char drag[32];
	/* Drag well off the left edge. */
	ASSERT_TRUE(format_sgr_mouse_event(drag, sizeof(drag), 32, 1, viewport.y + 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(drag, strlen(drag)) == 0);
	double low = E.split_resize_node->as.split.ratio;
	ASSERT_TRUE(low > ROTIDE_PANE_MIN_RATIO - 1e-9);
	ASSERT_TRUE(low < ROTIDE_PANE_MIN_RATIO + 1e-9);

	/* Drag well off the right edge. */
	ASSERT_TRUE(format_sgr_mouse_event(drag, sizeof(drag), 32, E.window_cols + 50,
	                                   viewport.y + 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(drag, strlen(drag)) == 0);
	double high = E.split_resize_node->as.split.ratio;
	double max_ratio = 1.0 - ROTIDE_PANE_MIN_RATIO;
	ASSERT_TRUE(high > max_ratio - 1e-9);
	ASSERT_TRUE(high < max_ratio + 1e-9);
	return 0;
}

static int test_editor_process_keypress_mouse_drag_inner_split_only_updates_inner(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("a");
	E.window_rows = 20;
	E.window_cols = 80;
	E.line_numbers_enabled = 0;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;

	/* Outer vertical: focus moves to right (sibling). Then split the right
	 * with a horizontal split → focus is the new inner_bottom. */
	struct editorPaneNode *right = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	ASSERT_TRUE(right != NULL);
	struct editorPaneNode *inner_bottom =
	        editorLayoutSplitFocused(EDITOR_SPLIT_HORIZONTAL, 0.5);
	ASSERT_TRUE(inner_bottom != NULL);

	struct editorPaneNode *inner_split = editorPaneTreeFindParent(E.layout_root, inner_bottom);
	struct editorPaneNode *outer_split = editorPaneTreeFindParent(E.layout_root, inner_split);
	ASSERT_TRUE(inner_split != NULL && outer_split != NULL);
	double outer_ratio_before = outer_split->as.split.ratio;

	struct editorRect viewport = {0};
	ASSERT_TRUE(editorLayoutEditorViewport(&viewport));
	struct editorRect inner_rect = {0};
	ASSERT_TRUE(editorLayoutSplitNodeRect(E.layout_root, viewport, ROTIDE_PANE_BORDER_SIZE,
	                                      inner_split, &inner_rect));
	int border_y = inner_rect.y + (int)((double)(inner_rect.h - ROTIDE_PANE_BORDER_SIZE) * 0.5);
	int mid_x = inner_rect.x + inner_rect.w - 1;

	char press[32];
	ASSERT_TRUE(format_sgr_mouse_event(press, sizeof(press), 0, mid_x + 1, border_y + 1, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_TRUE(E.split_resize_node == inner_split);

	int drag_y = inner_rect.y + (int)((double)(inner_rect.h - ROTIDE_PANE_BORDER_SIZE) * 0.75);
	char drag[32];
	ASSERT_TRUE(format_sgr_mouse_event(drag, sizeof(drag), 32, mid_x + 1, drag_y + 1, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(drag, strlen(drag)) == 0);
	ASSERT_TRUE(inner_split->as.split.ratio > 0.5 + 1e-9);
	ASSERT_TRUE(outer_split->as.split.ratio == outer_ratio_before);
	return 0;
}

static int test_editor_process_keypress_mouse_drag_does_not_move_cursor_during_resize(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("abcdefghij");
	add_row("klmnopqrst");
	E.window_rows = 8;
	E.window_cols = 80;
	E.line_numbers_enabled = 0;
	E.cy = 1;
	E.cx = 4;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;

	struct editorPaneNode *sibling = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	ASSERT_TRUE(sibling != NULL);
	int cx_before = E.cx;
	int cy_before = E.cy;

	struct editorRect viewport = {0};
	ASSERT_TRUE(editorLayoutEditorViewport(&viewport));
	int border_x = viewport.x + (int)((double)(viewport.w - ROTIDE_PANE_BORDER_SIZE) * 0.5);

	char press[32];
	ASSERT_TRUE(
	        format_sgr_mouse_event(press, sizeof(press), 0, border_x + 1, viewport.y + 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);

	int drag_x = viewport.x + (int)((double)(viewport.w - ROTIDE_PANE_BORDER_SIZE) * 0.7);
	char drag[32];
	ASSERT_TRUE(
	        format_sgr_mouse_event(drag, sizeof(drag), 32, drag_x + 1, viewport.y + 4, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(drag, strlen(drag)) == 0);
	ASSERT_EQ_INT(cx_before, E.cx);
	ASSERT_EQ_INT(cy_before, E.cy);
	ASSERT_EQ_INT(0, E.selection_mode_active);
	return 0;
}

static int test_editor_process_keypress_mouse_release_clears_split_resize_state(void) {
	ASSERT_TRUE(editorTabsInit());
	add_row("a");
	E.window_rows = 8;
	E.window_cols = 80;
	E.line_numbers_enabled = 0;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;

	struct editorPaneNode *sibling = editorLayoutSplitFocused(EDITOR_SPLIT_VERTICAL, 0.5);
	ASSERT_TRUE(sibling != NULL);

	struct editorRect viewport = {0};
	ASSERT_TRUE(editorLayoutEditorViewport(&viewport));
	int border_x = viewport.x + (int)((double)(viewport.w - ROTIDE_PANE_BORDER_SIZE) * 0.5);

	char press[32];
	ASSERT_TRUE(
	        format_sgr_mouse_event(press, sizeof(press), 0, border_x + 1, viewport.y + 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_EQ_INT(1, E.split_resize_active);

	char release[32];
	ASSERT_TRUE(format_sgr_mouse_event(release, sizeof(release), 0, border_x + 1,
	                                   viewport.y + 2, 'm'));
	ASSERT_TRUE(editor_process_keypress_with_input(release, strlen(release)) == 0);
	ASSERT_EQ_INT(0, E.split_resize_active);
	ASSERT_TRUE(E.split_resize_node == NULL);
	return 0;
}

static int test_editor_process_keypress_mouse_drag_on_splitter_resizes_drawer(void) {
	add_row("abcdef");
	E.window_rows = 4;
	E.window_cols = 40;
	E.drawer_width_cols = 12;
	E.cy = 0;
	E.cx = 3;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;

	int separator_x = editorDrawerWidthForCols(E.window_cols) + 1;
	char press[32];
	ASSERT_TRUE(format_sgr_mouse_event(press, sizeof(press), 0, separator_x, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_EQ_INT(1, E.drawer_resize_active);
	ASSERT_EQ_INT(0, E.mouse_left_button_down);
	ASSERT_EQ_INT(0, E.mouse_drag_started);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(3, E.cx);

	char drag_smaller[32];
	ASSERT_TRUE(format_sgr_mouse_event(drag_smaller, sizeof(drag_smaller), 32, 6, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(drag_smaller, strlen(drag_smaller)) == 0);
	ASSERT_EQ_INT(5, editorDrawerWidthForCols(E.window_cols));
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(3, E.cx);

	char drag_larger[32];
	ASSERT_TRUE(format_sgr_mouse_event(drag_larger, sizeof(drag_larger), 32, 200, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(drag_larger, strlen(drag_larger)) == 0);
	ASSERT_EQ_INT(38, editorDrawerWidthForCols(E.window_cols));

	char release[32];
	ASSERT_TRUE(format_sgr_mouse_event(release, sizeof(release), 0, 200, 2, 'm'));
	ASSERT_TRUE(editor_process_keypress_with_input(release, strlen(release)) == 0);
	ASSERT_EQ_INT(0, E.drawer_resize_active);
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
	E.cy = 4;
	E.cx = 0;
	E.rowoff = 0;

	int text_x = editorTextBodyStartColForCols(E.window_cols) + 1;
	char wheel_down[32];
	ASSERT_TRUE(format_sgr_mouse_event(wheel_down, sizeof(wheel_down), 65, text_x, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_down, strlen(wheel_down)) == 0);
	ASSERT_EQ_INT(4, E.cy);
	ASSERT_EQ_INT(3, E.rowoff);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FREE_SCROLL, E.viewport_mode);

	E.rowoff = 8;
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_down, strlen(wheel_down)) == 0);
	ASSERT_EQ_INT(9, E.rowoff);
	ASSERT_EQ_INT(4, E.cy);

	char wheel_up[32];
	ASSERT_TRUE(format_sgr_mouse_event(wheel_up, sizeof(wheel_up), 64, text_x, 2, 'M'));
	E.rowoff = 1;
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_up, strlen(wheel_up)) == 0);
	ASSERT_EQ_INT(0, E.rowoff);
	ASSERT_EQ_INT(4, E.cy);
	return 0;
}

static int test_editor_process_keypress_mouse_wheel_scrolls_wrapped_segments(void) {
	add_row("abcdefghijklmnopqrst");
	E.window_rows = 2;
	E.window_cols = 10;
	E.line_wrap_enabled = 1;
	E.rowoff = 0;
	E.wrapoff = 0;
	E.coloff = 0;
	E.cy = 0;
	E.cx = 0;
	ASSERT_TRUE(editorDrawerSetWidthForCols(1, E.window_cols));

	int text_x = editorTextBodyStartColForCols(E.window_cols) + 1;
	char wheel_down[32];
	ASSERT_TRUE(format_sgr_mouse_event(wheel_down, sizeof(wheel_down), 65, text_x, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_down, strlen(wheel_down)) == 0);
	ASSERT_EQ_INT(0, E.rowoff);
	ASSERT_EQ_INT(3, E.wrapoff);
	ASSERT_EQ_INT(0, E.coloff);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FREE_SCROLL, E.viewport_mode);

	char wheel_up[32];
	ASSERT_TRUE(format_sgr_mouse_event(wheel_up, sizeof(wheel_up), 64, text_x, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_up, strlen(wheel_up)) == 0);
	ASSERT_EQ_INT(0, E.rowoff);
	ASSERT_EQ_INT(0, E.wrapoff);
	return 0;
}

static int test_editor_process_keypress_mouse_wheel_scrolls_horizontally_and_clamps(void) {
	add_row("abcdefghijklmnopqrstuvwxyz");
	E.window_rows = 5;
	E.window_cols = 12;
	E.cy = 0;
	E.cx = 5;
	E.coloff = 0;

	int text_x = editorTextBodyStartColForCols(E.window_cols) + 1;
	char wheel_right[32];
	ASSERT_TRUE(format_sgr_mouse_event(wheel_right, sizeof(wheel_right), 67, text_x, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_right, strlen(wheel_right)) == 0);
	ASSERT_EQ_INT(5, E.cx);
	ASSERT_EQ_INT(3, E.coloff);
	ASSERT_EQ_INT(EDITOR_VIEWPORT_FREE_SCROLL, E.viewport_mode);

	E.coloff = 24;
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_right, strlen(wheel_right)) == 0);
	ASSERT_EQ_INT(25, E.coloff);

	char wheel_left[32];
	ASSERT_TRUE(format_sgr_mouse_event(wheel_left, sizeof(wheel_left), 66, text_x, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_left, strlen(wheel_left)) == 0);
	ASSERT_EQ_INT(22, E.coloff);

	E.coloff = 1;
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_left, strlen(wheel_left)) == 0);
	ASSERT_EQ_INT(0, E.coloff);
	return 0;
}

static int test_editor_process_keypress_mouse_wheel_scrolls_drawer_when_hovered(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	for (int i = 0; i < 12; i++) {
		char name[32];
		char path[512];
		ASSERT_TRUE(snprintf(name, sizeof(name), "file-%02d.txt", i) > 0);
		ASSERT_TRUE(path_join(path, sizeof(path), env.project_dir, name));
		ASSERT_TRUE(write_text_file(path, "x\n"));
	}

	for (int i = 0; i < 10; i++) {
		add_row("line");
	}
	E.window_rows = 4;
	E.window_cols = 30;
	E.cy = 4;
	E.cx = 0;
	E.rowoff = 2;
	E.drawer_rowoff = 0;

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	E.primary_focus = EDITOR_PRIMARY_FOCUS_TEXT;

	const char wheel_down[] = "\x1b[<65;1;2M";
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_down, sizeof(wheel_down) - 1) == 0);
	ASSERT_EQ_INT(3, E.drawer_rowoff);
	ASSERT_EQ_INT(2, E.rowoff);

	const char wheel_up[] = "\x1b[<64;1;2M";
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_up, sizeof(wheel_up) - 1) == 0);
	ASSERT_EQ_INT(0, E.drawer_rowoff);
	ASSERT_EQ_INT(2, E.rowoff);

	for (int i = 0; i < 12; i++) {
		char name[32];
		char path[512];
		ASSERT_TRUE(snprintf(name, sizeof(name), "file-%02d.txt", i) > 0);
		ASSERT_TRUE(path_join(path, sizeof(path), env.project_dir, name));
		ASSERT_TRUE(unlink(path) == 0);
	}
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_mouse_wheel_scrolls_drawer_with_empty_buffer(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	for (int i = 0; i < 12; i++) {
		char name[32];
		char path[512];
		ASSERT_TRUE(snprintf(name, sizeof(name), "file-%02d.txt", i) > 0);
		ASSERT_TRUE(path_join(path, sizeof(path), env.project_dir, name));
		ASSERT_TRUE(write_text_file(path, "x\n"));
	}

	E.window_rows = 4;
	E.window_cols = 30;
	E.drawer_rowoff = 0;

	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_EQ_INT(0, E.numrows);
	ASSERT_EQ_INT(0, E.drawer_selected_index);

	const char wheel_down[] = "\x1b[<65;1;2M";
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_down, sizeof(wheel_down) - 1) == 0);
	ASSERT_EQ_INT(3, E.drawer_rowoff);

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	free(output);
	ASSERT_EQ_INT(3, E.drawer_rowoff);

	for (int i = 0; i < 12; i++) {
		char name[32];
		char path[512];
		ASSERT_TRUE(snprintf(name, sizeof(name), "file-%02d.txt", i) > 0);
		ASSERT_TRUE(path_join(path, sizeof(path), env.project_dir, name));
		ASSERT_TRUE(unlink(path) == 0);
	}
	cleanup_recovery_test_env(&env);
	return 0;
}

static int
test_editor_process_keypress_mouse_wheel_scrolls_text_when_hovered_even_if_drawer_focused(void) {
	for (int i = 0; i < 10; i++) {
		add_row("line");
	}
	E.window_rows = 5;
	E.window_cols = 30;
	E.cy = 4;
	E.cx = 0;
	E.rowoff = 0;
	E.drawer_rowoff = 2;
	E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;

	int text_x = editorTextBodyStartColForCols(E.window_cols) + 1;
	char wheel_down[32];
	ASSERT_TRUE(format_sgr_mouse_event(wheel_down, sizeof(wheel_down), 65, text_x, 3, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_down, strlen(wheel_down)) == 0);
	ASSERT_EQ_INT(3, E.rowoff);
	ASSERT_EQ_INT(2, E.drawer_rowoff);

	char wheel_up[32];
	ASSERT_TRUE(format_sgr_mouse_event(wheel_up, sizeof(wheel_up), 64, text_x, 3, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(wheel_up, strlen(wheel_up)) == 0);
	ASSERT_EQ_INT(0, E.rowoff);
	ASSERT_EQ_INT(2, E.drawer_rowoff);
	return 0;
}

static int test_editor_process_keypress_mouse_click_clears_existing_selection(void) {
	add_row("abcdef");
	E.window_cols = 20;
	E.cy = 0;
	E.cx = 1;
	begin_selection();

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char click[32];
	ASSERT_TRUE(format_sgr_mouse_event(click, sizeof(click), 0, text_start + 5, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(click, strlen(click)) == 0);
	ASSERT_EQ_INT(0, E.selection_mode_active);
	ASSERT_EQ_INT(0, E.selection_anchor_offset);
	ASSERT_EQ_INT(4, E.cx);

	struct editorSelectionRange range;
	ASSERT_EQ_INT(0, editorGetSelectionRange(&range));
	return 0;
}

static int test_editor_process_keypress_mouse_drag_starts_selection_without_ctrl_b(void) {
	add_row("abcdef");
	E.window_rows = 3;
	E.window_cols = 20;
	E.cy = 0;
	E.cx = 0;

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char press[32];
	char drag[32];
	ASSERT_TRUE(format_sgr_mouse_event(press, sizeof(press), 0, text_start + 2, 2, 'M'));
	ASSERT_TRUE(format_sgr_mouse_event(drag, sizeof(drag), 32, text_start + 6, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_EQ_INT(0, E.selection_mode_active);
	ASSERT_TRUE(editor_process_keypress_with_input(drag, strlen(drag)) == 0);
	ASSERT_EQ_INT(1, E.selection_mode_active);
	ASSERT_EQ_INT(0, assert_selection_anchor(0, 1));

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

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char press[32];
	char release[32];
	ASSERT_TRUE(format_sgr_mouse_event(press, sizeof(press), 0, text_start + 4, 2, 'M'));
	ASSERT_TRUE(format_sgr_mouse_event(release, sizeof(release), 0, text_start + 4, 2, 'm'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_EQ_INT(0, E.selection_mode_active);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(3, E.cx);
	ASSERT_EQ_INT(1, E.mouse_left_button_down);

	ASSERT_TRUE(editor_process_keypress_with_input(release, strlen(release)) == 0);
	ASSERT_EQ_INT(0, E.mouse_left_button_down);
	ASSERT_EQ_INT(0, E.mouse_drag_started);

	struct editorSelectionRange range;
	ASSERT_EQ_INT(0, editorGetSelectionRange(&range));
	return 0;
}

static int test_editor_process_keypress_mouse_drag_resets_existing_selection_anchor(void) {
	add_row("abcdef");
	E.window_cols = 20;
	E.cy = 0;
	E.cx = 1;
	begin_selection();
	E.cx = 4;

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char press[32];
	char drag[32];
	ASSERT_TRUE(format_sgr_mouse_event(press, sizeof(press), 0, text_start + 6, 2, 'M'));
	ASSERT_TRUE(format_sgr_mouse_event(drag, sizeof(drag), 32, text_start + 3, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_TRUE(editor_process_keypress_with_input(drag, strlen(drag)) == 0);

	ASSERT_EQ_INT(1, E.selection_mode_active);
	ASSERT_EQ_INT(0, assert_selection_anchor(0, 5));

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
	E.window_cols = 10;
	E.line_numbers_enabled = 0;
	E.rowoff = 2;
	E.coloff = 1;
	E.cy = 0;
	E.cx = 0;

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char press[32];
	char drag[32];
	ASSERT_TRUE(format_sgr_mouse_event(press, sizeof(press), 0, text_start + 3, 3, 'M'));
	ASSERT_TRUE(format_sgr_mouse_event(drag, sizeof(drag), 32, text_start + 50, 9, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_TRUE(editor_process_keypress_with_input(drag, strlen(drag)) == 0);

	ASSERT_EQ_INT(1, E.selection_mode_active);
	ASSERT_EQ_INT(0, assert_selection_anchor(3, 3));
	ASSERT_EQ_INT(4, E.cy);
	ASSERT_EQ_INT(3, E.cx);
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

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char press[32];
	char drag[32];
	ASSERT_TRUE(format_sgr_mouse_event(press, sizeof(press), 0, text_start + 2, 2, 'M'));
	ASSERT_TRUE(format_sgr_mouse_event(drag, sizeof(drag), 32, text_start + 4, 3, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_TRUE(editor_process_keypress_with_input(drag, strlen(drag)) == 0);

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

	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char press[32];
	char drag[32];
	char release[32];
	char drag_after_release[32];
	ASSERT_TRUE(format_sgr_mouse_event(press, sizeof(press), 0, text_start + 2, 2, 'M'));
	ASSERT_TRUE(format_sgr_mouse_event(drag, sizeof(drag), 32, text_start + 5, 2, 'M'));
	ASSERT_TRUE(format_sgr_mouse_event(release, sizeof(release), 0, text_start + 5, 2, 'm'));
	ASSERT_TRUE(format_sgr_mouse_event(drag_after_release, sizeof(drag_after_release), 32,
	                                   text_start + 6, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(press, strlen(press)) == 0);
	ASSERT_TRUE(editor_process_keypress_with_input(drag, strlen(drag)) == 0);
	ASSERT_EQ_INT(4, E.cx);
	ASSERT_TRUE(editor_process_keypress_with_input(release, strlen(release)) == 0);
	ASSERT_EQ_INT(1, E.selection_mode_active);
	ASSERT_EQ_INT(0, assert_selection_anchor(0, 1));
	ASSERT_EQ_INT(0, E.mouse_left_button_down);
	ASSERT_TRUE(editor_process_keypress_with_input(drag_after_release,
	                                               strlen(drag_after_release)) == 0);
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

static int test_editor_bracketed_paste_markers_toggle_paste_active(void) {
	add_row("hello");
	E.cy = 0;
	E.cx = 0;
	E.paste_active = 0;

	char start_seq[] = "\x1b[200~";
	ASSERT_TRUE(editor_process_keypress_with_input(start_seq, sizeof(start_seq) - 1) == 0);
	ASSERT_EQ_INT(1, E.paste_active);

	char end_seq[] = "\x1b[201~";
	ASSERT_TRUE(editor_process_keypress_with_input(end_seq, sizeof(end_seq) - 1) == 0);
	ASSERT_EQ_INT(0, E.paste_active);
	return 0;
}

static int test_editor_process_keypress_mouse_drawer_right_click_on_file_opens_menu(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char file_path[512];
	ASSERT_TRUE(path_join(file_path, sizeof(file_path), env.project_dir, "alpha.txt"));
	ASSERT_TRUE(write_text_file(file_path, "alpha\n"));

	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));
	int file_idx = -1;
	ASSERT_TRUE(find_drawer_entry("alpha.txt", &file_idx, NULL));

	int row = file_idx - E.drawer_rowoff + 2;
	char right[32];
	ASSERT_TRUE(format_sgr_mouse_event(right, sizeof(right), 2, 2, row, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(right, strlen(right)) == 0);

	ASSERT_TRUE(editorPopupIsVisible());
	ASSERT_EQ_INT(EDITOR_POPUP_KIND_DRAWER_MENU, E.popup.kind);
	ASSERT_EQ_INT(2, editorPopupItemCount());
	ASSERT_EQ_STR("Rename", E.popup.items[0].label);
	ASSERT_EQ_STR("Delete", E.popup.items[1].label);
	ASSERT_EQ_INT(file_idx, E.drawer_selected_index);
	ASSERT_EQ_INT(EDITOR_PRIMARY_FOCUS_DRAWER, E.primary_focus);

	ASSERT_TRUE(unlink(file_path) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_mouse_drawer_right_click_on_folder_shows_full_menu(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char dir_path[512];
	char child_path[512];
	ASSERT_TRUE(path_join(dir_path, sizeof(dir_path), env.project_dir, "src"));
	ASSERT_TRUE(path_join(child_path, sizeof(child_path), dir_path, "child.txt"));
	ASSERT_TRUE(make_dir(dir_path));
	ASSERT_TRUE(write_text_file(child_path, "c\n"));

	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));
	int dir_idx = -1;
	ASSERT_TRUE(find_drawer_entry("src", &dir_idx, NULL));

	int row = dir_idx - E.drawer_rowoff + 2;
	char right[32];
	ASSERT_TRUE(format_sgr_mouse_event(right, sizeof(right), 2, 2, row, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(right, strlen(right)) == 0);

	ASSERT_TRUE(editorPopupIsVisible());
	ASSERT_EQ_INT(EDITOR_POPUP_KIND_DRAWER_MENU, E.popup.kind);
	ASSERT_EQ_INT(4, editorPopupItemCount());
	ASSERT_EQ_STR("New File", E.popup.items[0].label);
	ASSERT_EQ_STR("New Folder", E.popup.items[1].label);
	ASSERT_EQ_STR("Rename", E.popup.items[2].label);
	ASSERT_EQ_STR("Delete", E.popup.items[3].label);
	ASSERT_EQ_INT(dir_idx, E.drawer_selected_index);

	ASSERT_TRUE(unlink(child_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_mouse_drawer_right_click_on_root_omits_rename_delete(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));

	int root_row = 0 - E.drawer_rowoff + 2;
	char right[32];
	ASSERT_TRUE(format_sgr_mouse_event(right, sizeof(right), 2, 2, root_row, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(right, strlen(right)) == 0);

	ASSERT_TRUE(editorPopupIsVisible());
	ASSERT_EQ_INT(EDITOR_POPUP_KIND_DRAWER_MENU, E.popup.kind);
	ASSERT_EQ_INT(2, editorPopupItemCount());
	ASSERT_EQ_STR("New File", E.popup.items[0].label);
	ASSERT_EQ_STR("New Folder", E.popup.items[1].label);

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_mouse_left_click_outside_popup_closes_it(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char file_path[512];
	ASSERT_TRUE(path_join(file_path, sizeof(file_path), env.project_dir, "alpha.txt"));
	ASSERT_TRUE(write_text_file(file_path, "alpha\n"));

	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));
	int file_idx = -1;
	ASSERT_TRUE(find_drawer_entry("alpha.txt", &file_idx, NULL));

	int row = file_idx - E.drawer_rowoff + 2;
	char right[32];
	ASSERT_TRUE(format_sgr_mouse_event(right, sizeof(right), 2, 2, row, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(right, strlen(right)) == 0);
	ASSERT_TRUE(editorPopupIsVisible());

	/* Left click in the text area, away from the popup, should dismiss it. */
	int text_start = editorTextBodyStartColForCols(E.window_cols);
	char left[32];
	ASSERT_TRUE(format_sgr_mouse_event(left, sizeof(left), 0, text_start + 4, E.window_rows + 1,
	                                   'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(left, strlen(left)) == 0);
	ASSERT_TRUE(!editorPopupIsVisible());

	ASSERT_TRUE(unlink(file_path) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_mouse_drawer_right_click_header_does_not_open_menu(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));

	/* Header row is screen row 1 (drawer_row 0). */
	char right[32];
	ASSERT_TRUE(format_sgr_mouse_event(right, sizeof(right), 2, 4, 1, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(right, strlen(right)) == 0);
	ASSERT_TRUE(!editorPopupIsVisible());

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_process_keypress_mouse_drawer_right_click_on_collapsed_drawer_ignored(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));
	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerSetCollapsed(1));

	char right[32];
	ASSERT_TRUE(format_sgr_mouse_event(right, sizeof(right), 2, 1, 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(right, strlen(right)) == 0);
	ASSERT_TRUE(!editorPopupIsVisible());

	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_drawer_menu_popup_survives_mouse_motion(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char file_path[512];
	ASSERT_TRUE(path_join(file_path, sizeof(file_path), env.project_dir, "alpha.txt"));
	ASSERT_TRUE(write_text_file(file_path, "alpha\n"));

	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));
	int file_idx = -1;
	ASSERT_TRUE(find_drawer_entry("alpha.txt", &file_idx, NULL));

	int row = file_idx - E.drawer_rowoff + 2;
	char right[32];
	ASSERT_TRUE(format_sgr_mouse_event(right, sizeof(right), 2, 2, row, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(right, strlen(right)) == 0);
	ASSERT_TRUE(editorPopupIsVisible());

	/* SGR motion with no button held (cb 32+3=35); the menu must survive. */
	char motion[32];
	ASSERT_TRUE(format_sgr_mouse_event(motion, sizeof(motion), 35, 10, row + 2, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(motion, strlen(motion)) == 0);
	ASSERT_TRUE(editorPopupIsVisible());

	ASSERT_TRUE(unlink(file_path) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

static int test_editor_drawer_menu_popup_handles_arrow_and_escape_keys(void) {
	struct recoveryTestEnv env;
	ASSERT_TRUE(setup_recovery_test_env(&env));

	char dir_path[512];
	char child_path[512];
	ASSERT_TRUE(path_join(dir_path, sizeof(dir_path), env.project_dir, "src"));
	ASSERT_TRUE(path_join(child_path, sizeof(child_path), dir_path, "child.txt"));
	ASSERT_TRUE(make_dir(dir_path));
	ASSERT_TRUE(write_text_file(child_path, "c\n"));

	ASSERT_TRUE(editorTabsInit());
	ASSERT_TRUE(editorDrawerInitForStartup(1, NULL, 0));
	ASSERT_TRUE(editorDrawerExpandSelection(E.window_rows + 1));
	int dir_idx = -1;
	ASSERT_TRUE(find_drawer_entry("src", &dir_idx, NULL));

	int row = dir_idx - E.drawer_rowoff + 2;
	char right[32];
	ASSERT_TRUE(format_sgr_mouse_event(right, sizeof(right), 2, 2, row, 'M'));
	ASSERT_TRUE(editor_process_keypress_with_input(right, strlen(right)) == 0);
	ASSERT_TRUE(editorPopupIsVisible());
	ASSERT_EQ_INT(0, editorPopupSelectedIndex());

	ASSERT_EQ_INT(EDITOR_POPUP_KEY_CONSUMED, editorPopupHandleKey(ARROW_DOWN));
	ASSERT_EQ_INT(1, editorPopupSelectedIndex());
	ASSERT_EQ_INT(EDITOR_POPUP_KEY_CONSUMED, editorPopupHandleKey(ARROW_UP));
	ASSERT_EQ_INT(0, editorPopupSelectedIndex());

	ASSERT_EQ_INT(EDITOR_POPUP_KEY_CONSUMED, editorPopupHandleKey('\x1b'));
	ASSERT_TRUE(!editorPopupIsVisible());

	ASSERT_TRUE(unlink(child_path) == 0);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	cleanup_recovery_test_env(&env);
	return 0;
}

const struct editorTestCase g_input_mouse_tests[] = {
        {"editor_column_select_alt_mouse_drag_starts_column_selection",
         test_editor_column_select_alt_mouse_drag_starts_column_selection},
        {"editor_process_keypress_mouse_left_click_places_cursor_with_offsets",
         test_editor_process_keypress_mouse_left_click_places_cursor_with_offsets},
        {"editor_process_keypress_mouse_click_places_cursor_in_created_pane",
         test_editor_process_keypress_mouse_click_places_cursor_in_created_pane},
        {"editor_process_keypress_mouse_click_maps_same_column_with_line_numbers",
         test_editor_process_keypress_mouse_click_maps_same_column_with_line_numbers},
        {"editor_process_keypress_mouse_left_click_places_cursor_on_wrapped_segment",
         test_editor_process_keypress_mouse_left_click_places_cursor_on_wrapped_segment},
        {"editor_process_keypress_mouse_ctrl_click_does_not_start_drag_selection",
         test_editor_process_keypress_mouse_ctrl_click_does_not_start_drag_selection},
        {"editor_process_keypress_mouse_ctrl_hover_marks_word_as_hover_link",
         test_editor_process_keypress_mouse_ctrl_hover_marks_word_as_hover_link},
        {"editor_process_keypress_mouse_motion_without_ctrl_does_not_mark_hover",
         test_editor_process_keypress_mouse_motion_without_ctrl_does_not_mark_hover},
        {"editor_keypress_clears_hover_link", test_editor_keypress_clears_hover_link},
        {"editor_process_keypress_mouse_left_click_ignores_non_text_rows",
         test_editor_process_keypress_mouse_left_click_ignores_non_text_rows},
        {"editor_process_keypress_mouse_left_click_ignores_indicator_padding_columns",
         test_editor_process_keypress_mouse_left_click_ignores_indicator_padding_columns},
        {"editor_process_keypress_mouse_drawer_click_selects_and_toggles_directory",
         test_editor_process_keypress_mouse_drawer_click_selects_and_toggles_directory},
        {"editor_process_keypress_mouse_click_expands_collapsed_drawer",
         test_editor_process_keypress_mouse_click_expands_collapsed_drawer},
        {"editor_process_keypress_mouse_drawer_header_mode_buttons",
         test_editor_process_keypress_mouse_drawer_header_mode_buttons},
        {"editor_process_keypress_mouse_collapsed_drawer_body_click_edits_text_pane",
         test_editor_process_keypress_mouse_collapsed_drawer_body_click_edits_text_pane},
        {"editor_process_keypress_mouse_drawer_single_file_click_opens_preview_tab",
         test_editor_process_keypress_mouse_drawer_single_file_click_opens_preview_tab},
        {"editor_process_keypress_mouse_drawer_double_click_file_pins_preview_tab",
         test_editor_process_keypress_mouse_drawer_double_click_file_pins_preview_tab},
        {"editor_process_keypress_mouse_top_row_double_click_pins_preview_tab",
         test_editor_process_keypress_mouse_top_row_double_click_pins_preview_tab},
        {"editor_process_keypress_mouse_top_row_click_switches_tab",
         test_editor_process_keypress_mouse_top_row_click_switches_tab},
        {"editor_process_keypress_mouse_top_row_click_uses_variable_tab_layout",
         test_editor_process_keypress_mouse_top_row_click_uses_variable_tab_layout},
        {"editor_process_keypress_mouse_tab_bar_carets_switch_hidden_tabs",
         test_editor_process_keypress_mouse_tab_bar_carets_switch_hidden_tabs},
        {"editor_process_keypress_mouse_pane_strip_click_activates_pane_tab",
         test_editor_process_keypress_mouse_pane_strip_click_activates_pane_tab},
        {"editor_process_keypress_mouse_strip_blank_border_arms_resize",
         test_editor_process_keypress_mouse_strip_blank_border_arms_resize},
        {"editor_process_keypress_mouse_top_strip_vborder_arms_resize",
         test_editor_process_keypress_mouse_top_strip_vborder_arms_resize},
        {"editor_process_keypress_mouse_pane_strip_double_click_pins_preview_tab",
         test_editor_process_keypress_mouse_pane_strip_double_click_pins_preview_tab},
        {"editor_process_keypress_mouse_tab_drag_reorders_within_pane",
         test_editor_process_keypress_mouse_tab_drag_reorders_within_pane},
        {"editor_process_keypress_mouse_tab_drag_moves_across_panes",
         test_editor_process_keypress_mouse_tab_drag_moves_across_panes},
        {"editor_process_keypress_mouse_tab_drag_release_off_strip_cancels",
         test_editor_process_keypress_mouse_tab_drag_release_off_strip_cancels},
        {"editor_process_keypress_mouse_tab_drag_last_tab_repopulates_source_pane",
         test_editor_process_keypress_mouse_tab_drag_last_tab_repopulates_source_pane},
        {"editor_process_keypress_mouse_tab_click_without_drag_clears_drag",
         test_editor_process_keypress_mouse_tab_click_without_drag_clears_drag},
        {"editor_process_keypress_mouse_press_on_pane_border_arms_split_resize",
         test_editor_process_keypress_mouse_press_on_pane_border_arms_split_resize},
        {"editor_process_keypress_mouse_press_off_border_acts_as_text_click",
         test_editor_process_keypress_mouse_press_off_border_acts_as_text_click},
        {"editor_process_keypress_mouse_press_single_pane_never_arms_split",
         test_editor_process_keypress_mouse_press_single_pane_never_arms_split},
        {"editor_process_keypress_mouse_drag_vertical_split_decreases_ratio",
         test_editor_process_keypress_mouse_drag_vertical_split_decreases_ratio},
        {"editor_process_keypress_mouse_drag_vertical_split_increases_ratio",
         test_editor_process_keypress_mouse_drag_vertical_split_increases_ratio},
        {"editor_process_keypress_mouse_drag_horizontal_split_updates_ratio",
         test_editor_process_keypress_mouse_drag_horizontal_split_updates_ratio},
        {"editor_process_keypress_mouse_drag_clamps_to_min_and_max",
         test_editor_process_keypress_mouse_drag_clamps_to_min_and_max},
        {"editor_process_keypress_mouse_drag_inner_split_only_updates_inner",
         test_editor_process_keypress_mouse_drag_inner_split_only_updates_inner},
        {"editor_process_keypress_mouse_drag_does_not_move_cursor_during_resize",
         test_editor_process_keypress_mouse_drag_does_not_move_cursor_during_resize},
        {"editor_process_keypress_mouse_release_clears_split_resize_state",
         test_editor_process_keypress_mouse_release_clears_split_resize_state},
        {"editor_process_keypress_mouse_drag_on_splitter_resizes_drawer",
         test_editor_process_keypress_mouse_drag_on_splitter_resizes_drawer},
        {"editor_process_keypress_mouse_wheel_scrolls_three_lines_and_clamps",
         test_editor_process_keypress_mouse_wheel_scrolls_three_lines_and_clamps},
        {"editor_process_keypress_mouse_wheel_scrolls_wrapped_segments",
         test_editor_process_keypress_mouse_wheel_scrolls_wrapped_segments},
        {"editor_process_keypress_mouse_wheel_scrolls_horizontally_and_clamps",
         test_editor_process_keypress_mouse_wheel_scrolls_horizontally_and_clamps},
        {"editor_process_keypress_mouse_wheel_scrolls_drawer_when_hovered",
         test_editor_process_keypress_mouse_wheel_scrolls_drawer_when_hovered},
        {"editor_process_keypress_mouse_wheel_scrolls_drawer_with_empty_buffer",
         test_editor_process_keypress_mouse_wheel_scrolls_drawer_with_empty_buffer},
        {"editor_process_keypress_mouse_wheel_scrolls_text_when_hovered_even_if_drawer_focused",
         test_editor_process_keypress_mouse_wheel_scrolls_text_when_hovered_even_if_drawer_focused},
        {"editor_process_keypress_mouse_click_clears_existing_selection",
         test_editor_process_keypress_mouse_click_clears_existing_selection},
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
        {"editor_bracketed_paste_markers_toggle_paste_active",
         test_editor_bracketed_paste_markers_toggle_paste_active},
        {"editor_process_keypress_mouse_drawer_right_click_on_file_opens_menu",
         test_editor_process_keypress_mouse_drawer_right_click_on_file_opens_menu},
        {"editor_process_keypress_mouse_drawer_right_click_on_folder_shows_full_menu",
         test_editor_process_keypress_mouse_drawer_right_click_on_folder_shows_full_menu},
        {"editor_process_keypress_mouse_drawer_right_click_on_root_omits_rename_delete",
         test_editor_process_keypress_mouse_drawer_right_click_on_root_omits_rename_delete},
        {"editor_process_keypress_mouse_left_click_outside_popup_closes_it",
         test_editor_process_keypress_mouse_left_click_outside_popup_closes_it},
        {"editor_process_keypress_mouse_drawer_right_click_header_does_not_open_menu",
         test_editor_process_keypress_mouse_drawer_right_click_header_does_not_open_menu},
        {"editor_process_keypress_mouse_drawer_right_click_on_collapsed_drawer_ignored",
         test_editor_process_keypress_mouse_drawer_right_click_on_collapsed_drawer_ignored},
        {"editor_drawer_menu_popup_survives_mouse_motion",
         test_editor_drawer_menu_popup_survives_mouse_motion},
        {"editor_drawer_menu_popup_handles_arrow_and_escape_keys",
         test_editor_drawer_menu_popup_handles_arrow_and_escape_keys},
};

const int g_input_mouse_test_count =
        (int)(sizeof(g_input_mouse_tests) / sizeof(g_input_mouse_tests[0]));
