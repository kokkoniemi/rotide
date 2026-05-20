#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include "metrics_jsonl.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* JSON-escape src into dst. Returns the number of bytes that would be
 * written excluding the NUL, snprintf-style. Truncates safely if dst_size
 * is too small.
 */
static int json_escape(char *dst, size_t dst_size, const char *src) {
	size_t need = 0;
	size_t pos = 0;
	if (dst_size == 0) {
		dst = NULL;
	}
#define EMIT_RAW(c)                                                                                \
	do {                                                                                       \
		if (dst != NULL && pos + 1 < dst_size)                                             \
			dst[pos] = (char)(c);                                                      \
		pos++;                                                                             \
		need++;                                                                            \
	} while (0)
#define EMIT_STR(s, len)                                                                           \
	do {                                                                                       \
		for (size_t _i = 0; _i < (size_t)(len); _i++) {                                    \
			EMIT_RAW((s)[_i]);                                                         \
		}                                                                                  \
	} while (0)

	for (const unsigned char *p = (const unsigned char *)src; *p != '\0'; p++) {
		unsigned char c = *p;
		switch (c) {
			case '"':
				EMIT_STR("\\\"", 2);
				break;
			case '\\':
				EMIT_STR("\\\\", 2);
				break;
			case '\b':
				EMIT_STR("\\b", 2);
				break;
			case '\f':
				EMIT_STR("\\f", 2);
				break;
			case '\n':
				EMIT_STR("\\n", 2);
				break;
			case '\r':
				EMIT_STR("\\r", 2);
				break;
			case '\t':
				EMIT_STR("\\t", 2);
				break;
			default:
				if (c < 0x20) {
					char buf[8];
					int n = snprintf(buf, sizeof(buf), "\\u%04x", c);
					EMIT_STR(buf, n);
				} else {
					EMIT_RAW(c);
				}
				break;
		}
	}
#undef EMIT_RAW
#undef EMIT_STR

	if (dst != NULL && dst_size > 0) {
		size_t terminate_at = pos < dst_size ? pos : dst_size - 1;
		dst[terminate_at] = '\0';
	}
	return (int)need;
}

/* Append to *off into buf with snprintf semantics, advancing *off even when
 * truncation occurred so the caller can detect overflow via *off >= buf_size.
 */
static void append_fmt(char *buf, size_t buf_size, size_t *off, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int remaining = (int)(*off < buf_size ? buf_size - *off : 0);
	int n = vsnprintf(remaining > 0 ? buf + *off : NULL, (size_t)remaining, fmt, ap);
	va_end(ap);
	if (n < 0) {
		return;
	}
	*off += (size_t)n;
}

static void append_str(char *buf, size_t buf_size, size_t *off, const char *s, size_t len) {
	for (size_t i = 0; i < len; i++) {
		if (*off + 1 < buf_size) {
			buf[*off] = s[i];
		}
		(*off)++;
	}
	if (*off < buf_size) {
		buf[*off] = '\0';
	}
}

static void append_escaped_string_value(char *buf, size_t buf_size, size_t *off, const char *s) {
	append_str(buf, buf_size, off, "\"", 1);
	size_t pos = *off;
	int n = json_escape(pos < buf_size ? buf + pos : NULL, pos < buf_size ? buf_size - pos : 0,
	                    s);
	*off += (size_t)n;
	if (*off < buf_size) {
		buf[*off] = '\0';
	}
	append_str(buf, buf_size, off, "\"", 1);
}

static void append_field_value(char *buf, size_t buf_size, size_t *off,
                               const struct editorMetricsField *f) {
	switch (f->type) {
		case EDITOR_METRICS_STR:
			append_escaped_string_value(buf, buf_size, off, f->v.s ? f->v.s : "");
			break;
		case EDITOR_METRICS_INT:
			append_fmt(buf, buf_size, off, "%lld", f->v.i);
			break;
		case EDITOR_METRICS_UINT64:
			append_fmt(buf, buf_size, off, "%llu", f->v.u);
			break;
		case EDITOR_METRICS_DOUBLE:
			/* %.6g loses precision for nanosecond percentiles; %.6f wastes
			 * digits for tiny values. %.9g keeps full sample precision for
			 * the bench numbers (typical range 10..1e8 ns) without trailing
			 * zeros, and is round-trip safe for the chart script. */
			append_fmt(buf, buf_size, off, "%.9g", f->v.d);
			break;
		case EDITOR_METRICS_BOOL:
			append_str(buf, buf_size, off, f->v.b ? "true" : "false", f->v.b ? 4 : 5);
			break;
		case EDITOR_METRICS_HEX64:
			append_fmt(buf, buf_size, off, "\"0x%016llx\"", f->v.u);
			break;
	}
}

static const char *getenv_default(const char *name) {
	return getenv(name);
}

int editorMetricsFormatRow(char *buf, size_t buf_size, const char *kind, long long now_unix,
                           const char *(*env_lookup)(const char *name),
                           const struct editorMetricsField *fields, int field_count) {
	if (kind == NULL) {
		kind = "";
	}
	size_t off = 0;

	append_str(buf, buf_size, &off, "{", 1);

	append_str(buf, buf_size, &off, "\"kind\":", 7);
	append_escaped_string_value(buf, buf_size, &off, kind);

	time_t t = now_unix > 0 ? (time_t)now_unix : time(NULL);
	struct tm tm;
	gmtime_r(&t, &tm);
	char ts[32];
	strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tm);
	append_str(buf, buf_size, &off, ",\"ts\":", 6);
	append_escaped_string_value(buf, buf_size, &off, ts);

	if (env_lookup != NULL) {
		struct {
			const char *env;
			const char *key;
		} pairs[] = {
		        {"ROTIDE_METRICS_GIT_SHA", "git_sha"},
		        {"ROTIDE_METRICS_GIT_REF", "git_ref"},
		        {"ROTIDE_METRICS_CI_RUN_ID", "ci_run_id"},
		};
		for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
			const char *v = env_lookup(pairs[i].env);
			if (v == NULL || v[0] == '\0') {
				continue;
			}
			append_str(buf, buf_size, &off, ",\"", 2);
			append_str(buf, buf_size, &off, pairs[i].key, strlen(pairs[i].key));
			append_str(buf, buf_size, &off, "\":", 2);
			append_escaped_string_value(buf, buf_size, &off, v);
		}
	}

	for (int i = 0; i < field_count; i++) {
		const struct editorMetricsField *f = &fields[i];
		if (f->key == NULL || f->key[0] == '\0') {
			continue;
		}
		append_str(buf, buf_size, &off, ",", 1);
		append_escaped_string_value(buf, buf_size, &off, f->key);
		append_str(buf, buf_size, &off, ":", 1);
		append_field_value(buf, buf_size, &off, f);
	}

	append_str(buf, buf_size, &off, "}\n", 2);

	if (off < buf_size) {
		buf[off] = '\0';
	} else if (buf_size > 0) {
		buf[buf_size - 1] = '\0';
	}
	return (int)off;
}

int editorMetricsAppend(const char *path, const char *kind, const struct editorMetricsField *fields,
                        int field_count) {
	if (path == NULL || path[0] == '\0') {
		return -1;
	}
	/* 2 KiB stays well under PIPE_BUF (4 KiB on Linux) so concurrent
	 * O_APPEND writers don't tear lines. The schema is far smaller than
	 * this in practice; if a row ever needs more, the format helper
	 * truncates and we refuse to write a malformed row. */
	char buf[2048];
	int written = editorMetricsFormatRow(buf, sizeof(buf), kind, 0, getenv_default, fields,
	                                     field_count);
	if (written <= 0 || (size_t)written >= sizeof(buf)) {
		return -1;
	}

	int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
	if (fd < 0) {
		return -1;
	}
	const char *p = buf;
	size_t remaining = (size_t)written;
	while (remaining > 0) {
		ssize_t n = write(fd, p, remaining);
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			(void)close(fd);
			return -1;
		}
		p += n;
		remaining -= (size_t)n;
	}
	(void)close(fd);
	return 0;
}
