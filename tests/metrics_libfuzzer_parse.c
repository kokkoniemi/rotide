#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include "metrics_libfuzzer_parse.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

void editorLibFuzzerStatsInit(struct editorLibFuzzerStats *out) {
	if (out == NULL) {
		return;
	}
	out->has_cov_line = 0;
	out->cov_edges = 0;
	out->ft_features = 0;
	out->corp_count = 0;
	out->corp_bytes = 0;
	out->has_final_stats = 0;
	out->executed_units = 0;
	out->avg_exec_per_sec = 0;
	out->new_units_added = 0;
	out->slowest_unit_seconds = 0;
	out->peak_rss_mb = 0;
	out->has_runtime = 0;
	out->runtime_seconds = 0;
}

/* Find `needle` followed by spaces and an integer in `line`. On success
 * sets *out and returns a pointer to the position just past the integer.
 * Returns NULL on miss. */
static const char *find_int_after(const char *line, const char *needle, long long *out) {
	const char *p = strstr(line, needle);
	if (p == NULL) {
		return NULL;
	}
	p += strlen(needle);
	while (*p == ' ' || *p == '\t') {
		p++;
	}
	int sign = 1;
	if (*p == '-') {
		sign = -1;
		p++;
	}
	if (!isdigit((unsigned char)*p)) {
		return NULL;
	}
	long long v = 0;
	while (isdigit((unsigned char)*p)) {
		v = v * 10 + (*p - '0');
		p++;
	}
	if (out != NULL) {
		*out = sign * v;
	}
	return p;
}

/* Parse the corp byte side of "K/Sb" or "K/SKb" or "K/SMb". On entry,
 * `p` points just after the digits of K (i.e. at the '/'). */
static int parse_corp_bytes(const char *p, long long *count, long long *bytes) {
	if (*p != '/') {
		return 0;
	}
	p++;
	if (!isdigit((unsigned char)*p)) {
		return 0;
	}
	long long size = 0;
	while (isdigit((unsigned char)*p)) {
		size = size * 10 + (*p - '0');
		p++;
	}
	long long multiplier = 1;
	if (*p == 'K' || *p == 'k') {
		multiplier = 1024;
		p++;
	} else if (*p == 'M' || *p == 'm') {
		multiplier = 1024 * 1024;
		p++;
	} else if (*p == 'G' || *p == 'g') {
		multiplier = 1024LL * 1024 * 1024;
		p++;
	}
	/* libFuzzer always closes with a 'b'. Accept either case; reject the
	 * line if the suffix isn't present so we don't misread "20/30 corpus"
	 * style mishaps. */
	if (*p != 'b' && *p != 'B') {
		return 0;
	}
	if (bytes != NULL) {
		*bytes = size * multiplier;
	}
	(void)count;
	return 1;
}

static void parse_one_line(const char *line, struct editorLibFuzzerStats *out) {
	/* Run line: contains "cov:" AND "ft:" AND "corp:". */
	const char *cov = strstr(line, "cov:");
	const char *ft = strstr(line, "ft:");
	const char *corp = strstr(line, "corp:");
	if (cov != NULL && ft != NULL && corp != NULL) {
		long long c = 0;
		long long f = 0;
		long long k = 0;
		const char *after_cov = find_int_after(cov, "cov:", &c);
		const char *after_ft = find_int_after(ft, "ft:", &f);
		const char *after_corp = find_int_after(corp, "corp:", &k);
		if (after_cov != NULL && after_ft != NULL && after_corp != NULL) {
			long long bytes = 0;
			if (parse_corp_bytes(after_corp, NULL, &bytes)) {
				out->has_cov_line = 1;
				out->cov_edges = c;
				out->ft_features = f;
				out->corp_count = k;
				out->corp_bytes = bytes;
			}
		}
		return;
	}

	/* "Done N runs in T second(s)" */
	if (strncmp(line, "Done ", 5) == 0) {
		long long runs = 0;
		long long secs = 0;
		const char *after_runs = find_int_after(line, "Done", &runs);
		if (after_runs != NULL) {
			const char *after_in = find_int_after(after_runs, "in", &secs);
			if (after_in != NULL && strstr(after_in, "second") != NULL) {
				out->has_runtime = 1;
				out->runtime_seconds = secs;
				if (!out->has_final_stats || out->executed_units == 0) {
					/* Will be overwritten by the more specific
					 * stat::number_of_executed_units when present. */
					out->executed_units = runs;
				}
			}
		}
		return;
	}

	/* stat:: lines */
	if (strncmp(line, "stat::", 6) == 0) {
		struct {
			const char *key;
			long long *dst;
		} mapping[] = {
		        {"stat::number_of_executed_units", &out->executed_units},
		        {"stat::average_exec_per_sec", &out->avg_exec_per_sec},
		        {"stat::new_units_added", &out->new_units_added},
		        {"stat::slowest_unit_time_sec", &out->slowest_unit_seconds},
		        {"stat::peak_rss_mb", &out->peak_rss_mb},
		};
		for (size_t i = 0; i < sizeof(mapping) / sizeof(mapping[0]); i++) {
			size_t klen = strlen(mapping[i].key);
			if (strncmp(line, mapping[i].key, klen) != 0) {
				continue;
			}
			const char *p = line + klen;
			if (*p != ':') {
				continue;
			}
			p++;
			long long v = 0;
			const char *after = find_int_after(p, "", &v);
			if (after != NULL) {
				*mapping[i].dst = v;
				out->has_final_stats = 1;
			}
			break;
		}
	}
}

void editorLibFuzzerStatsParse(const char *text, struct editorLibFuzzerStats *out) {
	if (out == NULL) {
		return;
	}
	editorLibFuzzerStatsInit(out);
	if (text == NULL) {
		return;
	}

	const char *p = text;
	char line[1024];
	while (*p != '\0') {
		const char *eol = strchr(p, '\n');
		size_t len = eol != NULL ? (size_t)(eol - p) : strlen(p);
		if (len >= sizeof(line)) {
			len = sizeof(line) - 1;
		}
		memcpy(line, p, len);
		line[len] = '\0';
		parse_one_line(line, out);
		if (eol == NULL) {
			break;
		}
		p = eol + 1;
	}
}

int editorLibFuzzerScanCorpus(const char *dir, long long *file_count_out,
                              long long *byte_count_out) {
	if (file_count_out != NULL) {
		*file_count_out = 0;
	}
	if (byte_count_out != NULL) {
		*byte_count_out = 0;
	}
	if (dir == NULL) {
		return -1;
	}
	DIR *d = opendir(dir);
	if (d == NULL) {
		return -1;
	}
	long long count = 0;
	long long bytes = 0;
	struct dirent *ent;
	char path[4096];
	while ((ent = readdir(d)) != NULL) {
		if (ent->d_name[0] == '.') {
			continue;
		}
		int n = snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
		if (n < 0 || (size_t)n >= sizeof(path)) {
			continue;
		}
		struct stat st;
		if (stat(path, &st) != 0) {
			continue;
		}
		if (!S_ISREG(st.st_mode)) {
			continue;
		}
		count++;
		bytes += (long long)st.st_size;
	}
	(void)closedir(d);
	if (file_count_out != NULL) {
		*file_count_out = count;
	}
	if (byte_count_out != NULL) {
		*byte_count_out = bytes;
	}
	return 0;
}
