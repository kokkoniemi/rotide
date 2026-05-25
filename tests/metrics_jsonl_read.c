#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include "metrics_jsonl_read.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void editorMetricsRowInit(struct editorMetricsRow *row) {
	if (row == NULL) {
		return;
	}
	memset(row, 0, sizeof(*row));
}

const char *editorMetricsKindName(enum editorMetricsKind k) {
	switch (k) {
		case EDITOR_METRICS_KIND_TEST_RUN:
			return "test_run";
		case EDITOR_METRICS_KIND_BENCH:
			return "bench";
		case EDITOR_METRICS_KIND_FUZZ:
			return "fuzz";
		case EDITOR_METRICS_KIND_UNKNOWN:
		default:
			return "unknown";
	}
}

/* Each row is a flat object: `{"k1":v1,"k2":v2,...}`. We don't need a
 * full JSON parser — string values in our schema never contain a
 * `"<known_key>":` substring (keys are simple identifiers; the only
 * arbitrary strings are ts and short identifiers like target/name/seed).
 * `findKey` returns a pointer into `line` at the first character of the
 * value, or NULL if not found. */

static const char *findKey(const char *line, const char *key) {
	size_t klen = strlen(key);
	char needle[160];
	if (klen + 4 >= sizeof(needle)) {
		return NULL;
	}
	needle[0] = '"';
	memcpy(needle + 1, key, klen);
	needle[klen + 1] = '"';
	needle[klen + 2] = ':';
	needle[klen + 3] = '\0';
	const char *p = strstr(line, needle);
	if (p == NULL) {
		return NULL;
	}
	return p + klen + 3;
}

static int getString(const char *line, const char *key, char *out, size_t out_size) {
	const char *p = findKey(line, key);
	if (p == NULL || *p != '"') {
		return 0;
	}
	p++;
	size_t pos = 0;
	while (*p != '\0' && *p != '"') {
		if (*p == '\\' && p[1] != '\0') {
			/* Decode the limited set of escapes our writer produces. */
			char c = 0;
			switch (p[1]) {
				case '"':
					c = '"';
					break;
				case '\\':
					c = '\\';
					break;
				case 'n':
					c = '\n';
					break;
				case 't':
					c = '\t';
					break;
				case 'r':
					c = '\r';
					break;
				case 'b':
					c = '\b';
					break;
				case 'f':
					c = '\f';
					break;
				default:
					c = p[1];
					break;
			}
			if (pos + 1 < out_size) {
				out[pos] = c;
			}
			pos++;
			p += 2;
			continue;
		}
		if (pos + 1 < out_size) {
			out[pos] = *p;
		}
		pos++;
		p++;
	}
	if (out_size > 0) {
		out[pos < out_size ? pos : out_size - 1] = '\0';
	}
	return 1;
}

static int getInt(const char *line, const char *key, long long *out) {
	const char *p = findKey(line, key);
	if (p == NULL) {
		return 0;
	}
	char *end = NULL;
	errno = 0;
	long long v = strtoll(p, &end, 10);
	if (errno != 0 || end == p) {
		return 0;
	}
	*out = v;
	return 1;
}

static int getDouble(const char *line, const char *key, double *out) {
	const char *p = findKey(line, key);
	if (p == NULL) {
		return 0;
	}
	char *end = NULL;
	errno = 0;
	double v = strtod(p, &end);
	if (errno != 0 || end == p) {
		return 0;
	}
	*out = v;
	return 1;
}

static int parseIsoTs(const char *ts, time_t *out) {
	/* Accept exactly the writer's format: YYYY-MM-DDTHH:MM:SSZ. */
	struct tm tm;
	memset(&tm, 0, sizeof(tm));
	if (sscanf(ts, "%4d-%2d-%2dT%2d:%2d:%2dZ", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, // NOLINT(cert-err34-c)
	           &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6) {
		return 0;
	}
	tm.tm_year -= 1900;
	tm.tm_mon -= 1;
	/* timegm() interprets the struct as UTC, unlike mktime(). */
	time_t t = timegm(&tm);
	if (t == (time_t)-1) {
		return 0;
	}
	*out = t;
	return 1;
}

int editorMetricsRowParse(const char *line, struct editorMetricsRow *row) {
	if (line == NULL || row == NULL) {
		return 0;
	}
	editorMetricsRowInit(row);

	char kind_buf[32];
	if (!getString(line, "kind", kind_buf, sizeof(kind_buf))) {
		return 0;
	}
	if (strcmp(kind_buf, "test_run") == 0) {
		row->kind = EDITOR_METRICS_KIND_TEST_RUN;
	} else if (strcmp(kind_buf, "bench") == 0) {
		row->kind = EDITOR_METRICS_KIND_BENCH;
	} else if (strcmp(kind_buf, "fuzz") == 0) {
		row->kind = EDITOR_METRICS_KIND_FUZZ;
	} else {
		row->kind = EDITOR_METRICS_KIND_UNKNOWN;
	}

	if (!getString(line, "ts", row->ts, sizeof(row->ts))) {
		return 0;
	}
	(void)parseIsoTs(row->ts, &row->ts_unix);

	(void)getString(line, "git_sha", row->git_sha, sizeof(row->git_sha));
	(void)getString(line, "git_ref", row->git_ref, sizeof(row->git_ref));
	(void)getString(line, "ci_run_id", row->ci_run_id, sizeof(row->ci_run_id));

	switch (row->kind) {
		case EDITOR_METRICS_KIND_TEST_RUN:
			(void)getDouble(line, "wall_seconds", &row->wall_seconds);
			(void)getInt(line, "total_runs", &row->total_runs);
			(void)getInt(line, "passed_runs", &row->passed_runs);
			(void)getInt(line, "failed_unique", &row->failed_unique);
			(void)getInt(line, "crashes", &row->crashes);
			(void)getInt(line, "reset_violations", &row->reset_violations);
			(void)getInt(line, "flakes", &row->flakes);
			(void)getInt(line, "property_ops", &row->property_ops);
			(void)getDouble(line, "property_ops_seconds", &row->property_ops_seconds);
			(void)getInt(line, "exit_code", &row->exit_code);
			break;
		case EDITOR_METRICS_KIND_BENCH:
			(void)getString(line, "name", row->bench_name, sizeof(row->bench_name));
			(void)getInt(line, "samples", &row->samples);
			(void)getInt(line, "inner_ops", &row->inner_ops);
			(void)getDouble(line, "min_ns", &row->min_ns);
			(void)getDouble(line, "p50_ns", &row->p50_ns);
			(void)getDouble(line, "p95_ns", &row->p95_ns);
			(void)getDouble(line, "iqr_ns", &row->iqr_ns);
			break;
		case EDITOR_METRICS_KIND_FUZZ:
			(void)getString(line, "target", row->fuzz_target, sizeof(row->fuzz_target));
			(void)getInt(line, "cov_edges", &row->cov_edges);
			(void)getInt(line, "ft_features", &row->ft_features);
			(void)getInt(line, "corp_count", &row->corp_count);
			(void)getInt(line, "corp_bytes", &row->corp_bytes);
			(void)getInt(line, "corpus_files", &row->corpus_files);
			(void)getInt(line, "corpus_bytes", &row->corpus_bytes);
			(void)getInt(line, "executed_units", &row->executed_units);
			(void)getInt(line, "new_units_added", &row->new_units_added);
			(void)getInt(line, "runtime_seconds", &row->runtime_seconds);
			break;
		default:
			break;
	}
	return 1;
}

static int row_capacity_grow(struct editorMetricsRow **rows, int *cap) {
	int new_cap = *cap == 0 ? 64 : *cap * 2;
	struct editorMetricsRow *grown =
	        (struct editorMetricsRow *)realloc(*rows, (size_t)new_cap * sizeof(**rows));
	if (grown == NULL) {
		return -1;
	}
	*rows = grown;
	*cap = new_cap;
	return 0;
}

int editorMetricsRowsLoad(const char *path, struct editorMetricsRow **rows_out, int *count_out,
                          int *skipped_out) {
	if (path == NULL || rows_out == NULL || count_out == NULL) {
		return -1;
	}
	*rows_out = NULL;
	*count_out = 0;
	if (skipped_out != NULL) {
		*skipped_out = 0;
	}

	FILE *f = fopen(path, "r");
	if (f == NULL) {
		return -1;
	}

	struct editorMetricsRow *rows = NULL;
	int cap = 0;
	int count = 0;
	int skipped = 0;
	char *line = NULL;
	size_t line_cap = 0;
	ssize_t len;

	while ((len = getline(&line, &line_cap, f)) != -1) {
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
			line[--len] = '\0';
		}
		if (len == 0) {
			continue;
		}
		if (count == cap) {
			if (row_capacity_grow(&rows, &cap) != 0) {
				free(line);
				free(rows);
				(void)fclose(f);
				return -1;
			}
		}
		if (!editorMetricsRowParse(line, &rows[count])) {
			skipped++;
			continue;
		}
		count++;
	}

	free(line);
	(void)fclose(f);

	*rows_out = rows;
	*count_out = count;
	if (skipped_out != NULL) {
		*skipped_out = skipped;
	}
	return 0;
}

void editorMetricsRowsFree(struct editorMetricsRow *rows, int count) {
	(void)count;
	free(rows);
}
