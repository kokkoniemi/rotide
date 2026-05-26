#include "config/common.h"
#include "rotide.h"
#include "support/terminal.h"
#include "alloc_test_hooks.h"
#include "test_case.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "test_support.h"
#include "test_helpers.h"

static int test_editor_read_key_sequences(void) {
	int key = 0;
	char plain[] = "x";
	char up[] = "\x1b[A";
	char pgup[] = "\x1b[5~";
	char end_key[] = "\x1bOF";
	char plain_escape[] = "\x1b[x";

	ASSERT_TRUE(editor_read_key_with_input(plain, sizeof(plain) - 1, &key) == 0);
	ASSERT_EQ_INT('x', key);

	ASSERT_TRUE(editor_read_key_with_input(up, sizeof(up) - 1, &key) == 0);
	ASSERT_EQ_INT(ARROW_UP, key);

	ASSERT_TRUE(editor_read_key_with_input(pgup, sizeof(pgup) - 1, &key) == 0);
	ASSERT_EQ_INT(PAGE_UP, key);

	ASSERT_TRUE(editor_read_key_with_input(end_key, sizeof(end_key) - 1, &key) == 0);
	ASSERT_EQ_INT(END_KEY, key);

	ASSERT_TRUE(editor_read_key_with_input(plain_escape, sizeof(plain_escape) - 1, &key) == 0);
	ASSERT_EQ_INT('\x1b', key);
	return 0;
}

static int test_editor_read_key_alt_arrow_sequences(void) {
	int key = 0;
	const char csi_alt_left[] = "\x1b[1;3D";
	const char csi_alt_right[] = "\x1b[1;3C";
	const char csi_alt_down[] = "\x1b[1;3B";
	const char csi_alt_up[] = "\x1b[1;3A";
	const char csi_alt_shift_left[] = "\x1b[1;4D";
	const char csi_alt_shift_right[] = "\x1b[1;4C";
	const char csi_alt_shift_down[] = "\x1b[1;4B";
	const char csi_alt_shift_up[] = "\x1b[1;4A";
	const char csi_ctrl_left[] = "\x1b[1;5D";
	const char csi_ctrl_right[] = "\x1b[1;5C";
	const char csi_ctrl_down[] = "\x1b[1;5B";
	const char csi_ctrl_up[] = "\x1b[1;5A";
	const char csi_ctrl_alt_left[] = "\x1b[1;7D";
	const char csi_ctrl_alt_right[] = "\x1b[1;7C";
	const char csi_ctrl_alt_down[] = "\x1b[1;7B";
	const char csi_ctrl_alt_up[] = "\x1b[1;7A";
	const char csi_ctrl_shift_alt_left[] = "\x1b[1;8D";
	const char csi_ctrl_shift_alt_right[] = "\x1b[1;8C";
	const char csi_ctrl_shift_alt_down[] = "\x1b[1;8B";
	const char csi_ctrl_shift_alt_up[] = "\x1b[1;8A";
	const char fallback_alt_left[] = "\x1b\x1b[D";
	const char fallback_alt_right[] = "\x1b\x1b[C";
	const char fallback_alt_down[] = "\x1b\x1b[B";
	const char fallback_alt_up[] = "\x1b\x1b[A";
	const char alt_letter_lower[] = "\x1b"
	                                "a";
	const char alt_letter_upper[] = "\x1b"
	                                "A";
	const char ctrl_alt_letter[] = {'\x1b', CTRL_KEY('b')};

	ASSERT_TRUE(editor_read_key_with_input(csi_alt_left, sizeof(csi_alt_left) - 1, &key) == 0);
	ASSERT_EQ_INT(ALT_ARROW_LEFT, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_alt_right, sizeof(csi_alt_right) - 1, &key) ==
	            0);
	ASSERT_EQ_INT(ALT_ARROW_RIGHT, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_alt_down, sizeof(csi_alt_down) - 1, &key) == 0);
	ASSERT_EQ_INT(ALT_ARROW_DOWN, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_alt_up, sizeof(csi_alt_up) - 1, &key) == 0);
	ASSERT_EQ_INT(ALT_ARROW_UP, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_alt_shift_left, sizeof(csi_alt_shift_left) - 1,
	                                       &key) == 0);
	ASSERT_EQ_INT(ALT_SHIFT_ARROW_LEFT, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_alt_shift_right, sizeof(csi_alt_shift_right) - 1,
	                                       &key) == 0);
	ASSERT_EQ_INT(ALT_SHIFT_ARROW_RIGHT, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_alt_shift_down, sizeof(csi_alt_shift_down) - 1,
	                                       &key) == 0);
	ASSERT_EQ_INT(ALT_SHIFT_ARROW_DOWN, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_alt_shift_up, sizeof(csi_alt_shift_up) - 1,
	                                       &key) == 0);
	ASSERT_EQ_INT(ALT_SHIFT_ARROW_UP, key);

	ASSERT_TRUE(editor_read_key_with_input(csi_ctrl_left, sizeof(csi_ctrl_left) - 1, &key) ==
	            0);
	ASSERT_EQ_INT(CTRL_ARROW_LEFT, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_ctrl_right, sizeof(csi_ctrl_right) - 1, &key) ==
	            0);
	ASSERT_EQ_INT(CTRL_ARROW_RIGHT, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_ctrl_down, sizeof(csi_ctrl_down) - 1, &key) ==
	            0);
	ASSERT_EQ_INT(CTRL_ARROW_DOWN, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_ctrl_up, sizeof(csi_ctrl_up) - 1, &key) == 0);
	ASSERT_EQ_INT(CTRL_ARROW_UP, key);

	ASSERT_TRUE(editor_read_key_with_input(csi_ctrl_alt_left, sizeof(csi_ctrl_alt_left) - 1,
	                                       &key) == 0);
	ASSERT_EQ_INT(CTRL_ALT_ARROW_LEFT, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_ctrl_alt_right, sizeof(csi_ctrl_alt_right) - 1,
	                                       &key) == 0);
	ASSERT_EQ_INT(CTRL_ALT_ARROW_RIGHT, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_ctrl_alt_down, sizeof(csi_ctrl_alt_down) - 1,
	                                       &key) == 0);
	ASSERT_EQ_INT(CTRL_ALT_ARROW_DOWN, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_ctrl_alt_up, sizeof(csi_ctrl_alt_up) - 1,
	                                       &key) == 0);
	ASSERT_EQ_INT(CTRL_ALT_ARROW_UP, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_ctrl_shift_alt_left,
	                                       sizeof(csi_ctrl_shift_alt_left) - 1, &key) == 0);
	ASSERT_EQ_INT(CTRL_SHIFT_ALT_ARROW_LEFT, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_ctrl_shift_alt_right,
	                                       sizeof(csi_ctrl_shift_alt_right) - 1, &key) == 0);
	ASSERT_EQ_INT(CTRL_SHIFT_ALT_ARROW_RIGHT, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_ctrl_shift_alt_down,
	                                       sizeof(csi_ctrl_shift_alt_down) - 1, &key) == 0);
	ASSERT_EQ_INT(CTRL_SHIFT_ALT_ARROW_DOWN, key);
	ASSERT_TRUE(editor_read_key_with_input(csi_ctrl_shift_alt_up,
	                                       sizeof(csi_ctrl_shift_alt_up) - 1, &key) == 0);
	ASSERT_EQ_INT(CTRL_SHIFT_ALT_ARROW_UP, key);

	ASSERT_TRUE(editor_read_key_with_input(fallback_alt_left, sizeof(fallback_alt_left) - 1,
	                                       &key) == 0);
	ASSERT_EQ_INT(ALT_ARROW_LEFT, key);
	ASSERT_TRUE(editor_read_key_with_input(fallback_alt_right, sizeof(fallback_alt_right) - 1,
	                                       &key) == 0);
	ASSERT_EQ_INT(ALT_ARROW_RIGHT, key);
	ASSERT_TRUE(editor_read_key_with_input(fallback_alt_down, sizeof(fallback_alt_down) - 1,
	                                       &key) == 0);
	ASSERT_EQ_INT(ALT_ARROW_DOWN, key);
	ASSERT_TRUE(editor_read_key_with_input(fallback_alt_up, sizeof(fallback_alt_up) - 1,
	                                       &key) == 0);
	ASSERT_EQ_INT(ALT_ARROW_UP, key);

	ASSERT_TRUE(editor_read_key_with_input(alt_letter_lower, sizeof(alt_letter_lower) - 1,
	                                       &key) == 0);
	ASSERT_EQ_INT(EDITOR_ALT_LETTER_KEY('a'), key);
	ASSERT_TRUE(editor_read_key_with_input(alt_letter_upper, sizeof(alt_letter_upper) - 1,
	                                       &key) == 0);
	ASSERT_EQ_INT(EDITOR_ALT_LETTER_KEY('a'), key);
	ASSERT_TRUE(editor_read_key_with_input(ctrl_alt_letter, sizeof(ctrl_alt_letter), &key) ==
	            0);
	ASSERT_EQ_INT(EDITOR_CTRL_ALT_LETTER_KEY('b'), key);
	return 0;
}

static int test_editor_read_key_sgr_mouse_events(void) {
	int key = 0;
	struct editorMouseEvent event;
	char left_click[] = "\x1b[<0;5;3M";
	char ctrl_left_click[] = "\x1b[<16;12;6M";
	char left_drag[] = "\x1b[<32;6;4M";
	char left_release[] = "\x1b[<0;6;4m";
	char left_release_alt_cb[] = "\x1b[<3;7;4m";
	char wheel_up[] = "\x1b[<64;7;2M";
	char wheel_down[] = "\x1b[<65;4;9M";
	char wheel_left[] = "\x1b[<66;8;3M";
	char wheel_right[] = "\x1b[<67;9;3M";
	char shift_wheel_up[] = "\x1b[<68;10;5M";
	char shift_wheel_down[] = "\x1b[<69;11;5M";
	char modifier_drag_then_plain[] = "\x1b[<36;1;1MZ";
	char unsupported_then_plain[] = "\x1b[<2;1;1MY";

	ASSERT_TRUE(editor_read_key_with_input(left_click, sizeof(left_click) - 1, &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_LEFT_PRESS, event.kind);
	ASSERT_EQ_INT(5, event.x);
	ASSERT_EQ_INT(3, event.y);
	ASSERT_EQ_INT(EDITOR_MOUSE_MOD_NONE, event.modifiers);
	ASSERT_EQ_INT(0, editorConsumeMouseEvent(&event));

	ASSERT_TRUE(editor_read_key_with_input(ctrl_left_click, sizeof(ctrl_left_click) - 1,
	                                       &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_LEFT_PRESS, event.kind);
	ASSERT_EQ_INT(12, event.x);
	ASSERT_EQ_INT(6, event.y);
	ASSERT_EQ_INT(EDITOR_MOUSE_MOD_CTRL, event.modifiers);

	ASSERT_TRUE(editor_read_key_with_input(left_drag, sizeof(left_drag) - 1, &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_LEFT_DRAG, event.kind);
	ASSERT_EQ_INT(6, event.x);
	ASSERT_EQ_INT(4, event.y);
	ASSERT_EQ_INT(EDITOR_MOUSE_MOD_NONE, event.modifiers);

	ASSERT_TRUE(editor_read_key_with_input(left_release, sizeof(left_release) - 1, &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_LEFT_RELEASE, event.kind);
	ASSERT_EQ_INT(6, event.x);
	ASSERT_EQ_INT(4, event.y);
	ASSERT_EQ_INT(EDITOR_MOUSE_MOD_NONE, event.modifiers);

	ASSERT_TRUE(editor_read_key_with_input(left_release_alt_cb, sizeof(left_release_alt_cb) - 1,
	                                       &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_LEFT_RELEASE, event.kind);
	ASSERT_EQ_INT(7, event.x);
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

	ASSERT_TRUE(editor_read_key_with_input(wheel_left, sizeof(wheel_left) - 1, &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_WHEEL_LEFT, event.kind);
	ASSERT_EQ_INT(8, event.x);
	ASSERT_EQ_INT(3, event.y);

	ASSERT_TRUE(editor_read_key_with_input(wheel_right, sizeof(wheel_right) - 1, &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_WHEEL_RIGHT, event.kind);
	ASSERT_EQ_INT(9, event.x);
	ASSERT_EQ_INT(3, event.y);

	ASSERT_TRUE(editor_read_key_with_input(shift_wheel_up, sizeof(shift_wheel_up) - 1, &key) ==
	            0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_WHEEL_LEFT, event.kind);
	ASSERT_EQ_INT(10, event.x);
	ASSERT_EQ_INT(5, event.y);

	ASSERT_TRUE(editor_read_key_with_input(shift_wheel_down, sizeof(shift_wheel_down) - 1,
	                                       &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_WHEEL_RIGHT, event.kind);
	ASSERT_EQ_INT(11, event.x);
	ASSERT_EQ_INT(5, event.y);

	ASSERT_TRUE(editor_read_key_with_input(modifier_drag_then_plain,
	                                       sizeof(modifier_drag_then_plain) - 1, &key) == 0);
	ASSERT_EQ_INT(MOUSE_EVENT, key);
	ASSERT_TRUE(editorConsumeMouseEvent(&event) == 1);
	ASSERT_EQ_INT(EDITOR_MOUSE_EVENT_LEFT_DRAG, event.kind);
	ASSERT_EQ_INT(EDITOR_MOUSE_MOD_SHIFT, event.modifiers);

	ASSERT_TRUE(editor_read_key_with_input(unsupported_then_plain,
	                                       sizeof(unsupported_then_plain) - 1, &key) == 0);
	ASSERT_EQ_INT('Y', key);
	ASSERT_EQ_INT(0, editorConsumeMouseEvent(&event));
	return 0;
}

static int test_editor_read_key_returns_input_eof_event_on_closed_stdin(void) {
	int key = 0;
	ASSERT_TRUE(editor_read_key_with_input("", 0, &key) == 0);
	ASSERT_EQ_INT(INPUT_EOF_EVENT, key);
	return 0;
}

static int test_editor_read_key_escape_parse_eof_returns_input_eof_event(void) {
	int key = 0;
	char incomplete[] = "\x1b[";
	ASSERT_TRUE(editor_read_key_with_input(incomplete, sizeof(incomplete) - 1, &key) == 0);
	ASSERT_EQ_INT(INPUT_EOF_EVENT, key);
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

	ASSERT_EQ_INT(0, editorReadWindowSize(&rows, &cols));

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
		ASSERT_EQ_INT(-1, editorReadCursorPosition(&rows, &cols));
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

static int test_editor_refresh_window_size_reserves_tab_status_and_message_rows(void) {
	char response[] = "\x1b[9;33R";
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

	ASSERT_EQ_INT(6, E.window_rows);
	ASSERT_EQ_INT(33, E.window_cols);
	free(stdout_bytes);
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

const struct editorTestCase g_workspace_io_tests[] = {
        {"editor_read_key_sequences", test_editor_read_key_sequences},
        {"editor_read_key_alt_arrow_sequences", test_editor_read_key_alt_arrow_sequences},
        {"editor_read_key_sgr_mouse_events", test_editor_read_key_sgr_mouse_events},
        {"editor_read_key_returns_input_eof_event_on_closed_stdin",
         test_editor_read_key_returns_input_eof_event_on_closed_stdin},
        {"editor_read_key_escape_parse_eof_returns_input_eof_event",
         test_editor_read_key_escape_parse_eof_returns_input_eof_event},
        {"editor_read_key_returns_resize_event_when_queued",
         test_editor_read_key_returns_resize_event_when_queued},
        {"read_cursor_position_and_window_size_fallback",
         test_read_cursor_position_and_window_size_fallback},
        {"read_cursor_position_rejects_malformed_responses",
         test_read_cursor_position_rejects_malformed_responses},
        {"editor_refresh_window_size_clamps_tiny_terminal",
         test_editor_refresh_window_size_clamps_tiny_terminal},
        {"editor_refresh_window_size_failure_keeps_previous_dimensions",
         test_editor_refresh_window_size_failure_keeps_previous_dimensions},
        {"editor_refresh_window_size_reserves_tab_status_and_message_rows",
         test_editor_refresh_window_size_reserves_tab_status_and_message_rows},
        {"editor_refresh_screen_reports_oom_without_crash",
         test_editor_refresh_screen_reports_oom_without_crash},
};

const int g_workspace_io_test_count =
        (int)(sizeof(g_workspace_io_tests) / sizeof(g_workspace_io_tests[0]));
