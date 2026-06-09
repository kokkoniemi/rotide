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

/* Pass rate as a percentage: passed_runs / total_runs * 100. Normalizes the
 * failure signal against suite size so it stays comparable as cases are added.
 * Returns 100.0 when total_runs <= 0 (no runs means nothing failed). */
double editorMetricsPassRate(long long passed_runs, long long total_runs);

/* Centered rolling median used to damp runner jitter on the cost chart.
 * `window` is clamped to odd and >= 1; near the edges the window shrinks to the
 * available neighbours so the endpoints are preserved. Writes `n_points`
 * medians to `out`, which must not alias `in`. Deterministic. */
void editorMetricsRollingMedian(const double *in, int n_points, int window, double *out);

/* High-level: scan rows for bench/fuzz series with >= 2 points, write one
 * SVG per chart into `out_dir` plus a tiny `index.txt` manifest listing
 * the files. Filenames:
 *   bench-<sanitized_name>.svg
 *   fuzz-<target>-cov.svg
 *   fuzz-<target>-corpus.svg
 *   loc-first-party-by-domain.svg / loc-first-party-total.svg
 *   loc-churn-by-domain.svg / loc-vendor.svg / loc-tests.svg
 * `points_limit` caps points-per-series (<=0 → 30, hard cap 60). Filter
 * fields on `opts` are honored. Returns the number of SVG files written,
 * or -1 on I/O failure. */
int editorMetricsCmdRenderSvg(const struct editorMetricsRow *rows, int count,
                              const struct editorMetricsCmdOptions *opts, int points_limit,
                              const char *out_dir);

#endif
