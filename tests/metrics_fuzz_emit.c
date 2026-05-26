#define _DEFAULT_SOURCE
#define _GNU_SOURCE

/* Append a `kind=fuzz` row to a metrics JSONL file, parsing the captured
 * libFuzzer stderr at --log and scanning the corpus dir at --corpus-dir.
 *
 * Usage:
 *   metrics_fuzz_emit --target NAME --log PATH --corpus-dir PATH \
 *                     --metrics-out PATH [--soak-seconds N]
 *
 * The wrapper is intentionally narrow: failures are warnings, never fatal
 * to the surrounding fuzz target. The fuzz exit code is preserved by the
 * Makefile-level caller.
 */

#include "metrics_jsonl.h"
#include "metrics_libfuzzer_parse.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *read_whole_file(const char *path) {
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		return NULL;
	}
	struct stat st;
	if (fstat(fd, &st) != 0) {
		(void)close(fd);
		return NULL;
	}
	size_t size = (size_t)st.st_size;
	char *buf = (char *)malloc(size + 1);
	if (buf == NULL) {
		(void)close(fd);
		return NULL;
	}
	size_t off = 0;
	while (off < size) {
		ssize_t n = read(fd, buf + off, size - off);
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			free(buf);
			(void)close(fd);
			return NULL;
		}
		if (n == 0) {
			break;
		}
		off += (size_t)n;
	}
	(void)close(fd);
	buf[off] = '\0';
	return buf;
}

static void usage(FILE *out) {
	(void)fprintf(out, "usage: metrics_fuzz_emit --target NAME --log PATH "
	                   "--corpus-dir PATH --metrics-out PATH [--soak-seconds N]\n");
}

int main(int argc, char **argv) {
	const char *target = NULL;
	const char *log = NULL;
	const char *corpus_dir = NULL;
	const char *metrics_out = NULL;
	long long soak_seconds = -1;

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		const char *next = (i + 1 < argc) ? argv[i + 1] : NULL;
		if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
			usage(stdout);
			return 0;
		}
		if (strcmp(a, "--target") == 0 && next) {
			target = next;
			i++;
			continue;
		}
		if (strcmp(a, "--log") == 0 && next) {
			log = next;
			i++;
			continue;
		}
		if (strcmp(a, "--corpus-dir") == 0 && next) {
			corpus_dir = next;
			i++;
			continue;
		}
		if (strcmp(a, "--metrics-out") == 0 && next) {
			metrics_out = next;
			i++;
			continue;
		}
		if (strcmp(a, "--soak-seconds") == 0 && next) {
			char *end = NULL;
			soak_seconds = strtoll(next, &end, 10);
			if (end == NULL || *end != '\0' || soak_seconds < 0) {
				(void)fprintf(stderr, "metrics_fuzz_emit: bad --soak-seconds\n");
				return 2;
			}
			i++;
			continue;
		}
		(void)fprintf(stderr, "metrics_fuzz_emit: unknown arg: %s\n", a);
		usage(stderr);
		return 2;
	}

	if (target == NULL || log == NULL || corpus_dir == NULL || metrics_out == NULL) {
		usage(stderr);
		return 2;
	}

	char *text = read_whole_file(log);
	if (text == NULL) {
		(void)fprintf(stderr, "metrics_fuzz_emit: warning: cannot read log %s: %s\n", log,
		              strerror(errno));
		/* Still try to emit a row with corpus stats so chart code sees
		 * something. */
		text = strdup("");
	}

	struct editorLibFuzzerStats s;
	editorLibFuzzerStatsParse(text, &s);
	free(text);

	long long corpus_count = 0;
	long long corpus_bytes = 0;
	(void)editorLibFuzzerScanCorpus(corpus_dir, &corpus_count, &corpus_bytes);

	long long runtime =
	        s.has_runtime ? s.runtime_seconds : (soak_seconds >= 0 ? soak_seconds : 0);

	struct editorMetricsField fields[] = {
	        {"target", EDITOR_METRICS_STR, .v.s = target},
	        {"cov_edges", EDITOR_METRICS_INT, .v.i = s.cov_edges},
	        {"ft_features", EDITOR_METRICS_INT, .v.i = s.ft_features},
	        {"corp_count", EDITOR_METRICS_INT, .v.i = s.corp_count},
	        {"corp_bytes", EDITOR_METRICS_INT, .v.i = s.corp_bytes},
	        {"corpus_files", EDITOR_METRICS_INT, .v.i = corpus_count},
	        {"corpus_bytes", EDITOR_METRICS_INT, .v.i = corpus_bytes},
	        {"executed_units", EDITOR_METRICS_INT, .v.i = s.executed_units},
	        {"avg_exec_per_sec", EDITOR_METRICS_INT, .v.i = s.avg_exec_per_sec},
	        {"new_units_added", EDITOR_METRICS_INT, .v.i = s.new_units_added},
	        {"peak_rss_mb", EDITOR_METRICS_INT, .v.i = s.peak_rss_mb},
	        {"runtime_seconds", EDITOR_METRICS_INT, .v.i = runtime},
	        {"has_final_stats", EDITOR_METRICS_BOOL, .v.b = s.has_final_stats},
	};
	int rc = editorMetricsAppend(metrics_out, "fuzz", fields,
	                             (int)(sizeof(fields) / sizeof(fields[0])));
	if (rc != 0) {
		(void)fprintf(stderr, "metrics_fuzz_emit: warning: failed to append to %s\n",
		              metrics_out);
		return 1;
	}
	return 0;
}
