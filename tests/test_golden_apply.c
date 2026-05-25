#define _GNU_SOURCE

#include "golden_apply_lib.h"
#include "grid_snapshot_format.h"
#include "test_case.h"
#include "test_helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *emit_to_string(const char *text, const char *indent) {
	char *buf = NULL;
	size_t len = 0;
	FILE *m = open_memstream(&buf, &len);
	if (m == NULL) {
		return NULL;
	}
	editor_grid_snapshot_emit_c_string(text, indent, m);
	(void)fclose(m);
	return buf;
}

static int test_emit_c_string_single_line(void) {
	char *got = emit_to_string("hello", "");
	ASSERT_TRUE(got != NULL);
	ASSERT_EQ_STR("\"hello\"\n", got);
	free(got);
	return 0;
}

static int test_emit_c_string_splits_at_newline(void) {
	char *got = emit_to_string("a\nb\n", "\t");
	ASSERT_TRUE(got != NULL);
	ASSERT_EQ_STR("\t\"a\\n\"\n\t\"b\\n\"\n", got);
	free(got);
	return 0;
}

static int test_emit_c_string_escapes_quote_and_backslash(void) {
	char *got = emit_to_string("a\"b\\c", "");
	ASSERT_TRUE(got != NULL);
	ASSERT_EQ_STR("\"a\\\"b\\\\c\"\n", got);
	free(got);
	return 0;
}

static int test_emit_c_string_escapes_control_bytes(void) {
	char *got = emit_to_string("\001\t", "");
	ASSERT_TRUE(got != NULL);
	/* \001 → \001 (octal), \t → \t (already special-cased). */
	ASSERT_EQ_STR("\"\\001\\t\"\n", got);
	free(got);
	return 0;
}

static int test_emit_c_string_no_trailing_newline_keeps_open_segment(void) {
	char *got = emit_to_string("ab\ncd", "");
	ASSERT_TRUE(got != NULL);
	ASSERT_EQ_STR("\"ab\\n\"\n\"cd\"\n", got);
	free(got);
	return 0;
}

static int test_emit_c_string_empty_input(void) {
	char *got = emit_to_string("", "  ");
	ASSERT_TRUE(got != NULL);
	ASSERT_EQ_STR("  \"\"\n", got);
	free(got);
	return 0;
}

static int test_parse_stash_row_well_formed(void) {
	const char *line = "{\"file\":\"tests/foo.c\",\"line\":42,\"actual\":\"a\\nb\\n\"}";
	struct goldenStashEntry e;
	ASSERT_EQ_INT(1, editor_golden_parse_stash_line(line, &e));
	ASSERT_EQ_STR("tests/foo.c", e.file);
	ASSERT_EQ_INT(42, e.line);
	ASSERT_EQ_STR("a\nb\n", e.actual);
	free(e.actual);
	return 0;
}

static int test_parse_stash_row_rejects_missing_keys(void) {
	struct goldenStashEntry e;
	ASSERT_EQ_INT(0, editor_golden_parse_stash_line("{\"line\":1,\"actual\":\"x\"}", &e));
	ASSERT_EQ_INT(0, editor_golden_parse_stash_line("{\"file\":\"x\",\"actual\":\"x\"}", &e));
	ASSERT_EQ_INT(0, editor_golden_parse_stash_line("{\"file\":\"x\",\"line\":1}", &e));
	return 0;
}

static int test_parse_stash_row_unicode_control_escape(void) {
	const char *line = "{\"file\":\"f\",\"line\":1,\"actual\":\"a\\u0001b\"}";
	struct goldenStashEntry e;
	ASSERT_EQ_INT(1, editor_golden_parse_stash_line(line, &e));
	ASSERT_EQ_STR("a\x01"
	              "b",
	              e.actual);
	free(e.actual);
	return 0;
}

static int test_rewrite_replaces_block_with_new_content(void) {
	const char *src = "int foo(void) {\n"
	                  "\tASSERT_GRID_EQ(\n"
	                  "\t\t/* golden-start */\n"
	                  "\t\t\"old line\\n\"\n"
	                  "\t\t/* golden-end */\n"
	                  "\t);\n"
	                  "}\n";

	struct goldenStashEntry e = {0};
	snprintf(e.file, sizeof(e.file), "%s", "fake.c");
	e.line = 2; /* ASSERT_GRID_EQ call site */
	e.actual = strdup("new line 1\nnew line 2\n");

	int applied = 0;
	int skipped = 0;
	char *out = editor_golden_rewrite_text(src, strlen(src), &e, 1, &applied, &skipped, NULL);
	ASSERT_TRUE(out != NULL);
	ASSERT_EQ_INT(1, applied);
	ASSERT_EQ_INT(0, skipped);

	ASSERT_TRUE(strstr(out, "/* golden-start */") != NULL);
	ASSERT_TRUE(strstr(out, "/* golden-end */") != NULL);
	ASSERT_TRUE(strstr(out, "\"new line 1\\n\"") != NULL);
	ASSERT_TRUE(strstr(out, "\"new line 2\\n\"") != NULL);
	ASSERT_TRUE(strstr(out, "\"old line\\n\"") == NULL);

	free(e.actual);
	free(out);
	return 0;
}

static int test_rewrite_preserves_indentation_of_start_marker(void) {
	const char *src = "\tASSERT_GRID_EQ(\n"
	                  "\t    /* golden-start */\n"
	                  "\t    \"x\"\n"
	                  "\t    /* golden-end */\n"
	                  "\t);\n";
	struct goldenStashEntry e = {0};
	snprintf(e.file, sizeof(e.file), "fake.c");
	e.line = 1;
	e.actual = strdup("y\n");

	char *out = editor_golden_rewrite_text(src, strlen(src), &e, 1, NULL, NULL, NULL);
	ASSERT_TRUE(out != NULL);
	/* New literal line must carry the same `\t    ` prefix as the
	 * golden-start line. */
	ASSERT_TRUE(strstr(out, "\t    \"y\\n\"\n") != NULL);
	free(e.actual);
	free(out);
	return 0;
}

static int test_rewrite_skips_entry_without_markers(void) {
	const char *src = "int foo(void) {\n"
	                  "\tASSERT_GRID_EQ(\"plain\");\n"
	                  "}\n";
	struct goldenStashEntry e = {0};
	snprintf(e.file, sizeof(e.file), "fake.c");
	e.line = 2;
	e.actual = strdup("new");

	FILE *log = tmpfile();
	int applied = 0;
	int skipped = 0;
	char *out = editor_golden_rewrite_text(src, strlen(src), &e, 1, &applied, &skipped, log);
	ASSERT_TRUE(out != NULL);
	ASSERT_EQ_INT(0, applied);
	ASSERT_EQ_INT(1, skipped);
	ASSERT_EQ_STR(src, out); /* unchanged */
	(void)fclose(log);

	free(e.actual);
	free(out);
	return 0;
}

static int test_rewrite_applies_multiple_in_order(void) {
	const char *src = "static int a(void) {\n"
	                  "\tASSERT_GRID_EQ(\n"
	                  "\t\t/* golden-start */\n"
	                  "\t\t\"a_old\"\n"
	                  "\t\t/* golden-end */\n"
	                  "\t);\n"
	                  "}\n"
	                  "static int b(void) {\n"
	                  "\tASSERT_GRID_EQ(\n"
	                  "\t\t/* golden-start */\n"
	                  "\t\t\"b_old\"\n"
	                  "\t\t/* golden-end */\n"
	                  "\t);\n"
	                  "}\n";

	struct goldenStashEntry entries[2];
	memset(entries, 0, sizeof(entries));
	snprintf(entries[0].file, sizeof(entries[0].file), "fake.c");
	entries[0].line = 2;
	entries[0].actual = strdup("a_new\n");
	snprintf(entries[1].file, sizeof(entries[1].file), "fake.c");
	entries[1].line = 9;
	entries[1].actual = strdup("b_new\n");

	int applied = 0;
	int skipped = 0;
	char *out =
	        editor_golden_rewrite_text(src, strlen(src), entries, 2, &applied, &skipped, NULL);
	ASSERT_TRUE(out != NULL);
	ASSERT_EQ_INT(2, applied);
	ASSERT_EQ_INT(0, skipped);
	ASSERT_TRUE(strstr(out, "\"a_new\\n\"") != NULL);
	ASSERT_TRUE(strstr(out, "\"b_new\\n\"") != NULL);
	ASSERT_TRUE(strstr(out, "\"a_old\"") == NULL);
	ASSERT_TRUE(strstr(out, "\"b_old\"") == NULL);

	free(entries[0].actual);
	free(entries[1].actual);
	free(out);
	return 0;
}

const struct editorTestCase g_golden_apply_tests[] = {
        {"emit_c_string_single_line", test_emit_c_string_single_line},
        {"emit_c_string_splits_at_newline", test_emit_c_string_splits_at_newline},
        {"emit_c_string_escapes_quote_and_backslash",
         test_emit_c_string_escapes_quote_and_backslash},
        {"emit_c_string_escapes_control_bytes", test_emit_c_string_escapes_control_bytes},
        {"emit_c_string_no_trailing_newline_keeps_open_segment",
         test_emit_c_string_no_trailing_newline_keeps_open_segment},
        {"emit_c_string_empty_input", test_emit_c_string_empty_input},
        {"parse_stash_row_well_formed", test_parse_stash_row_well_formed},
        {"parse_stash_row_rejects_missing_keys", test_parse_stash_row_rejects_missing_keys},
        {"parse_stash_row_unicode_control_escape", test_parse_stash_row_unicode_control_escape},
        {"rewrite_replaces_block_with_new_content", test_rewrite_replaces_block_with_new_content},
        {"rewrite_preserves_indentation_of_start_marker",
         test_rewrite_preserves_indentation_of_start_marker},
        {"rewrite_skips_entry_without_markers", test_rewrite_skips_entry_without_markers},
        {"rewrite_applies_multiple_in_order", test_rewrite_applies_multiple_in_order},
};

const int g_golden_apply_test_count =
        (int)(sizeof(g_golden_apply_tests) / sizeof(g_golden_apply_tests[0]));
