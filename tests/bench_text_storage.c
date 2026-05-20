#include "editing/row_cache.h"
#include "rotide.h"
#include "text/document.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

/* Stub for the global referenced by editor support TUs that we link against. */
struct editorConfig E;

#define BENCH_DOC_BYTES (1u << 20)
#define BENCH_OPS 10000

static uint64_t g_rng;

static uint64_t bench_rng_next(void) {
	uint64_t x = g_rng;
	if (x == 0) {
		x = 0x9E3779B97F4A7C15ULL;
	}
	x ^= x >> 12;
	x ^= x << 25;
	x ^= x >> 27;
	g_rng = x;
	return x * 0x2545F4914F6CDD1DULL;
}

static double monotonic_seconds(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static long process_rss_kib(void) {
	struct rusage ru;
	if (getrusage(RUSAGE_SELF, &ru) != 0) {
		return -1;
	}
	/* ru_maxrss is in KiB on Linux. */
	return ru.ru_maxrss;
}

static char *generate_seed_text(size_t bytes) {
	char *buf = (char *)malloc(bytes);
	if (buf == NULL) {
		return NULL;
	}
	for (size_t i = 0; i < bytes; i++) {
		unsigned r = (unsigned)(bench_rng_next() % 100);
		if (r < 5) {
			buf[i] = '\n';
		} else if (r < 55) {
			buf[i] = (char)('a' + (bench_rng_next() % 26));
		} else {
			buf[i] = (char)('A' + (bench_rng_next() % 26));
		}
	}
	return buf;
}

int main(int argc, char **argv) {
	uint64_t seed = 0x9E3779B97F4A7C15ULL;
	size_t doc_bytes = BENCH_DOC_BYTES;
	int ops = BENCH_OPS;
	for (int i = 1; i + 1 < argc; i += 2) {
		if (strcmp(argv[i], "--seed") == 0) {
			seed = strtoull(argv[i + 1], NULL, 0);
		} else if (strcmp(argv[i], "--bytes") == 0) {
			doc_bytes = (size_t)strtoull(argv[i + 1], NULL, 0);
		} else if (strcmp(argv[i], "--ops") == 0) {
			ops = atoi(argv[i + 1]);
		} else {
			fprintf(stderr, "unknown flag: %s\n", argv[i]);
			return 2;
		}
	}
	g_rng = seed;

	char *seed_text = generate_seed_text(doc_bytes);
	if (seed_text == NULL) {
		fprintf(stderr, "bench: malloc(%zu) failed\n", doc_bytes);
		return 1;
	}

	struct editorDocument doc;
	editorDocumentInit(&doc);

	double t0 = monotonic_seconds();
	if (!editorDocumentResetFromString(&doc, seed_text, doc_bytes)) {
		fprintf(stderr, "bench: reset failed\n");
		free(seed_text);
		editorDocumentFree(&doc);
		return 1;
	}
	double t1 = monotonic_seconds();
	free(seed_text);

	double t_open = t1 - t0;

	double t_inserts_start = monotonic_seconds();
	for (int i = 0; i < ops; i++) {
		size_t cur = editorDocumentLength(&doc);
		size_t at = cur == 0 ? 0 : (size_t)(bench_rng_next() % (cur + 1));
		char ch = (char)('a' + (bench_rng_next() % 26));
		if (!editorDocumentReplaceRange(&doc, at, 0, &ch, 1)) {
			fprintf(stderr, "bench: insert op#%d failed\n", i);
			editorDocumentFree(&doc);
			return 1;
		}
	}
	double t_inserts = monotonic_seconds() - t_inserts_start;

	double t_deletes_start = monotonic_seconds();
	for (int i = 0; i < ops; i++) {
		size_t cur = editorDocumentLength(&doc);
		if (cur == 0) {
			break;
		}
		size_t at = (size_t)(bench_rng_next() % cur);
		if (!editorDocumentReplaceRange(&doc, at, 1, NULL, 0)) {
			fprintf(stderr, "bench: delete op#%d failed\n", i);
			editorDocumentFree(&doc);
			return 1;
		}
	}
	double t_deletes = monotonic_seconds() - t_deletes_start;

	double t_replaces_start = monotonic_seconds();
	for (int i = 0; i < ops; i++) {
		size_t cur = editorDocumentLength(&doc);
		if (cur == 0) {
			break;
		}
		size_t at = (size_t)(bench_rng_next() % cur);
		char ch = (char)('a' + (bench_rng_next() % 26));
		if (!editorDocumentReplaceRange(&doc, at, 1, &ch, 1)) {
			fprintf(stderr, "bench: replace op#%d failed\n", i);
			editorDocumentFree(&doc);
			return 1;
		}
	}
	double t_replaces = monotonic_seconds() - t_replaces_start;

	size_t final_len = editorDocumentLength(&doc);
	int final_lines = editorDocumentLineCount(&doc);

	long rss_before_rows = process_rss_kib();
	struct editorRow *rows = NULL;
	int numrows = 0;
	if (!editorBuildFullRowsFromDocument(&doc, &rows, &numrows)) {
		fprintf(stderr, "bench: row-cache build failed\n");
		editorDocumentFree(&doc);
		return 1;
	}
	long rss_after_rows = process_rss_kib();

	printf("bench_text_storage: seed=0x%016llx bytes=%zu ops=%d\n", (unsigned long long)seed,
	       doc_bytes, ops);
	printf("  open_reset:      %8.4f s  (%.2f MB/s)\n", t_open,
	       (double)doc_bytes / (t_open > 0 ? t_open : 1e-12) / 1e6);
	printf("  random_inserts:  %8.4f s  (%.2f us/op)\n", t_inserts,
	       t_inserts * 1e6 / (double)(ops > 0 ? ops : 1));
	printf("  random_deletes:  %8.4f s  (%.2f us/op)\n", t_deletes,
	       t_deletes * 1e6 / (double)(ops > 0 ? ops : 1));
	printf("  random_replaces: %8.4f s  (%.2f us/op)\n", t_replaces,
	       t_replaces * 1e6 / (double)(ops > 0 ? ops : 1));
	printf("  final_length=%zu final_lines=%d\n", final_len, final_lines);
	if (rss_before_rows >= 0 && rss_after_rows >= 0) {
		printf("  row_cache_build: rows=%d rss_before=%ld KiB rss_after=%ld KiB "
		       "delta=%ld KiB\n",
		       numrows, rss_before_rows, rss_after_rows, rss_after_rows - rss_before_rows);
	}

	editorFreeRowArray(rows, numrows);
	editorDocumentFree(&doc);
	return 0;
}
