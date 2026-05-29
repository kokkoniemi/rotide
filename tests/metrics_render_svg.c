#include "metrics_render_svg.h"

#include "metrics_jsonl_read.h"
#include "metrics_summary_cmd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define SVG_W 720
#define SVG_H 320
#define SVG_PAD_TOP 44
#define SVG_PAD_RIGHT 24
#define SVG_PAD_BOTTOM 52
#define SVG_PAD_LEFT 72
#define SVG_PLOT_W (SVG_W - SVG_PAD_LEFT - SVG_PAD_RIGHT)
#define SVG_PLOT_H (SVG_H - SVG_PAD_TOP - SVG_PAD_BOTTOM)
/* Visual tick cap. Date labels are ~70px wide at 12px font; with
 * SVG_PLOT_W=624 and 6 ticks, spacing is ~125px — never overlap. */
#define SVG_MAX_X_TICKS 6

#define RENDER_DEFAULT_POINTS 30
#define RENDER_MAX_POINTS 60

#define COLOR_PRIMARY "#1f77b4"
#define COLOR_SECONDARY "#ff7f0e"
#define COLOR_TERTIARY "#2ca02c"
#define COLOR_AXIS "#444"
#define COLOR_GRID "#e6e6e6"
#define COLOR_TEXT "#222"
#define COLOR_BG "#ffffff"

static int compareDouble(const void *a, const void *b) {
	double da = *(const double *)a;
	double db = *(const double *)b;
	if (da < db) {
		return -1;
	}
	if (da > db) {
		return 1;
	}
	return 0;
}

double editorMetricsPassRate(long long passed_runs, long long total_runs) {
	if (total_runs <= 0) {
		return 100.0;
	}
	return (double)passed_runs / (double)total_runs * 100.0;
}

void editorMetricsRollingMedian(const double *in, int n_points, int window, double *out) {
	if (in == NULL || out == NULL || n_points <= 0) {
		return;
	}
	if (window < 1) {
		window = 1;
	}
	if (window % 2 == 0) {
		window += 1; /* odd window keeps the median centered on each point */
	}
	if (window > RENDER_MAX_POINTS) {
		window = RENDER_MAX_POINTS - 1; /* RENDER_MAX_POINTS is even */
	}
	int half = window / 2;
	double scratch[RENDER_MAX_POINTS];
	for (int i = 0; i < n_points; i++) {
		int lo = i - half < 0 ? 0 : i - half;
		int hi = i + half >= n_points ? n_points - 1 : i + half;
		int m = 0;
		for (int j = lo; j <= hi; j++) {
			scratch[m++] = in[j];
		}
		qsort(scratch, (size_t)m, sizeof(scratch[0]), compareDouble);
		out[i] =
		        (m % 2 == 1) ? scratch[m / 2] : (scratch[m / 2 - 1] + scratch[m / 2]) / 2.0;
	}
}

static int rowPassesFilters(const struct editorMetricsRow *r,
                            const struct editorMetricsCmdOptions *opts) {
	if (opts->kind_filter != EDITOR_METRICS_KIND_UNKNOWN && r->kind != opts->kind_filter) {
		return 0;
	}
	if (opts->since_unix > 0 && r->ts_unix < opts->since_unix) {
		return 0;
	}
	if (r->kind == EDITOR_METRICS_KIND_FUZZ && opts->target_filter != NULL &&
	    opts->target_filter[0] != '\0' && strcmp(r->fuzz_target, opts->target_filter) != 0) {
		return 0;
	}
	if (r->kind == EDITOR_METRICS_KIND_BENCH && opts->bench_name_filter != NULL &&
	    opts->bench_name_filter[0] != '\0' &&
	    strcmp(r->bench_name, opts->bench_name_filter) != 0) {
		return 0;
	}
	return 1;
}

static int compareByTsPtr(const void *a, const void *b) {
	const struct editorMetricsRow *ra = *(const struct editorMetricsRow *const *)a;
	const struct editorMetricsRow *rb = *(const struct editorMetricsRow *const *)b;
	if (ra->ts_unix < rb->ts_unix)
		return -1;
	if (ra->ts_unix > rb->ts_unix)
		return 1;
	return 0;
}

static void formatDateLabel(const struct editorMetricsRow *r, char *out, size_t out_sz) {
	if (r->ts[0] != '\0' && strlen(r->ts) >= 10) {
		(void)snprintf(out, out_sz, "%.10s", r->ts);
		return;
	}
	if (r->ts_unix > 0) {
		time_t t = (time_t)r->ts_unix;
		struct tm tm_buf;
		if (gmtime_r(&t, &tm_buf) != NULL) {
			(void)strftime(out, out_sz, "%Y-%m-%d", &tm_buf);
			return;
		}
	}
	(void)snprintf(out, out_sz, "?");
}

/* Replace any char outside [A-Za-z0-9._-] with '_' so the result is safe
 * as a filename across platforms. */
static void sanitizeFilenameInto(const char *in, char *out, size_t out_sz) {
	size_t j = 0;
	for (size_t i = 0; in[i] != '\0' && j + 1 < out_sz; i++) {
		unsigned char c = (unsigned char)in[i];
		int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		         (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
		out[j++] = ok ? (char)c : '_';
	}
	out[j] = '\0';
}

/* Drops control chars in addition to XML-escaping &<>"'. */
static void writeXmlEscaped(FILE *out, const char *s) {
	for (const char *p = s; *p != '\0'; p++) {
		unsigned char c = (unsigned char)*p;
		switch (c) {
			case '<':
				(void)fputs("&lt;", out);
				break;
			case '>':
				(void)fputs("&gt;", out);
				break;
			case '&':
				(void)fputs("&amp;", out);
				break;
			case '"':
				(void)fputs("&quot;", out);
				break;
			case '\'':
				(void)fputs("&#39;", out);
				break;
			default:
				if (c >= 0x20 && c != 0x7f) {
					(void)fputc((int)c, out);
				}
				break;
		}
	}
}

/* Always emits indices 0 and n-1 as ticks, even when stride wouldn't
 * land on the last point — keeps the chart's endpoints labeled. */
static int pickTickIndices(int n, int *out, int out_cap) {
	if (n <= 0 || out_cap <= 0) {
		return 0;
	}
	if (n <= SVG_MAX_X_TICKS) {
		int k = 0;
		for (int i = 0; i < n && k < out_cap; i++) {
			out[k++] = i;
		}
		return k;
	}
	int target = SVG_MAX_X_TICKS;
	int stride = (n - 1) / (target - 1);
	if (stride < 1) {
		stride = 1;
	}
	int k = 0;
	for (int i = 0; i < n - 1 && k < out_cap - 1; i += stride) {
		out[k++] = i;
	}
	if (k == 0 || out[k - 1] != n - 1) {
		out[k++] = n - 1;
	}
	return k;
}

static double mapX(int i, int n) {
	if (n <= 1) {
		return (double)SVG_PAD_LEFT + (double)SVG_PLOT_W * 0.5;
	}
	return (double)SVG_PAD_LEFT + ((double)i / (double)(n - 1)) * (double)SVG_PLOT_W;
}

static double mapY(double v, double y_min, double y_max) {
	if (y_max <= y_min) {
		return (double)SVG_PAD_TOP + (double)SVG_PLOT_H * 0.5;
	}
	double t = (v - y_min) / (y_max - y_min);
	return (double)SVG_PAD_TOP + (1.0 - t) * (double)SVG_PLOT_H;
}

/* Compact y-axis label: "1.2k", "8.5M", "950" depending on magnitude. */
static void formatYValue(double v, char *out, size_t out_sz) {
	double abs_v = v < 0 ? -v : v;
	if (abs_v >= 1e9) {
		(void)snprintf(out, out_sz, "%.1fG", v / 1e9);
	} else if (abs_v >= 1e6) {
		(void)snprintf(out, out_sz, "%.1fM", v / 1e6);
	} else if (abs_v >= 1e3) {
		(void)snprintf(out, out_sz, "%.1fk", v / 1e3);
	} else if (abs_v >= 10.0 || v == (double)(long long)v) {
		(void)snprintf(out, out_sz, "%.0f", v);
	} else {
		(void)snprintf(out, out_sz, "%.2f", v);
	}
}

static void computeYBounds(const struct editorSvgSeries *series, int n_series, int n_points,
                           double *y_min_out, double *y_max_out) {
	double mn = series[0].values[0];
	double mx = mn;
	for (int s = 0; s < n_series; s++) {
		for (int i = 0; i < n_points; i++) {
			double v = series[s].values[i];
			if (v < mn) {
				mn = v;
			}
			if (v > mx) {
				mx = v;
			}
		}
	}
	double span = mx - mn;
	if (span <= 0) {
		span = mx == 0 ? 1.0 : (mx < 0 ? -mx : mx);
	}
	double pad = span * 0.1;
	if (pad < 1.0) {
		pad = 1.0;
	}
	double y_min = mn - pad;
	double y_max = mx + pad;
	/* Clamp lower bound to 0 if all values are non-negative — looks
	 * nicer and matches reader expectations (no negative ns / edges). */
	if (mn >= 0 && y_min < 0) {
		y_min = 0;
	}
	*y_min_out = y_min;
	*y_max_out = y_max;
}

void editorMetricsRenderSvgChart(FILE *out, const char *title, const char *y_unit,
                                 const char *const *x_labels, int n_points,
                                 const struct editorSvgSeries *series, int n_series) {
	if (out == NULL || n_points < 2 || n_series <= 0) {
		return;
	}

	double y_min = 0;
	double y_max = 1;
	computeYBounds(series, n_series, n_points, &y_min, &y_max);

	(void)fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n", out);
	(void)fprintf(out,
	              "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\" "
	              "viewBox=\"0 0 %d %d\" font-family=\"-apple-system,BlinkMacSystemFont,"
	              "Segoe UI,sans-serif\" font-size=\"12\">\n",
	              SVG_W, SVG_H, SVG_W, SVG_H);
	(void)fprintf(out, "<rect width=\"%d\" height=\"%d\" fill=\"%s\"/>\n", SVG_W, SVG_H,
	              COLOR_BG);

	(void)fprintf(out,
	              "<text x=\"%d\" y=\"24\" text-anchor=\"middle\" font-size=\"14\" "
	              "font-weight=\"600\" fill=\"%s\">",
	              SVG_W / 2, COLOR_TEXT);
	if (title != NULL) {
		writeXmlEscaped(out, title);
	}
	(void)fputs("</text>\n", out);

	double y_ticks[3];
	y_ticks[0] = y_max;
	y_ticks[1] = (y_max + y_min) * 0.5;
	y_ticks[2] = y_min;
	for (int t = 0; t < 3; t++) {
		double y_px = mapY(y_ticks[t], y_min, y_max);
		(void)fprintf(out,
		              "<line x1=\"%d\" y1=\"%.1f\" x2=\"%d\" y2=\"%.1f\" stroke=\"%s\" "
		              "stroke-width=\"1\"/>\n",
		              SVG_PAD_LEFT, y_px, SVG_PAD_LEFT + SVG_PLOT_W, y_px, COLOR_GRID);
		char label[32];
		formatYValue(y_ticks[t], label, sizeof(label));
		(void)fprintf(out,
		              "<text x=\"%d\" y=\"%.1f\" text-anchor=\"end\" "
		              "dominant-baseline=\"middle\" fill=\"%s\">",
		              SVG_PAD_LEFT - 8, y_px, COLOR_TEXT);
		writeXmlEscaped(out, label);
		(void)fputs("</text>\n", out);
	}

	if (y_unit != NULL && y_unit[0] != '\0') {
		(void)fprintf(out,
		              "<text x=\"16\" y=\"%d\" text-anchor=\"middle\" fill=\"%s\" "
		              "transform=\"rotate(-90 16 %d)\">",
		              SVG_PAD_TOP + SVG_PLOT_H / 2, COLOR_TEXT,
		              SVG_PAD_TOP + SVG_PLOT_H / 2);
		writeXmlEscaped(out, y_unit);
		(void)fputs("</text>\n", out);
	}

	/* Drawn after the gridlines so the axis lines sit on top. */
	(void)fprintf(out,
	              "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" stroke=\"%s\" "
	              "stroke-width=\"1\"/>\n",
	              SVG_PAD_LEFT, SVG_PAD_TOP, SVG_PAD_LEFT, SVG_PAD_TOP + SVG_PLOT_H,
	              COLOR_AXIS);
	(void)fprintf(out,
	              "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" stroke=\"%s\" "
	              "stroke-width=\"1\"/>\n",
	              SVG_PAD_LEFT, SVG_PAD_TOP + SVG_PLOT_H, SVG_PAD_LEFT + SVG_PLOT_W,
	              SVG_PAD_TOP + SVG_PLOT_H, COLOR_AXIS);

	int tick_idx[SVG_MAX_X_TICKS + 2];
	int n_ticks = pickTickIndices(n_points, tick_idx, (int)(sizeof(tick_idx) / sizeof(int)));
	for (int t = 0; t < n_ticks; t++) {
		int i = tick_idx[t];
		double x_px = mapX(i, n_points);
		(void)fprintf(out,
		              "<line x1=\"%.1f\" y1=\"%d\" x2=\"%.1f\" y2=\"%d\" stroke=\"%s\" "
		              "stroke-width=\"1\"/>\n",
		              x_px, SVG_PAD_TOP + SVG_PLOT_H, x_px, SVG_PAD_TOP + SVG_PLOT_H + 4,
		              COLOR_AXIS);
		(void)fprintf(out, "<text x=\"%.1f\" y=\"%d\" text-anchor=\"middle\" fill=\"%s\">",
		              x_px, SVG_PAD_TOP + SVG_PLOT_H + 18, COLOR_TEXT);
		if (x_labels != NULL && x_labels[i] != NULL) {
			writeXmlEscaped(out, x_labels[i]);
		}
		(void)fputs("</text>\n", out);
	}

	const char *fallback_colors[3] = {COLOR_PRIMARY, COLOR_SECONDARY, COLOR_TERTIARY};
	for (int s = 0; s < n_series; s++) {
		const struct editorSvgSeries *ser = &series[s];
		const char *color = ser->color;
		if (color == NULL || color[0] == '\0') {
			color = fallback_colors[s % 3];
		}
		(void)fprintf(out,
		              "<polyline fill=\"none\" stroke=\"%s\" stroke-width=\"2\" "
		              "stroke-linejoin=\"round\" stroke-linecap=\"round\" points=\"",
		              color);
		for (int i = 0; i < n_points; i++) {
			double x_px = mapX(i, n_points);
			double y_px = mapY(ser->values[i], y_min, y_max);
			if (i > 0) {
				(void)fputc(' ', out);
			}
			(void)fprintf(out, "%.1f,%.1f", x_px, y_px);
		}
		(void)fputs("\"/>\n", out);

		double xl = mapX(n_points - 1, n_points);
		double yl = mapY(ser->values[n_points - 1], y_min, y_max);
		(void)fprintf(out, "<circle cx=\"%.1f\" cy=\"%.1f\" r=\"3\" fill=\"%s\"/>\n", xl,
		              yl, color);
	}

	if (n_series > 1) {
		int legend_x = SVG_PAD_LEFT + SVG_PLOT_W - 110;
		int legend_y = SVG_PAD_TOP + 6;
		for (int s = 0; s < n_series; s++) {
			const struct editorSvgSeries *ser = &series[s];
			const char *color = ser->color;
			if (color == NULL || color[0] == '\0') {
				color = fallback_colors[s % 3];
			}
			int row_y = legend_y + s * 16;
			(void)fprintf(out,
			              "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" stroke=\"%s\" "
			              "stroke-width=\"2\"/>\n",
			              legend_x, row_y, legend_x + 18, row_y, color);
			(void)fprintf(out,
			              "<text x=\"%d\" y=\"%d\" dominant-baseline=\"middle\" "
			              "fill=\"%s\">",
			              legend_x + 24, row_y, COLOR_TEXT);
			if (ser->label != NULL) {
				writeXmlEscaped(out, ser->label);
			}
			(void)fputs("</text>\n", out);
		}
	}

	(void)fputs("</svg>\n", out);
}

static struct editorMetricsRow **collectGroupSorted(const struct editorMetricsRow *rows, int count,
                                                    enum editorMetricsKind kind,
                                                    const char *group_key,
                                                    const struct editorMetricsCmdOptions *opts,
                                                    int *out_count) {
	*out_count = 0;
	if (count <= 0) {
		return NULL;
	}
	struct editorMetricsRow **group = (struct editorMetricsRow **)malloc(
	        (size_t)count * sizeof(struct editorMetricsRow *));
	if (group == NULL) {
		return NULL;
	}
	int n = 0;
	for (int i = 0; i < count; i++) {
		const struct editorMetricsRow *r = &rows[i];
		if (r->kind != kind || !rowPassesFilters(r, opts)) {
			continue;
		}
		const char *rk = kind == EDITOR_METRICS_KIND_FUZZ ? r->fuzz_target : r->bench_name;
		if (strcmp(rk, group_key) != 0) {
			continue;
		}
		group[n++] = (struct editorMetricsRow *)r;
	}
	qsort(group, (size_t)n, sizeof(struct editorMetricsRow *), compareByTsPtr);
	*out_count = n;
	return group;
}

static const char **collectGroupKeys(const struct editorMetricsRow *rows, int count,
                                     enum editorMetricsKind kind,
                                     const struct editorMetricsCmdOptions *opts, int *out_count) {
	*out_count = 0;
	if (count <= 0) {
		return NULL;
	}
	const char **keys = (const char **)malloc((size_t)count * sizeof(*keys));
	if (keys == NULL) {
		return NULL;
	}
	int n = 0;
	for (int i = 0; i < count; i++) {
		const struct editorMetricsRow *r = &rows[i];
		if (r->kind != kind || !rowPassesFilters(r, opts)) {
			continue;
		}
		const char *k = kind == EDITOR_METRICS_KIND_FUZZ ? r->fuzz_target : r->bench_name;
		if (k[0] == '\0') {
			continue;
		}
		int seen = 0;
		for (int j = 0; j < n; j++) {
			if (strcmp(keys[j], k) == 0) {
				seen = 1;
				break;
			}
		}
		if (!seen) {
			keys[n++] = k;
		}
	}
	*out_count = n;
	return keys;
}

/* Same-day collisions are disambiguated with "#2", "#3", ... so each
 * x-axis label stays unique. Caller frees both `*buf_out` (slot storage)
 * and the returned pointer array. */
static const char **buildDateLabels(struct editorMetricsRow **group, int n, char **buf_out) {
	*buf_out = NULL;
	if (n <= 0) {
		return NULL;
	}
	const size_t slot = 48;
	char *buf = (char *)malloc((size_t)n * slot);
	const char **labels = (const char **)malloc((size_t)n * sizeof(*labels));
	if (buf == NULL || labels == NULL) {
		free(buf);
		free(labels);
		return NULL;
	}
	char prev[32] = {0};
	int dup = 1;
	for (int i = 0; i < n; i++) {
		char base[32];
		formatDateLabel(group[i], base, sizeof(base));
		char *dst = buf + (size_t)i * slot;
		if (i > 0 && strcmp(base, prev) == 0) {
			dup++;
			(void)snprintf(dst, slot, "%s#%d", base, dup);
		} else {
			dup = 1;
			(void)snprintf(dst, slot, "%s", base);
		}
		(void)snprintf(prev, sizeof(prev), "%s", base);
		labels[i] = dst;
	}
	*buf_out = buf;
	return labels;
}

static int writeSvgFile(const char *out_dir, const char *filename, const char *title,
                        const char *y_unit, const char *const *x_labels, int n_points,
                        const struct editorSvgSeries *series, int n_series) {
	char path[1024];
	if ((int)snprintf(path, sizeof(path), "%s/%s", out_dir, filename) >= (int)sizeof(path)) {
		return -1;
	}
	FILE *f = fopen(path, "wb");
	if (f == NULL) {
		return -1;
	}
	editorMetricsRenderSvgChart(f, title, y_unit, x_labels, n_points, series, n_series);
	(void)fclose(f);
	return 0;
}

int editorMetricsCmdRenderSvg(const struct editorMetricsRow *rows, int count,
                              const struct editorMetricsCmdOptions *opts, int points_limit,
                              const char *out_dir) {
	if (opts == NULL || out_dir == NULL) {
		return -1;
	}
	if (points_limit <= 0) {
		points_limit = RENDER_DEFAULT_POINTS;
	}
	if (points_limit > RENDER_MAX_POINTS) {
		points_limit = RENDER_MAX_POINTS;
	}
	if (mkdir(out_dir, 0755) != 0) {
		struct stat st;
		if (stat(out_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
			return -1;
		}
	}

	char manifest_path[1024];
	if ((int)snprintf(manifest_path, sizeof(manifest_path), "%s/index.txt", out_dir) >=
	    (int)sizeof(manifest_path)) {
		return -1;
	}
	FILE *manifest = fopen(manifest_path, "wb");
	if (manifest == NULL) {
		return -1;
	}

	int n_written = 0;
	double bufs[3][RENDER_MAX_POINTS];

	if (opts->kind_filter == EDITOR_METRICS_KIND_UNKNOWN ||
	    opts->kind_filter == EDITOR_METRICS_KIND_TEST_RUN) {
		struct editorMetricsRow **group = (struct editorMetricsRow **)malloc(
		        (size_t)(count > 0 ? count : 1) * sizeof(struct editorMetricsRow *));
		int gc = 0;
		if (group != NULL) {
			for (int i = 0; i < count; i++) {
				const struct editorMetricsRow *r = &rows[i];
				if (r->kind != EDITOR_METRICS_KIND_TEST_RUN ||
				    !rowPassesFilters(r, opts)) {
					continue;
				}
				group[gc++] = (struct editorMetricsRow *)r;
			}
			qsort(group, (size_t)gc, sizeof(struct editorMetricsRow *), compareByTsPtr);
		}
		if (gc >= 2) {
			int start = gc > points_limit ? gc - points_limit : 0;
			int n = gc - start;
			char *label_buf = NULL;
			const char **labels = buildDateLabels(&group[start], n, &label_buf);

			/* Cost chart. exec_seconds_total (summed per-test time) is the
			 * primary series because, unlike whole-suite wall clock, it's
			 * independent of --jobs and runner core count. A small rolling
			 * median damps single-run jitter on shared CI runners. wall is
			 * overlaid (also smoothed) for context. The title annotates jobs
			 * so a --jobs config change is visible rather than silently
			 * stepping the wall trend. */
			for (int j = 0; j < n; j++) {
				bufs[2][j] = group[start + j]->exec_seconds_total;
			}
			editorMetricsRollingMedian(bufs[2], n, 5, bufs[0]);
			for (int j = 0; j < n; j++) {
				bufs[2][j] = group[start + j]->wall_seconds;
			}
			editorMetricsRollingMedian(bufs[2], n, 5, bufs[1]);
			struct editorSvgSeries ser_cost[2];
			ser_cost[0].values = bufs[0];
			ser_cost[0].label = "exec total";
			ser_cost[0].color = COLOR_PRIMARY;
			ser_cost[1].values = bufs[1];
			ser_cost[1].label = "wall";
			ser_cost[1].color = COLOR_SECONDARY;

			long long jobs0 = group[start]->jobs;
			int jobs_uniform = 1;
			for (int j = 1; j < n; j++) {
				if (group[start + j]->jobs != jobs0) {
					jobs_uniform = 0;
					break;
				}
			}
			char cost_title[64];
			if (!jobs_uniform) {
				(void)snprintf(cost_title, sizeof(cost_title),
				               "Test runtime (jobs varies, median)");
			} else if (jobs0 > 0) {
				(void)snprintf(cost_title, sizeof(cost_title),
				               "Test runtime (jobs=%lld, median)", jobs0);
			} else {
				(void)snprintf(cost_title, sizeof(cost_title),
				               "Test runtime (median)");
			}
			if (writeSvgFile(out_dir, "test-wall-seconds.svg", cost_title, "seconds",
			                 labels, n, ser_cost, 2) == 0) {
				(void)fprintf(manifest, "test-wall-seconds.svg\n");
				n_written++;
			}

			int stab_all_zero = 1;
			for (int j = 0; j < n; j++) {
				bufs[0][j] = (double)group[start + j]->crashes;
				bufs[1][j] = (double)group[start + j]->failed_unique;
				if (group[start + j]->crashes != 0 ||
				    group[start + j]->failed_unique != 0) {
					stab_all_zero = 0;
				}
			}
			struct editorSvgSeries ser_stab[2];
			ser_stab[0].values = bufs[0];
			ser_stab[0].label = "crashes";
			ser_stab[0].color = COLOR_PRIMARY;
			ser_stab[1].values = bufs[1];
			ser_stab[1].label = "failed";
			ser_stab[1].color = COLOR_SECONDARY;
			/* When the whole window is green, crashes and failed both sit on
			 * y=0 and overlap indistinguishably. Say so in the title so a clean
			 * history reads as healthy at a glance rather than looking empty. */
			char stab_title[64];
			if (stab_all_zero) {
				(void)snprintf(stab_title, sizeof(stab_title),
				               "Test stability (no failures in %d runs)", n);
			} else {
				(void)snprintf(stab_title, sizeof(stab_title), "Test stability");
			}
			if (writeSvgFile(out_dir, "test-stability.svg", stab_title, "count", labels,
			                 n, ser_stab, 2) == 0) {
				(void)fprintf(manifest, "test-stability.svg\n");
				n_written++;
			}

			/* Pass rate normalizes failures against suite size so the trend
			 * stays comparable as cases are added: 2 failures out of 50 reads
			 * very differently from 2 out of 1500. */
			for (int j = 0; j < n; j++) {
				bufs[0][j] = editorMetricsPassRate(group[start + j]->passed_runs,
				                                   group[start + j]->total_runs);
			}
			struct editorSvgSeries ser_pass;
			ser_pass.values = bufs[0];
			ser_pass.label = "pass %";
			ser_pass.color = COLOR_PRIMARY;
			if (writeSvgFile(out_dir, "test-pass-rate.svg", "Test pass rate", "%",
			                 labels, n, &ser_pass, 1) == 0) {
				(void)fprintf(manifest, "test-pass-rate.svg\n");
				n_written++;
			}

			/* Flakiness lives on its own chart sourced from --repeat>1 rows
			 * (the nightly flake-hunt lane). Per-commit rows run --repeat 1
			 * where pass-and-fail-in-one-run is structurally impossible, so
			 * mixing them in would pin the series to zero and break the
			 * shared x-axis with the per-commit stability rows above. */
			struct editorMetricsRow **flake_group = (struct editorMetricsRow **)malloc(
			        (size_t)(n > 0 ? n : 1) * sizeof(struct editorMetricsRow *));
			if (flake_group != NULL) {
				int fc = 0;
				for (int j = 0; j < n; j++) {
					if (group[start + j]->repeat > 1) {
						flake_group[fc++] = group[start + j];
					}
				}
				if (fc >= 2) {
					for (int j = 0; j < fc; j++) {
						bufs[0][j] = (double)flake_group[j]->flakes;
					}
					char *flake_label_buf = NULL;
					const char **flake_labels =
					        buildDateLabels(flake_group, fc, &flake_label_buf);
					struct editorSvgSeries ser_flake;
					ser_flake.values = bufs[0];
					ser_flake.label = "flakes";
					ser_flake.color = COLOR_TERTIARY;
					if (writeSvgFile(out_dir, "test-flakes.svg",
					                 "Test flakiness (--repeat soak)", "count",
					                 flake_labels, fc, &ser_flake, 1) == 0) {
						(void)fprintf(manifest, "test-flakes.svg\n");
						n_written++;
					}
					free((void *)flake_labels);
					free(flake_label_buf);
				}
				free(flake_group);
			}

			free((void *)labels);
			free(label_buf);
		}
		free(group);
	}

	if (opts->kind_filter == EDITOR_METRICS_KIND_UNKNOWN ||
	    opts->kind_filter == EDITOR_METRICS_KIND_BENCH) {
		int n_keys = 0;
		const char **keys =
		        collectGroupKeys(rows, count, EDITOR_METRICS_KIND_BENCH, opts, &n_keys);
		for (int i = 0; i < n_keys; i++) {
			int gc = 0;
			struct editorMetricsRow **group = collectGroupSorted(
			        rows, count, EDITOR_METRICS_KIND_BENCH, keys[i], opts, &gc);
			if (group == NULL || gc < 2) {
				free(group);
				continue;
			}
			int start = gc > points_limit ? gc - points_limit : 0;
			int n = gc - start;
			for (int j = 0; j < n; j++) {
				bufs[0][j] = group[start + j]->p50_ns;
				bufs[1][j] = group[start + j]->p95_ns;
				bufs[2][j] = group[start + j]->min_ns;
			}
			char *label_buf = NULL;
			const char **labels = buildDateLabels(&group[start], n, &label_buf);
			struct editorSvgSeries ser[3];
			ser[0].values = bufs[0];
			ser[0].label = "p50";
			ser[0].color = COLOR_PRIMARY;
			ser[1].values = bufs[1];
			ser[1].label = "p95";
			ser[1].color = COLOR_SECONDARY;
			ser[2].values = bufs[2];
			ser[2].label = "min";
			ser[2].color = COLOR_TERTIARY;

			char title[256];
			(void)snprintf(title, sizeof(title), "%s — min / p50 / p95", keys[i]);
			char safe[128];
			sanitizeFilenameInto(keys[i], safe, sizeof(safe));
			char fname[160];
			(void)snprintf(fname, sizeof(fname), "bench-%s.svg", safe);
			if (writeSvgFile(out_dir, fname, title, "ns", labels, n, ser, 3) == 0) {
				(void)fprintf(manifest, "%s\n", fname);
				n_written++;
			}
			free(group);
			free((void *)labels);
			free(label_buf);
		}
		free(keys);
	}

	if (opts->kind_filter == EDITOR_METRICS_KIND_UNKNOWN ||
	    opts->kind_filter == EDITOR_METRICS_KIND_FUZZ) {
		int n_keys = 0;
		const char **keys =
		        collectGroupKeys(rows, count, EDITOR_METRICS_KIND_FUZZ, opts, &n_keys);
		for (int i = 0; i < n_keys; i++) {
			int gc = 0;
			struct editorMetricsRow **group = collectGroupSorted(
			        rows, count, EDITOR_METRICS_KIND_FUZZ, keys[i], opts, &gc);
			if (group == NULL || gc < 2) {
				free(group);
				continue;
			}
			int start = gc > points_limit ? gc - points_limit : 0;
			int n = gc - start;
			char *label_buf = NULL;
			const char **labels = buildDateLabels(&group[start], n, &label_buf);
			char safe[128];
			sanitizeFilenameInto(keys[i], safe, sizeof(safe));

			for (int j = 0; j < n; j++) {
				bufs[0][j] = (double)group[start + j]->cov_edges;
			}
			struct editorSvgSeries ser_cov;
			ser_cov.values = bufs[0];
			ser_cov.label = "cov_edges";
			ser_cov.color = COLOR_PRIMARY;
			char title_cov[256];
			(void)snprintf(title_cov, sizeof(title_cov), "%s — coverage edges",
			               keys[i]);
			char fname_cov[160];
			(void)snprintf(fname_cov, sizeof(fname_cov), "fuzz-%s-cov.svg", safe);
			if (writeSvgFile(out_dir, fname_cov, title_cov, "edges", labels, n,
			                 &ser_cov, 1) == 0) {
				(void)fprintf(manifest, "%s\n", fname_cov);
				n_written++;
			}

			for (int j = 0; j < n; j++) {
				bufs[0][j] = (double)group[start + j]->corpus_bytes;
			}
			struct editorSvgSeries ser_b;
			ser_b.values = bufs[0];
			ser_b.label = "corpus_bytes";
			ser_b.color = COLOR_PRIMARY;
			char title_b[256];
			(void)snprintf(title_b, sizeof(title_b), "%s — corpus bytes", keys[i]);
			char fname_b[160];
			(void)snprintf(fname_b, sizeof(fname_b), "fuzz-%s-corpus.svg", safe);
			if (writeSvgFile(out_dir, fname_b, title_b, "bytes", labels, n, &ser_b,
			                 1) == 0) {
				(void)fprintf(manifest, "%s\n", fname_b);
				n_written++;
			}

			/* runtime_seconds==0 collapses to a 0 sample rather than NaN/skip
			 * so the chart shape (n_points) matches the other fuzz charts. */
			for (int j = 0; j < n; j++) {
				const struct editorMetricsRow *r = group[start + j];
				bufs[0][j] = (r->runtime_seconds > 0)
				                     ? (double)r->executed_units /
				                               (double)r->runtime_seconds
				                     : 0.0;
			}
			struct editorSvgSeries ser_t;
			ser_t.values = bufs[0];
			ser_t.label = "exec/s";
			ser_t.color = COLOR_PRIMARY;
			char title_t[256];
			(void)snprintf(title_t, sizeof(title_t), "%s — fuzz throughput", keys[i]);
			char fname_t[160];
			(void)snprintf(fname_t, sizeof(fname_t), "fuzz-%s-throughput.svg", safe);
			if (writeSvgFile(out_dir, fname_t, title_t, "exec/s", labels, n, &ser_t,
			                 1) == 0) {
				(void)fprintf(manifest, "%s\n", fname_t);
				n_written++;
			}

			free(group);
			free((void *)labels);
			free(label_buf);
		}
		free(keys);
	}

	(void)fclose(manifest);
	return n_written;
}
