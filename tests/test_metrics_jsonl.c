#include "metrics_jsonl.h"
#include "test_case.h"
#include "test_helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Treat a freshly-formatted row as a string. Frozen across tests to keep
 * regressions visible in diffs. */

static int starts_with(const char *s, const char *prefix) {
	return strncmp(s, prefix, strlen(prefix)) == 0;
}

static const char *env_returns_null(const char *name) {
	(void)name;
	return NULL;
}

static const char *env_canned(const char *name) {
	if (strcmp(name, "ROTIDE_METRICS_GIT_SHA") == 0) {
		return "abc1234";
	}
	if (strcmp(name, "ROTIDE_METRICS_CI_RUN_ID") == 0) {
		return "987654";
	}
	return NULL;
}

static int test_metrics_format_single_line_with_newline(void) {
	struct editorMetricsField fields[] = {
	        {"total_runs", EDITOR_METRICS_INT, .v.i = 12},
	};
	char buf[512];
	int n = editorMetricsFormatRow(buf, sizeof(buf), "test_run", 1747655696LL, env_returns_null,
	                               fields, 1);
	ASSERT_TRUE(n > 0 && (size_t)n < sizeof(buf));
	ASSERT_EQ_INT(n, (int)strlen(buf));
	ASSERT_TRUE(buf[n - 1] == '\n');
	/* exactly one newline, at the end */
	int newlines = 0;
	for (int i = 0; i < n; i++) {
		if (buf[i] == '\n') {
			newlines++;
		}
	}
	ASSERT_EQ_INT(1, newlines);
	return 0;
}

static int test_metrics_format_includes_kind_and_ts(void) {
	char buf[512];
	int n = editorMetricsFormatRow(buf, sizeof(buf), "bench", 1747655696LL, env_returns_null,
	                               NULL, 0);
	ASSERT_TRUE(n > 0);
	ASSERT_TRUE(starts_with(buf, "{\"kind\":\"bench\",\"ts\":\""));
	/* ISO 8601 UTC for 1747655696 = 2025-05-19T11:54:56Z */
	ASSERT_TRUE(strstr(buf, "\"ts\":\"2025-05-19T11:54:56Z\"") != NULL);
	return 0;
}

static int test_metrics_format_int_uint_double_bool_hex(void) {
	struct editorMetricsField fields[] = {
	        {"i", EDITOR_METRICS_INT, .v.i = -7},
	        {"u", EDITOR_METRICS_UINT64, .v.u = 9876543210ULL},
	        {"d", EDITOR_METRICS_DOUBLE, .v.d = 4.812},
	        {"b1", EDITOR_METRICS_BOOL, .v.b = 1},
	        {"b0", EDITOR_METRICS_BOOL, .v.b = 0},
	        {"seed", EDITOR_METRICS_HEX64, .v.u = 0x0123456789abcdefULL},
	};
	char buf[512];
	int n = editorMetricsFormatRow(buf, sizeof(buf), "k", 1747655696LL, env_returns_null,
	                               fields, 6);
	ASSERT_TRUE(n > 0);
	ASSERT_TRUE(strstr(buf, "\"i\":-7") != NULL);
	ASSERT_TRUE(strstr(buf, "\"u\":9876543210") != NULL);
	ASSERT_TRUE(strstr(buf, "\"d\":4.812") != NULL);
	ASSERT_TRUE(strstr(buf, "\"b1\":true") != NULL);
	ASSERT_TRUE(strstr(buf, "\"b0\":false") != NULL);
	ASSERT_TRUE(strstr(buf, "\"seed\":\"0x0123456789abcdef\"") != NULL);
	return 0;
}

static int test_metrics_format_escapes_special_chars(void) {
	struct editorMetricsField fields[] = {
	        /* Octal \001 instead of hex \x01 — hex escapes are greedy in C
	         * and would absorb the following 'c' into one byte 0x1c. */
	        {"name", EDITOR_METRICS_STR, .v.s = "quote\" back\\slash\nnewline\ttab\001ctrl"},
	};
	char buf[512];
	int n = editorMetricsFormatRow(buf, sizeof(buf), "k", 1747655696LL, env_returns_null,
	                               fields, 1);
	ASSERT_TRUE(n > 0);
	ASSERT_TRUE(strstr(buf, "\\\"") != NULL);
	ASSERT_TRUE(strstr(buf, "\\\\") != NULL);
	ASSERT_TRUE(strstr(buf, "\\n") != NULL);
	ASSERT_TRUE(strstr(buf, "\\t") != NULL);
	ASSERT_TRUE(strstr(buf, "\\u0001") != NULL);
	/* No raw control byte landed in the encoded value. */
	for (int i = 0; i < n; i++) {
		ASSERT_TRUE((unsigned char)buf[i] >= 0x20 || buf[i] == '\n');
	}
	return 0;
}

static int test_metrics_format_escapes_special_chars_in_key(void) {
	struct editorMetricsField fields[] = {
	        {"weird\"key", EDITOR_METRICS_INT, .v.i = 1},
	};
	char buf[512];
	int n = editorMetricsFormatRow(buf, sizeof(buf), "k", 1747655696LL, env_returns_null,
	                               fields, 1);
	ASSERT_TRUE(n > 0);
	ASSERT_TRUE(strstr(buf, "\"weird\\\"key\":1") != NULL);
	return 0;
}

static int test_metrics_format_skips_empty_keys(void) {
	struct editorMetricsField fields[] = {
	        {NULL, EDITOR_METRICS_INT, .v.i = 1},
	        {"", EDITOR_METRICS_INT, .v.i = 2},
	        {"keep", EDITOR_METRICS_INT, .v.i = 3},
	};
	char buf[512];
	int n = editorMetricsFormatRow(buf, sizeof(buf), "k", 1747655696LL, env_returns_null,
	                               fields, 3);
	ASSERT_TRUE(n > 0);
	ASSERT_TRUE(strstr(buf, "\"keep\":3") != NULL);
	/* Only one comma between metadata and the lone surviving field. */
	int commas_after_ts = 0;
	const char *p = strstr(buf, "\"ts\":");
	ASSERT_TRUE(p != NULL);
	for (; *p && *p != '\n'; p++) {
		if (*p == ',') {
			commas_after_ts++;
		}
	}
	ASSERT_EQ_INT(1, commas_after_ts);
	return 0;
}

static int test_metrics_format_env_enrichment_present(void) {
	char buf[512];
	int n = editorMetricsFormatRow(buf, sizeof(buf), "k", 1747655696LL, env_canned, NULL, 0);
	ASSERT_TRUE(n > 0);
	ASSERT_TRUE(strstr(buf, "\"git_sha\":\"abc1234\"") != NULL);
	ASSERT_TRUE(strstr(buf, "\"ci_run_id\":\"987654\"") != NULL);
	/* git_ref unset → omitted. */
	ASSERT_TRUE(strstr(buf, "\"git_ref\"") == NULL);
	return 0;
}

static int test_metrics_format_env_lookup_null_skips_all(void) {
	char buf[512];
	int n = editorMetricsFormatRow(buf, sizeof(buf), "k", 1747655696LL, NULL, NULL, 0);
	ASSERT_TRUE(n > 0);
	ASSERT_TRUE(strstr(buf, "\"git_sha\"") == NULL);
	ASSERT_TRUE(strstr(buf, "\"git_ref\"") == NULL);
	ASSERT_TRUE(strstr(buf, "\"ci_run_id\"") == NULL);
	return 0;
}

static int test_metrics_format_truncation_safe(void) {
	struct editorMetricsField fields[] = {
	        {"name", EDITOR_METRICS_STR, .v.s = "abcdefghij"},
	};
	char small[16];
	int n = editorMetricsFormatRow(small, sizeof(small), "k", 1747655696LL, env_returns_null,
	                               fields, 1);
	ASSERT_TRUE(n > (int)sizeof(small)); /* would have needed more */
	ASSERT_TRUE(small[sizeof(small) - 1] == '\0');
	return 0;
}

static char *temp_metrics_path(void) {
	const char *tmpdir = getenv("TMPDIR");
	if (tmpdir == NULL || tmpdir[0] == '\0') {
		tmpdir = "/tmp";
	}
	size_t need = strlen(tmpdir) + sizeof("/rotide-metrics-XXXXXX") + 1;
	char *path = (char *)malloc(need);
	if (path == NULL) {
		return NULL;
	}
	snprintf(path, need, "%s/rotide-metrics-XXXXXX", tmpdir);
	int fd = mkstemp(path);
	if (fd < 0) {
		free(path);
		return NULL;
	}
	(void)close(fd);
	/* mkstemp leaves an empty file; the helper should append to it. */
	return path;
}

static int test_metrics_append_writes_two_lines(void) {
	char *path = temp_metrics_path();
	ASSERT_TRUE(path != NULL);

	struct editorMetricsField a[] = {{"x", EDITOR_METRICS_INT, .v.i = 1}};
	struct editorMetricsField b[] = {{"x", EDITOR_METRICS_INT, .v.i = 2}};
	ASSERT_EQ_INT(0, editorMetricsAppend(path, "k", a, 1));
	ASSERT_EQ_INT(0, editorMetricsAppend(path, "k", b, 1));

	size_t len = 0;
	char *content = read_file_contents(path, &len);
	ASSERT_TRUE(content != NULL);
	int newlines = 0;
	for (size_t i = 0; i < len; i++) {
		if (content[i] == '\n') {
			newlines++;
		}
	}
	ASSERT_EQ_INT(2, newlines);
	ASSERT_TRUE(strstr(content, "\"x\":1") != NULL);
	ASSERT_TRUE(strstr(content, "\"x\":2") != NULL);

	free(content);
	(void)unlink(path);
	free(path);
	return 0;
}

static int test_metrics_append_io_failure_returns_negative(void) {
	struct editorMetricsField f[] = {{"x", EDITOR_METRICS_INT, .v.i = 1}};
	/* A path inside a non-existent directory cannot be created. */
	int rc = editorMetricsAppend("/nonexistent-rotide-dir-xyz/m.jsonl", "k", f, 1);
	ASSERT_TRUE(rc != 0);
	rc = editorMetricsAppend(NULL, "k", f, 1);
	ASSERT_TRUE(rc != 0);
	rc = editorMetricsAppend("", "k", f, 1);
	ASSERT_TRUE(rc != 0);
	return 0;
}

const struct editorTestCase g_metrics_jsonl_tests[] = {
        {"metrics_format_single_line_with_newline", test_metrics_format_single_line_with_newline},
        {"metrics_format_includes_kind_and_ts", test_metrics_format_includes_kind_and_ts},
        {"metrics_format_int_uint_double_bool_hex", test_metrics_format_int_uint_double_bool_hex},
        {"metrics_format_escapes_special_chars", test_metrics_format_escapes_special_chars},
        {"metrics_format_escapes_special_chars_in_key",
         test_metrics_format_escapes_special_chars_in_key},
        {"metrics_format_skips_empty_keys", test_metrics_format_skips_empty_keys},
        {"metrics_format_env_enrichment_present", test_metrics_format_env_enrichment_present},
        {"metrics_format_env_lookup_null_skips_all", test_metrics_format_env_lookup_null_skips_all},
        {"metrics_format_truncation_safe", test_metrics_format_truncation_safe},
        {"metrics_append_writes_two_lines", test_metrics_append_writes_two_lines},
        {"metrics_append_io_failure_returns_negative",
         test_metrics_append_io_failure_returns_negative},
};

const int g_metrics_jsonl_test_count =
        (int)(sizeof(g_metrics_jsonl_tests) / sizeof(g_metrics_jsonl_tests[0]));
