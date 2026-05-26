#ifndef TESTS_METRICS_RENDER_SVG_H
#define TESTS_METRICS_RENDER_SVG_H

#include "metrics_jsonl_read.h"
#include "metrics_summary_cmd.h"

#include <stdio.h>

/* One data series on a chart. `values` has exactly `n_points` doubles
 * (matches the x_labels passed to editorMetricsRenderSvgChart). `label`
 * shows in the chart's legend; `color` is a hex string like "#1f77b4". */
struct editorSvgSeries {
	const double *values;
	const char *label;
	const char *color;
};

/* Low-level: render one line chart as SVG to `out`. All data series share
 * the same x-axis (i.e. same `n_points`). The chart auto-scales the
 * y-axis based on all series; x-axis ticks are thinned to ~6 labels so
 * date strings stay readable regardless of n_points. */
void editorMetricsRenderSvgChart(FILE *out, const char *title, const char *y_unit,
                                 const char *const *x_labels, int n_points,
                                 const struct editorSvgSeries *series, int n_series);

/* High-level: scan rows for bench/fuzz series with >= 2 points, write one
 * SVG per chart into `out_dir` plus a tiny `index.txt` manifest listing
 * the files. Filenames:
 *   bench-<sanitized_name>.svg
 *   fuzz-<target>-cov.svg
 *   fuzz-<target>-corpus.svg
 * `points_limit` caps points-per-series (<=0 → 30, hard cap 60). Filter
 * fields on `opts` are honored. Returns the number of SVG files written,
 * or -1 on I/O failure. */
int editorMetricsCmdRenderSvg(const struct editorMetricsRow *rows, int count,
                              const struct editorMetricsCmdOptions *opts, int points_limit,
                              const char *out_dir);

#endif
