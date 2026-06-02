#include "config/common.h"
#include "test_case.h"
#include "test_helpers.h"
#include "test_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct scanRecorder {
	const char *select_table; /* on_section opts into this table (NULL = none) */
	const char *reject_key;   /* on_entry rejects this key (NULL = accept all) */
	int section_count;
	int entry_count;
	char last_table[64];
	char keys[16][64];
	char values[16][128];
};

static int recorderOnSection(void *ctx, const char *table) {
	struct scanRecorder *rec = ctx;
	if (table[0] != '\0') { /* "" is the implicit pre-section region, not a real header */
		rec->section_count++;
		(void)snprintf(rec->last_table, sizeof(rec->last_table), "%s", table);
	}
	return rec->select_table != NULL && strcmp(table, rec->select_table) == 0;
}

static int recorderOnEntry(void *ctx, const char *key, char *value) {
	struct scanRecorder *rec = ctx;
	if (rec->reject_key != NULL && strcmp(key, rec->reject_key) == 0) {
		return 0;
	}
	if (rec->entry_count < (int)(sizeof(rec->keys) / sizeof(rec->keys[0]))) {
		(void)snprintf(rec->keys[rec->entry_count], sizeof(rec->keys[0]), "%s", key);
		(void)snprintf(rec->values[rec->entry_count], sizeof(rec->values[0]), "%s", value);
	}
	rec->entry_count++;
	return 1;
}

/* Write `content` to a fresh temp file, scan it, then remove it. Returns the
 * scan status as int, or -1 if the temp file could not be set up. */
static int scan_text(const char *content, const struct editorConfigScanner *scanner, void *ctx) {
	char dir_template[] = "/tmp/rotide-test-config-scan-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	if (dir_path == NULL) {
		return -1;
	}
	char file_path[512];
	if (!path_join(file_path, sizeof(file_path), dir_path, "config.toml")) {
		(void)rmdir(dir_path);
		return -1;
	}
	if (!write_text_file(file_path, content)) {
		(void)rmdir(dir_path);
		return -1;
	}
	enum editorConfigScanStatus status = editorConfigScanFile(file_path, scanner, ctx);
	(void)unlink(file_path);
	(void)rmdir(dir_path);
	return (int)status;
}

static int test_config_scan_missing_file_returns_missing(void) {
	char dir_template[] = "/tmp/rotide-test-config-scan-missing-XXXXXX";
	char *dir_path = mkdtemp(dir_template);
	ASSERT_TRUE(dir_path != NULL);
	char file_path[512];
	ASSERT_TRUE(path_join(file_path, sizeof(file_path), dir_path, "absent.toml"));

	struct scanRecorder rec = {0};
	struct editorConfigScanner scanner = {recorderOnSection, recorderOnEntry};
	enum editorConfigScanStatus status = editorConfigScanFile(file_path, &scanner, &rec);

	ASSERT_EQ_INT(EDITOR_CONFIG_SCAN_MISSING, status);
	ASSERT_TRUE(rmdir(dir_path) == 0);
	return 0;
}

static int test_config_scan_blank_and_comment_lines_yield_no_entries(void) {
	struct scanRecorder rec = {0};
	struct editorConfigScanner scanner = {recorderOnSection, recorderOnEntry};
	int status = scan_text("\n   \n# a comment\n\t# indented comment\n", &scanner, &rec);
	ASSERT_EQ_INT(EDITOR_CONFIG_SCAN_OK, status);
	ASSERT_EQ_INT(0, rec.section_count);
	ASSERT_EQ_INT(0, rec.entry_count);
	return 0;
}

static int test_config_scan_collects_entries_from_selected_section(void) {
	struct scanRecorder rec = {0};
	rec.select_table = "keymap";
	struct editorConfigScanner scanner = {recorderOnSection, recorderOnEntry};
	int status = scan_text("[keymap]\nsave = \"ctrl+s\"\nquit = \"ctrl+q\"\n", &scanner, &rec);
	ASSERT_EQ_INT(EDITOR_CONFIG_SCAN_OK, status);
	ASSERT_EQ_INT(1, rec.section_count);
	ASSERT_EQ_INT(2, rec.entry_count);
	ASSERT_EQ_STR("save", rec.keys[0]);
	ASSERT_EQ_STR("\"ctrl+s\"", rec.values[0]);
	ASSERT_EQ_STR("quit", rec.keys[1]);
	ASSERT_EQ_STR("\"ctrl+q\"", rec.values[1]);
	return 0;
}

static int test_config_scan_trims_key_and_value_and_strips_inline_comment(void) {
	struct scanRecorder rec = {0};
	rec.select_table = "editor";
	struct editorConfigScanner scanner = {recorderOnSection, recorderOnEntry};
	int status = scan_text("[  editor  ]\n   tab_width   =   4   # inline\n", &scanner, &rec);
	ASSERT_EQ_INT(EDITOR_CONFIG_SCAN_OK, status);
	ASSERT_EQ_STR("editor", rec.last_table);
	ASSERT_EQ_INT(1, rec.entry_count);
	ASSERT_EQ_STR("tab_width", rec.keys[0]);
	ASSERT_EQ_STR("4", rec.values[0]);
	return 0;
}

static int test_config_scan_skips_unselected_section_without_validating(void) {
	struct scanRecorder rec = {0};
	rec.select_table = "keymap";
	struct editorConfigScanner scanner = {recorderOnSection, recorderOnEntry};
	/* The line without '=' lives under an unselected section and must be ignored,
	 * not treated as malformed. */
	int status = scan_text("[other]\nthis line has no equals\n[keymap]\nsave = \"ctrl+s\"\n",
	                       &scanner, &rec);
	ASSERT_EQ_INT(EDITOR_CONFIG_SCAN_OK, status);
	ASSERT_EQ_INT(2, rec.section_count);
	ASSERT_EQ_INT(1, rec.entry_count);
	ASSERT_EQ_STR("save", rec.keys[0]);
	return 0;
}

static int test_config_scan_unclosed_section_is_malformed(void) {
	struct scanRecorder rec = {0};
	struct editorConfigScanner scanner = {recorderOnSection, recorderOnEntry};
	int status = scan_text("[keymap\n", &scanner, &rec);
	ASSERT_EQ_INT(EDITOR_CONFIG_SCAN_MALFORMED, status);
	return 0;
}

static int test_config_scan_trailing_junk_after_section_is_malformed(void) {
	struct scanRecorder rec = {0};
	struct editorConfigScanner scanner = {recorderOnSection, recorderOnEntry};
	int status = scan_text("[keymap] junk\n", &scanner, &rec);
	ASSERT_EQ_INT(EDITOR_CONFIG_SCAN_MALFORMED, status);
	return 0;
}

static int test_config_scan_entry_without_equals_is_malformed(void) {
	struct scanRecorder rec = {0};
	rec.select_table = "keymap";
	struct editorConfigScanner scanner = {recorderOnSection, recorderOnEntry};
	int status = scan_text("[keymap]\nnoequals\n", &scanner, &rec);
	ASSERT_EQ_INT(EDITOR_CONFIG_SCAN_MALFORMED, status);
	return 0;
}

static int test_config_scan_empty_key_is_malformed(void) {
	struct scanRecorder rec = {0};
	rec.select_table = "keymap";
	struct editorConfigScanner scanner = {recorderOnSection, recorderOnEntry};
	int status = scan_text("[keymap]\n = \"ctrl+s\"\n", &scanner, &rec);
	ASSERT_EQ_INT(EDITOR_CONFIG_SCAN_MALFORMED, status);
	return 0;
}

static int test_config_scan_entry_callback_rejection_is_malformed(void) {
	struct scanRecorder rec = {0};
	rec.select_table = "keymap";
	rec.reject_key = "bad";
	struct editorConfigScanner scanner = {recorderOnSection, recorderOnEntry};
	int status = scan_text("[keymap]\nsave = \"ctrl+s\"\nbad = \"x\"\n", &scanner, &rec);
	ASSERT_EQ_INT(EDITOR_CONFIG_SCAN_MALFORMED, status);
	ASSERT_EQ_INT(1, rec.entry_count); /* save accepted before bad rejected */
	return 0;
}

static int test_config_scan_overlong_line_is_malformed(void) {
	char content[1200];
	int prefix = snprintf(content, sizeof(content), "[keymap]\nkey = ");
	ASSERT_TRUE(prefix > 0 && prefix < (int)sizeof(content));
	memset(content + prefix, 'a', 1100);
	content[prefix + 1100] = '\0';

	struct scanRecorder rec = {0};
	rec.select_table = "keymap";
	struct editorConfigScanner scanner = {recorderOnSection, recorderOnEntry};
	int status = scan_text(content, &scanner, &rec);
	ASSERT_EQ_INT(EDITOR_CONFIG_SCAN_MALFORMED, status);
	return 0;
}

static int test_config_scan_delivers_top_of_file_entries_when_selected(void) {
	struct scanRecorder rec = {0};
	rec.select_table = ""; /* opt into the pre-section region */
	struct editorConfigScanner scanner = {recorderOnSection, recorderOnEntry};
	int status = scan_text("top = \"1\"\n[other]\nx = \"2\"\n", &scanner, &rec);
	ASSERT_EQ_INT(EDITOR_CONFIG_SCAN_OK, status);
	ASSERT_EQ_INT(1, rec.entry_count);
	ASSERT_EQ_STR("top", rec.keys[0]);
	ASSERT_EQ_STR("\"1\"", rec.values[0]);
	return 0;
}

static int test_config_scan_skips_top_of_file_entries_when_not_selected(void) {
	struct scanRecorder rec = {0};
	rec.select_table = "keymap";
	struct editorConfigScanner scanner = {recorderOnSection, recorderOnEntry};
	int status = scan_text("pre = \"1\"\n[keymap]\nsave = \"ctrl+s\"\n", &scanner, &rec);
	ASSERT_EQ_INT(EDITOR_CONFIG_SCAN_OK, status);
	ASSERT_EQ_INT(1, rec.entry_count);
	ASSERT_EQ_STR("save", rec.keys[0]);
	return 0;
}

static int test_config_scan_null_on_section_selects_nothing(void) {
	struct scanRecorder rec = {0};
	struct editorConfigScanner scanner = {NULL, recorderOnEntry};
	int status = scan_text("[keymap]\nsave = \"ctrl+s\"\n", &scanner, &rec);
	ASSERT_EQ_INT(EDITOR_CONFIG_SCAN_OK, status);
	ASSERT_EQ_INT(0, rec.entry_count);
	return 0;
}

static int test_config_scan_null_on_entry_accepts_well_formed_lines(void) {
	struct scanRecorder rec = {0};
	rec.select_table = "keymap";
	struct editorConfigScanner scanner = {recorderOnSection, NULL};
	int status = scan_text("[keymap]\nsave = \"ctrl+s\"\n", &scanner, &rec);
	ASSERT_EQ_INT(EDITOR_CONFIG_SCAN_OK, status);
	ASSERT_EQ_INT(0, rec.entry_count);
	return 0;
}

const struct editorTestCase g_config_scan_tests[] = {
        {"config_scan_missing_file_returns_missing", test_config_scan_missing_file_returns_missing},
        {"config_scan_blank_and_comment_lines_yield_no_entries",
         test_config_scan_blank_and_comment_lines_yield_no_entries},
        {"config_scan_collects_entries_from_selected_section",
         test_config_scan_collects_entries_from_selected_section},
        {"config_scan_trims_key_and_value_and_strips_inline_comment",
         test_config_scan_trims_key_and_value_and_strips_inline_comment},
        {"config_scan_skips_unselected_section_without_validating",
         test_config_scan_skips_unselected_section_without_validating},
        {"config_scan_unclosed_section_is_malformed",
         test_config_scan_unclosed_section_is_malformed},
        {"config_scan_trailing_junk_after_section_is_malformed",
         test_config_scan_trailing_junk_after_section_is_malformed},
        {"config_scan_entry_without_equals_is_malformed",
         test_config_scan_entry_without_equals_is_malformed},
        {"config_scan_empty_key_is_malformed", test_config_scan_empty_key_is_malformed},
        {"config_scan_entry_callback_rejection_is_malformed",
         test_config_scan_entry_callback_rejection_is_malformed},
        {"config_scan_overlong_line_is_malformed", test_config_scan_overlong_line_is_malformed},
        {"config_scan_delivers_top_of_file_entries_when_selected",
         test_config_scan_delivers_top_of_file_entries_when_selected},
        {"config_scan_skips_top_of_file_entries_when_not_selected",
         test_config_scan_skips_top_of_file_entries_when_not_selected},
        {"config_scan_null_on_section_selects_nothing",
         test_config_scan_null_on_section_selects_nothing},
        {"config_scan_null_on_entry_accepts_well_formed_lines",
         test_config_scan_null_on_entry_accepts_well_formed_lines},
};

const int g_config_scan_test_count =
        (int)(sizeof(g_config_scan_tests) / sizeof(g_config_scan_tests[0]));
