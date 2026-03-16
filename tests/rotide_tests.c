#include "rotide.h"

#include "buffer.h"
#include "input.h"
#include "output.h"
#include "terminal.h"
#include "test_helpers.h"

struct editorConfig E;

static int test_utf8_decode_valid_sequences(void) {
	unsigned int cp = 0;
	const char e_acute[] = "\xC3\xA9";
	const char euro[] = "\xE2\x82\xAC";
	const char grin[] = "\xF0\x9F\x98\x80";

	ASSERT_EQ_INT(1, editorUtf8DecodeCodepoint("A", 1, &cp));
	ASSERT_EQ_INT('A', cp);

	ASSERT_EQ_INT(2, editorUtf8DecodeCodepoint(e_acute, (int)sizeof(e_acute) - 1, &cp));
	ASSERT_EQ_INT(0xE9, cp);

	ASSERT_EQ_INT(3, editorUtf8DecodeCodepoint(euro, (int)sizeof(euro) - 1, &cp));
	ASSERT_EQ_INT(0x20AC, cp);

	ASSERT_EQ_INT(4, editorUtf8DecodeCodepoint(grin, (int)sizeof(grin) - 1, &cp));
	ASSERT_EQ_INT(0x1F600, cp);

	return 0;
}

static int test_utf8_decode_invalid_sequences(void) {
	unsigned int cp = 0;
	const char invalid_lead[] = "\xFF";
	const char truncated[] = "\xC3";
	const char bad_continuation[] = "\xE2\x28\xA1";
	const char overlong[] = "\xC0\xAF";

	ASSERT_EQ_INT(1, editorUtf8DecodeCodepoint(invalid_lead, (int)sizeof(invalid_lead) - 1,
				&cp));
	ASSERT_EQ_INT(0xFF, cp);

	ASSERT_EQ_INT(1, editorUtf8DecodeCodepoint(truncated, (int)sizeof(truncated) - 1, &cp));
	ASSERT_EQ_INT(0xC3, cp);

	ASSERT_EQ_INT(1, editorUtf8DecodeCodepoint(bad_continuation,
				(int)sizeof(bad_continuation) - 1, &cp));
	ASSERT_EQ_INT(0xE2, cp);

	ASSERT_EQ_INT(1, editorUtf8DecodeCodepoint(overlong, (int)sizeof(overlong) - 1, &cp));
	ASSERT_EQ_INT(0xC0, cp);

	return 0;
}

static int test_utf8_continuation_detection(void) {
	ASSERT_TRUE(editorIsUtf8ContinuationByte(0x80));
	ASSERT_TRUE(editorIsUtf8ContinuationByte(0xBF));
	ASSERT_TRUE(!editorIsUtf8ContinuationByte(0x7F));
	ASSERT_TRUE(!editorIsUtf8ContinuationByte(0xC2));
	return 0;
}

static int test_grapheme_extend_classification(void) {
	ASSERT_TRUE(editorIsGraphemeExtendCodepoint(0x0301));
	ASSERT_TRUE(editorIsGraphemeExtendCodepoint(0xFE0F));
	ASSERT_TRUE(editorIsGraphemeExtendCodepoint(0x1F3FB));
	ASSERT_TRUE(editorIsGraphemeExtendCodepoint(0x200C));
	ASSERT_TRUE(!editorIsGraphemeExtendCodepoint(0x200D));
	ASSERT_TRUE(!editorIsGraphemeExtendCodepoint('A'));
	return 0;
}

static int test_char_display_width_basics(void) {
	const char e_acute[] = "\xC3\xA9";
	const char invalid[] = "\xFF";

	ASSERT_EQ_INT(1, editorCharDisplayWidth("A", 1));
	ASSERT_EQ_INT(0, editorCharDisplayWidth(&e_acute[1], 1));
	ASSERT_EQ_INT(1, editorCharDisplayWidth(invalid, (int)sizeof(invalid) - 1));
	return 0;
}

static int test_row_char_boundaries(void) {
	const char text[] = "A\xC3\xA9" "Z";
	add_row_bytes(text, sizeof(text) - 1);

	struct erow *row = &E.rows[0];
	ASSERT_EQ_INT(1, editorRowClampCxToCharBoundary(row, 2));
	ASSERT_EQ_INT(0, editorRowPrevCharIdx(row, 1));
	ASSERT_EQ_INT(1, editorRowPrevCharIdx(row, 3));
	ASSERT_EQ_INT(3, editorRowNextCharIdx(row, 1));
	ASSERT_EQ_INT(4, editorRowNextCharIdx(row, 3));
	return 0;
}

static int test_row_cluster_boundaries_combining(void) {
	const char text[] = "a\xCC\x81" "b";
	add_row_bytes(text, sizeof(text) - 1);

	struct erow *row = &E.rows[0];
	ASSERT_EQ_INT(3, editorRowNextClusterIdx(row, 0));
	ASSERT_EQ_INT(0, editorRowPrevClusterIdx(row, 3));
	ASSERT_EQ_INT(4, editorRowNextClusterIdx(row, 3));
	ASSERT_EQ_INT(0, editorRowClampCxToClusterBoundary(row, 2));
	ASSERT_EQ_INT(3, editorRowClampCxToClusterBoundary(row, 3));
	return 0;
}

static int test_row_cluster_boundaries_zwj_sequence(void) {
	const char woman_technologist[] = "\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB";
	add_row_bytes(woman_technologist, sizeof(woman_technologist) - 1);

	struct erow *row = &E.rows[0];
	ASSERT_EQ_INT((int)sizeof(woman_technologist) - 1, editorRowNextClusterIdx(row, 0));
	ASSERT_EQ_INT(0, editorRowPrevClusterIdx(row, (int)sizeof(woman_technologist) - 1));
	return 0;
}

static int test_row_cluster_boundaries_regional_indicators(void) {
	const char flag_sequence[] = "\xF0\x9F\x87\xAB\xF0\x9F\x87\xAE\xF0\x9F\x87\xA8";
	add_row_bytes(flag_sequence, sizeof(flag_sequence) - 1);

	struct erow *row = &E.rows[0];
	ASSERT_EQ_INT(8, editorRowNextClusterIdx(row, 0));
	ASSERT_EQ_INT((int)sizeof(flag_sequence) - 1, editorRowNextClusterIdx(row, 8));
	ASSERT_EQ_INT(8, editorRowPrevClusterIdx(row, (int)sizeof(flag_sequence) - 1));
	return 0;
}

static int test_row_cx_to_rx_with_tabs(void) {
	add_row("a\tb");
	struct erow *row = &E.rows[0];
	ASSERT_EQ_INT(8, editorRowCxToRx(row, 2));
	ASSERT_EQ_INT(9, editorRowCxToRx(row, 3));
	return 0;
}

static int test_editor_update_row_expands_tabs(void) {
	add_row("a\tb");
	struct erow *row = &E.rows[0];
	ASSERT_EQ_INT(9, row->rsize);
	ASSERT_EQ_STR("a       b", row->render);
	return 0;
}

static int test_insert_and_delete_row_updates_dirty(void) {
	ASSERT_EQ_INT(0, E.dirty);
	add_row("one");
	add_row("two");
	ASSERT_EQ_INT(2, E.numrows);
	ASSERT_EQ_INT(2, E.dirty);

	editorInsertRow(-1, "noop", 4);
	ASSERT_EQ_INT(2, E.numrows);
	ASSERT_EQ_INT(2, E.dirty);

	editorDeleteRow(0);
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("two", E.rows[0].chars);
	ASSERT_EQ_INT(3, E.dirty);
	return 0;
}

static int test_insert_and_delete_chars(void) {
	add_row("abc");
	E.dirty = 0;

	struct erow *row = &E.rows[0];
	editorInsertCharAt(row, 1, 'X');
	ASSERT_EQ_STR("aXbc", row->chars);
	ASSERT_EQ_INT(1, E.dirty);

	editorDelCharAt(row, 2);
	ASSERT_EQ_STR("aXc", row->chars);
	ASSERT_EQ_INT(2, E.dirty);

	editorDelCharsAt(row, 1, 2);
	ASSERT_EQ_STR("a", row->chars);
	ASSERT_EQ_INT(3, E.dirty);

	editorDelCharsAt(row, 1, 0);
	ASSERT_EQ_STR("a", row->chars);
	ASSERT_EQ_INT(3, E.dirty);
	return 0;
}

static int test_editor_insert_char_creates_initial_row(void) {
	editorInsertChar('Q');
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("Q", E.rows[0].chars);
	ASSERT_EQ_INT(1, E.cx);
	ASSERT_EQ_INT(2, E.dirty);
	return 0;
}

static int test_editor_insert_newline_splits_row(void) {
	add_row("hello");
	E.dirty = 0;
	E.cy = 0;
	E.cx = 2;

	editorInsertNewline();
	ASSERT_EQ_INT(2, E.numrows);
	ASSERT_EQ_STR("he", E.rows[0].chars);
	ASSERT_EQ_STR("llo", E.rows[1].chars);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(0, E.cx);
	ASSERT_EQ_INT(1, E.dirty);
	return 0;
}

static int test_editor_insert_newline_at_row_start(void) {
	add_row("hello");
	E.dirty = 0;
	E.cy = 0;
	E.cx = 0;

	editorInsertNewline();
	ASSERT_EQ_INT(2, E.numrows);
	ASSERT_EQ_STR("", E.rows[0].chars);
	ASSERT_EQ_STR("hello", E.rows[1].chars);
	ASSERT_EQ_INT(1, E.cy);
	ASSERT_EQ_INT(1, E.dirty);
	return 0;
}

static int test_editor_del_char_cluster_and_merge(void) {
	const char with_combining[] = "a\xCC\x81" "b";
	add_row_bytes(with_combining, sizeof(with_combining) - 1);
	E.cy = 0;
	E.cx = 3;
	E.dirty = 0;

	editorDelChar();
	ASSERT_EQ_STR("b", E.rows[0].chars);
	ASSERT_EQ_INT(0, E.cx);
	ASSERT_EQ_INT(1, E.dirty);

	reset_editor_state();
	add_row("abc");
	add_row("def");
	E.dirty = 0;
	E.cy = 1;
	E.cx = 0;

	editorDelChar();
	ASSERT_EQ_INT(1, E.numrows);
	ASSERT_EQ_STR("abcdef", E.rows[0].chars);
	ASSERT_EQ_INT(0, E.cy);
	ASSERT_EQ_INT(3, E.cx);
	ASSERT_EQ_INT(2, E.dirty);
	return 0;
}

static int test_editor_rows_to_str(void) {
	add_row("a");
	add_row("bc");
	add_row("");

	int buflen = 0;
	char *joined = editorRowsToStr(&buflen);
	ASSERT_TRUE(joined != NULL);
	ASSERT_EQ_INT(6, buflen);
	ASSERT_MEM_EQ("a\nbc\n\n", joined, (size_t)buflen);
	free(joined);
	return 0;
}

static int test_editor_open_reads_rows_and_clears_dirty(void) {
	char path[] = "/tmp/rotide-test-open-XXXXXX";
	int fd = mkstemp(path);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(write_all(fd, "alpha\r\nbeta\n\n", 13) == 0);
	ASSERT_TRUE(close(fd) == 0);

	editorOpen(path);

	ASSERT_EQ_STR(path, E.filename);
	ASSERT_EQ_INT(3, E.numrows);
	ASSERT_EQ_STR("alpha", E.rows[0].chars);
	ASSERT_EQ_STR("beta", E.rows[1].chars);
	ASSERT_EQ_STR("", E.rows[2].chars);
	ASSERT_EQ_INT(0, E.dirty);

	unlink(path);
	return 0;
}

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

static int test_editor_prompt_accept_and_cancel(void) {
	char ok_input[] = "name\r";
	char esc_input[] = "\x1b";

	char *answer = editor_prompt_with_input(ok_input, sizeof(ok_input) - 1, "Name: %s");
	ASSERT_TRUE(answer != NULL);
	ASSERT_EQ_STR("name", answer);
	free(answer);

	answer = editor_prompt_with_input(esc_input, sizeof(esc_input) - 1, "Name: %s");
	ASSERT_TRUE(answer == NULL);
	ASSERT_EQ_STR("", E.statusmsg);
	return 0;
}

static int test_editor_read_key_sequences(void) {
	int key = 0;
	char plain[] = "x";
	char up[] = "\x1b[A";
	char pgup[] = "\x1b[5~";
	char end_key[] = "\x1bOF";
	char incomplete[] = "\x1b[";

	ASSERT_TRUE(editor_read_key_with_input(plain, sizeof(plain) - 1, &key) == 0);
	ASSERT_EQ_INT('x', key);

	ASSERT_TRUE(editor_read_key_with_input(up, sizeof(up) - 1, &key) == 0);
	ASSERT_EQ_INT(ARROW_UP, key);

	ASSERT_TRUE(editor_read_key_with_input(pgup, sizeof(pgup) - 1, &key) == 0);
	ASSERT_EQ_INT(PAGE_UP, key);

	ASSERT_TRUE(editor_read_key_with_input(end_key, sizeof(end_key) - 1, &key) == 0);
	ASSERT_EQ_INT(END_KEY, key);

	ASSERT_TRUE(editor_read_key_with_input(incomplete, sizeof(incomplete) - 1, &key) == 0);
	ASSERT_EQ_INT('\x1b', key);
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

	ASSERT_EQ_INT(0, readWindowSize(&rows, &cols));

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

static int test_editor_process_keypress_insert_move_and_backspace(void) {
	add_row("ab");
	E.cy = 0;
	E.cx = 2;

	char backspace[] = {BACKSPACE};
	ASSERT_TRUE(editor_process_keypress_with_input(backspace, sizeof(backspace)) == 0);
	ASSERT_EQ_STR("a", E.rows[0].chars);
	ASSERT_EQ_INT(1, E.cx);

	char insert_z[] = {'Z'};
	ASSERT_TRUE(editor_process_keypress_with_input(insert_z, sizeof(insert_z)) == 0);
	ASSERT_EQ_STR("aZ", E.rows[0].chars);
	ASSERT_EQ_INT(2, E.cx);

	char arrow_left[] = "\x1b[D";
	ASSERT_TRUE(editor_process_keypress_with_input(arrow_left, sizeof(arrow_left) - 1) == 0);
	ASSERT_EQ_INT(1, E.cx);

	char home_key[] = "\x1b[H";
	ASSERT_TRUE(editor_process_keypress_with_input(home_key, sizeof(home_key) - 1) == 0);
	ASSERT_EQ_INT(0, E.cx);

	char end_key[] = "\x1b[F";
	ASSERT_TRUE(editor_process_keypress_with_input(end_key, sizeof(end_key) - 1) == 0);
	ASSERT_EQ_INT((int)strlen(E.rows[0].chars), E.cx);
	return 0;
}

static int test_editor_process_keypress_delete_key(void) {
	add_row("abcd");
	E.cy = 0;
	E.cx = 1;

	char del_key[] = "\x1b[3~";
	ASSERT_TRUE(editor_process_keypress_with_input(del_key, sizeof(del_key) - 1) == 0);
	ASSERT_EQ_STR("acd", E.rows[0].chars);
	ASSERT_EQ_INT(1, E.cx);
	return 0;
}

static int test_editor_process_keypress_ctrl_s_saves_file(void) {
	char path[] = "/tmp/rotide-test-ctrls-XXXXXX";
	int fd = mkstemp(path);
	ASSERT_TRUE(fd != -1);
	ASSERT_TRUE(close(fd) == 0);

	add_row("line1");
	add_row("line2");
	E.filename = strdup(path);
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 7;

	char ctrl_s[] = {CTRL_KEY('s')};
	ASSERT_TRUE(editor_process_keypress_with_input(ctrl_s, sizeof(ctrl_s)) == 0);
	ASSERT_EQ_INT(0, E.dirty);

	size_t content_len = 0;
	char *contents = read_file_contents(path, &content_len);
	ASSERT_TRUE(contents != NULL);
	ASSERT_MEM_EQ("line1\nline2\n", contents, content_len);

	free(contents);
	unlink(path);
	return 0;
}

static int test_editor_refresh_screen_contains_expected_sequences(void) {
	add_row("first line");
	add_row("second line");
	E.cy = 1;
	E.cx = 3;
	E.rowoff = 0;
	E.coloff = 0;
	E.window_rows = 4;
	E.window_cols = 30;
	E.filename = strdup("sample.txt");
	ASSERT_TRUE(E.filename != NULL);
	E.dirty = 1;
	editorSetStatusMsg("status message");

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(output_len > 0);
	ASSERT_TRUE(strstr(output, "\x1b[?25l") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[?25h") != NULL);
	ASSERT_TRUE(strstr(output, "\x1b[7m") != NULL);
	ASSERT_TRUE(strstr(output, "first line") != NULL);
	ASSERT_TRUE(strstr(output, "status message") != NULL);

	free(output);
	return 0;
}

static int test_editor_refresh_screen_hides_expired_message(void) {
	add_row("line one");
	add_row("line two");
	E.window_rows = 4;
	E.window_cols = 30;
	strncpy(E.statusmsg, "old message", sizeof(E.statusmsg) - 1);
	E.statusmsg[sizeof(E.statusmsg) - 1] = '\0';
	E.statusmsg_time = time(NULL) - 10;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_TRUE(strstr(output, "old message") == NULL);
	free(output);
	return 0;
}

static int test_editor_refresh_screen_updates_horizontal_scroll(void) {
	add_row("01234567890123456789");
	add_row("second");
	E.window_rows = 3;
	E.window_cols = 5;
	E.cy = 0;
	E.cx = 15;
	E.coloff = 0;

	size_t output_len = 0;
	char *output = refresh_screen_and_capture(&output_len);
	ASSERT_TRUE(output != NULL);
	ASSERT_EQ_INT(15, E.rx);
	ASSERT_TRUE(E.coloff > 0);
	free(output);
	return 0;
}

struct testCase {
	const char *name;
	int (*run)(void);
};

int main(void) {
	setlocale(LC_CTYPE, "");

	const struct testCase tests[] = {
		{"utf8_decode_valid_sequences", test_utf8_decode_valid_sequences},
		{"utf8_decode_invalid_sequences", test_utf8_decode_invalid_sequences},
		{"utf8_continuation_detection", test_utf8_continuation_detection},
		{"grapheme_extend_classification", test_grapheme_extend_classification},
		{"char_display_width_basics", test_char_display_width_basics},
		{"row_char_boundaries", test_row_char_boundaries},
		{"row_cluster_boundaries_combining", test_row_cluster_boundaries_combining},
		{"row_cluster_boundaries_zwj_sequence", test_row_cluster_boundaries_zwj_sequence},
		{"row_cluster_boundaries_regional_indicators", test_row_cluster_boundaries_regional_indicators},
		{"row_cx_to_rx_with_tabs", test_row_cx_to_rx_with_tabs},
		{"editor_update_row_expands_tabs", test_editor_update_row_expands_tabs},
		{"insert_and_delete_row_updates_dirty", test_insert_and_delete_row_updates_dirty},
		{"insert_and_delete_chars", test_insert_and_delete_chars},
		{"editor_insert_char_creates_initial_row", test_editor_insert_char_creates_initial_row},
		{"editor_insert_newline_splits_row", test_editor_insert_newline_splits_row},
		{"editor_insert_newline_at_row_start", test_editor_insert_newline_at_row_start},
		{"editor_del_char_cluster_and_merge", test_editor_del_char_cluster_and_merge},
		{"editor_rows_to_str", test_editor_rows_to_str},
		{"editor_open_reads_rows_and_clears_dirty", test_editor_open_reads_rows_and_clears_dirty},
		{"editor_save_writes_file_and_clears_dirty", test_editor_save_writes_file_and_clears_dirty},
		{"editor_save_prompts_for_filename", test_editor_save_prompts_for_filename},
		{"editor_prompt_accept_and_cancel", test_editor_prompt_accept_and_cancel},
		{"editor_read_key_sequences", test_editor_read_key_sequences},
		{"read_cursor_position_and_window_size_fallback", test_read_cursor_position_and_window_size_fallback},
		{"editor_process_keypress_insert_move_and_backspace",
			test_editor_process_keypress_insert_move_and_backspace},
		{"editor_process_keypress_delete_key", test_editor_process_keypress_delete_key},
		{"editor_process_keypress_ctrl_s_saves_file", test_editor_process_keypress_ctrl_s_saves_file},
		{"editor_refresh_screen_contains_expected_sequences",
			test_editor_refresh_screen_contains_expected_sequences},
		{"editor_refresh_screen_hides_expired_message", test_editor_refresh_screen_hides_expired_message},
		{"editor_refresh_screen_updates_horizontal_scroll",
			test_editor_refresh_screen_updates_horizontal_scroll},
	};

	int total = (int)(sizeof(tests) / sizeof(tests[0]));
	int passed = 0;

	for (int i = 0; i < total; i++) {
		reset_editor_state();
		int failed = tests[i].run();
		reset_editor_state();

		if (failed == 0) {
			passed++;
			printf("PASS %s\n", tests[i].name);
		} else {
			printf("FAIL %s\n", tests[i].name);
		}
	}

	printf("\n%d/%d tests passed\n", passed, total);
	return (passed == total) ? EXIT_SUCCESS : EXIT_FAILURE;
}
