#ifndef TESTS_METRICS_JSONL_H
#define TESTS_METRICS_JSONL_H

#include <stddef.h>

/* One row per call, appended to `path` in JSON-Lines format. The row is
 * one line: `{"kind":"<kind>","ts":"<iso8601>",<env_enrichment>,<fields>}\n`.
 *
 * Env enrichment fields are added when set (string values only):
 *   ROTIDE_METRICS_GIT_SHA   -> git_sha
 *   ROTIDE_METRICS_GIT_REF   -> git_ref
 *   ROTIDE_METRICS_CI_RUN_ID -> ci_run_id
 *
 * Append is via O_APPEND so concurrent producers on the same host don't
 * tear lines as long as each row fits in PIPE_BUF (~4 KiB on Linux), which
 * the schema honours.
 */

enum editorMetricsType {
	EDITOR_METRICS_STR,    /* v.s, JSON-escaped */
	EDITOR_METRICS_INT,    /* v.i, signed long long */
	EDITOR_METRICS_UINT64, /* v.u, unsigned long long */
	EDITOR_METRICS_DOUBLE, /* v.d */
	EDITOR_METRICS_BOOL,   /* v.b, rendered as true/false */
	EDITOR_METRICS_HEX64,  /* v.u, rendered as "0x%016llx" string */
};

struct editorMetricsField {
	const char *key;
	enum editorMetricsType type;
	union {
		const char *s;
		long long i;
		unsigned long long u;
		double d;
		int b;
	} v;
};

/* Returns 0 on success, -1 on I/O failure. */
int editorMetricsAppend(const char *path, const char *kind, const struct editorMetricsField *fields,
                        int field_count);

/* Format a single row into `buf` (NUL-terminated, with trailing newline)
 * without touching the filesystem. `env_lookup` is consulted for enrichment
 * env vars; pass NULL to skip enrichment.
 *
 * `now_unix` is the epoch second to embed as `ts` (UTC ISO 8601). Pass 0
 * to call the system clock.
 *
 * Returns the number of bytes that would be written (excluding the trailing
 * NUL), like snprintf. If the return value >= buf_size the output was
 * truncated.
 */
int editorMetricsFormatRow(char *buf, size_t buf_size, const char *kind, long long now_unix,
                           const char *(*env_lookup)(const char *name),
                           const struct editorMetricsField *fields, int field_count);

#endif
