#ifndef TESTS_METRICS_LIBFUZZER_PARSE_H
#define TESTS_METRICS_LIBFUZZER_PARSE_H

/* Parse the final-stats subset of a libFuzzer run log (its stderr output).
 *
 * Two distinct sources of truth in the log:
 *   1. Per-update progress lines:
 *        #NNN STAGE cov: C ft: F corp: K/SbU lim: L exec/s: E rss: R Mb ...
 *      where U is a unit suffix (b/Kb/Mb). The *last* such line is the
 *      current state at end-of-run, so we keep overwriting.
 *   2. End-of-run lines (only when libFuzzer is run with
 *      -print_final_stats=1):
 *        Done NNN runs in T second(s)
 *        stat::number_of_executed_units: N
 *        stat::average_exec_per_sec:     N
 *        stat::new_units_added:          N
 *        stat::slowest_unit_time_sec:    N
 *        stat::peak_rss_mb:              N
 *
 * `has_cov_line` and `has_final_stats` flag which sources fired so the
 * caller can decide what to emit and what to omit.
 */

struct editorLibFuzzerStats {
	int has_cov_line;
	long long cov_edges;
	long long ft_features;
	long long corp_count;
	long long corp_bytes;

	int has_final_stats;
	long long executed_units;
	long long avg_exec_per_sec;
	long long new_units_added;
	long long slowest_unit_seconds; /* whole seconds; libFuzzer doesn't print fractional here */
	long long peak_rss_mb;

	int has_runtime;
	long long runtime_seconds;
};

void editorLibFuzzerStatsInit(struct editorLibFuzzerStats *out);

/* `text` is a NUL-terminated string holding the captured libFuzzer
 * stderr. Lines that don't match are ignored. Idempotent; later matching
 * lines overwrite earlier values, so the result reflects end-of-run state.
 */
void editorLibFuzzerStatsParse(const char *text, struct editorLibFuzzerStats *out);

/* Walk a directory and count regular files plus total byte size. Hidden
 * entries (those starting with '.') are skipped to match the convention
 * libFuzzer uses for the corpus directory.
 *
 * Returns 0 on success, -1 on opendir failure. On success
 * *file_count_out and *byte_count_out are populated. The function does
 * not descend into subdirectories (libFuzzer's corpus is flat).
 */
int editorLibFuzzerScanCorpus(const char *dir, long long *file_count_out,
                              long long *byte_count_out);

#endif
