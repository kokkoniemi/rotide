#include "language/syntax_worker.h"

#include "support/alloc.h"
#include "support/size_utils.h"
#include "text/row.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t g_syntax_worker_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_syntax_worker_cond = PTHREAD_COND_INITIALIZER;
static pthread_t g_syntax_worker_thread;
static int g_syntax_worker_started = 0;
static int g_syntax_worker_enabled = 0;
static int g_syntax_worker_stopping = 0;
static int g_syntax_worker_has_pending = 0;
static int g_syntax_worker_running = 0;
static struct editorSyntaxWorkerJob g_syntax_worker_pending = {0};
static struct editorSyntaxWorkerResult *g_syntax_worker_result = NULL;

static void editorSyntaxWorkerJobFree(struct editorSyntaxWorkerJob *job) {
	if (job == NULL) {
		return;
	}
	free(job->text);
	memset(job, 0, sizeof(*job));
}

void editorSyntaxWorkerResultDestroy(struct editorSyntaxWorkerResult *result) {
	if (result == NULL) {
		return;
	}
	editorSyntaxStateDestroy(result->state);
	free(result->span_counts);
	free(result->spans);
	free(result);
}

static int editorSyntaxWorkerLineStarts(const char *text, size_t len,
		size_t **starts_out, int *count_out) {
	if (starts_out == NULL || count_out == NULL) {
		return 0;
	}
	*starts_out = NULL;
	*count_out = 0;

	size_t line_count = 1;
	for (size_t i = 0; i < len; i++) {
		if (text[i] == '\n') {
			line_count++;
		}
	}
	if (line_count > (size_t)INT32_MAX) {
		return 0;
	}

	size_t bytes = 0;
	if (!editorSizeMul(sizeof(size_t), line_count, &bytes)) {
		return 0;
	}
	size_t *starts = editorMalloc(bytes);
	if (starts == NULL) {
		return 0;
	}
	starts[0] = 0;
	size_t row = 1;
	for (size_t i = 0; i < len; i++) {
		if (text[i] == '\n' && row < line_count) {
			starts[row++] = i + 1;
		}
	}

	*starts_out = starts;
	*count_out = (int)line_count;
	return 1;
}

static size_t editorSyntaxWorkerLineEnd(const char *text, size_t len,
		const size_t *starts, int line_count, int row) {
	size_t end = len;
	if (row + 1 < line_count) {
		end = starts[row + 1];
		if (end > 0 && text[end - 1] == '\n') {
			end--;
		}
	}
	if (end > len) {
		end = len;
	}
	return end;
}

static int editorSyntaxWorkerU32(size_t value, uint32_t *out) {
	if (out == NULL || value > UINT32_MAX) {
		return 0;
	}
	*out = (uint32_t)value;
	return 1;
}

static int editorSyntaxWorkerBuildSpans(struct editorSyntaxWorkerResult *result,
		const char *text, size_t len) {
	if (result == NULL || result->state == NULL || result->row_count <= 0) {
		return 1;
	}

	size_t row_count_size = 0;
	size_t span_rows = 0;
	size_t counts_bytes = 0;
	size_t spans_bytes = 0;
	if (!editorIntToSize(result->row_count, &row_count_size) ||
			!editorSizeMul(sizeof(*result->span_counts), row_count_size, &counts_bytes) ||
			!editorSizeMul(row_count_size, ROTIDE_MAX_SYNTAX_SPANS_PER_ROW, &span_rows) ||
			!editorSizeMul(sizeof(*result->spans), span_rows, &spans_bytes)) {
		return 0;
	}

	result->span_counts = editorMalloc(counts_bytes);
	result->spans = editorMalloc(spans_bytes);
	if (result->span_counts == NULL || result->spans == NULL) {
		return 0;
	}
	memset(result->span_counts, 0, counts_bytes);

	size_t *line_starts = NULL;
	int line_count = 0;
	if (!editorSyntaxWorkerLineStarts(text, len, &line_starts, &line_count)) {
		return 0;
	}

	struct editorTextSource source = {0};
	editorTextSourceInitString(&source, text, len);
	int capture_limit = ROTIDE_MAX_SYNTAX_SPANS_PER_ROW * 3;
	struct editorSyntaxCapture *captures = NULL;
	size_t capture_bytes = 0;
	if (!editorSizeMul(sizeof(*captures), (size_t)capture_limit, &capture_bytes)) {
		free(line_starts);
		return 0;
	}
	captures = editorMalloc(capture_bytes);
	if (captures == NULL) {
		free(line_starts);
		return 0;
	}

	for (int rel_row = 0; rel_row < result->row_count; rel_row++) {
		int row_idx = result->first_row + rel_row;
		if (row_idx < 0 || row_idx >= line_count) {
			continue;
		}

		size_t row_start = line_starts[row_idx];
		size_t row_end = editorSyntaxWorkerLineEnd(text, len, line_starts, line_count, row_idx);
		uint32_t start_byte = 0;
		uint32_t end_byte = 0;
		if (!editorSyntaxWorkerU32(row_start, &start_byte) ||
				!editorSyntaxWorkerU32(row_end, &end_byte) ||
				start_byte >= end_byte) {
			continue;
		}

		struct erow row = {0};
		row.chars = (char *)&text[row_start];
		row.size = (int)(row_end - row_start);
		if (!editorRowBuildRender(row.chars, row.size, &row.render, &row.rsize,
					&row.render_display_cols)) {
			free(captures);
			free(line_starts);
			return 0;
		}

		int capture_count = 0;
		if (!editorSyntaxStateCollectCapturesForRange(result->state, &source, start_byte,
					end_byte, captures, capture_limit, &capture_count)) {
			free(row.render);
			free(captures);
			free(line_starts);
			return 0;
		}

		int span_base = rel_row * ROTIDE_MAX_SYNTAX_SPANS_PER_ROW;
		for (int cap_idx = 0; cap_idx < capture_count; cap_idx++) {
			struct editorSyntaxCapture capture = captures[cap_idx];
			if (capture.highlight_class == EDITOR_SYNTAX_HL_NONE ||
					capture.end_byte <= capture.start_byte) {
				continue;
			}

			int slot = result->span_counts[rel_row];
			if (slot >= ROTIDE_MAX_SYNTAX_SPANS_PER_ROW) {
				editorSyntaxStateRecordCaptureTruncated(result->state, row_idx);
				continue;
			}

			int local_start = (int)(capture.start_byte - start_byte);
			int local_end = (int)(capture.end_byte - start_byte);
			if (local_start < 0) {
				local_start = 0;
			}
			if (local_start > row.size) {
				local_start = row.size;
			}
			if (local_end < 0) {
				local_end = 0;
			}
			if (local_end > row.size) {
				local_end = row.size;
			}

			local_start = editorRowClampCxToCharBoundary(&row, local_start);
			local_end = editorRowClampCxToCharBoundary(&row, local_end);
			if (local_end <= local_start && local_end < row.size) {
				local_end = editorRowNextCharIdx(&row, local_end);
			}
			if (local_end <= local_start) {
				continue;
			}

			int render_start = editorRowCxToRenderIdx(&row, local_start);
			int render_end = editorRowCxToRenderIdx(&row, local_end);
			if (render_end <= render_start) {
				continue;
			}

			result->spans[span_base + slot].start_render_idx = render_start;
			result->spans[span_base + slot].end_render_idx = render_end;
			result->spans[span_base + slot].highlight_class = capture.highlight_class;
			result->span_counts[rel_row] = slot + 1;
		}

		free(row.render);
	}

	free(captures);
	free(line_starts);
	return 1;
}

static struct editorSyntaxWorkerResult *editorSyntaxWorkerRunJob(
		struct editorSyntaxWorkerJob *job) {
	struct editorSyntaxWorkerResult *result = editorMalloc(sizeof(*result));
	if (result == NULL) {
		return NULL;
	}
	memset(result, 0, sizeof(*result));
	result->language = job->language;
	result->revision = job->revision;
	result->generation = job->generation;
	result->first_row = job->first_row;
	result->row_count = job->row_count;

	result->state = editorSyntaxStateCreate(job->language);
	if (result->state == NULL) {
		return result;
	}

	struct editorTextSource source = {0};
	editorTextSourceInitString(&source, job->text, job->text_len);
	result->parsed = editorSyntaxStateParseFull(result->state, &source);
	if (result->parsed && !editorSyntaxWorkerBuildSpans(result, job->text, job->text_len)) {
		result->parsed = 0;
	}
	if (!result->parsed) {
		editorSyntaxStateDestroy(result->state);
		result->state = NULL;
		free(result->span_counts);
		free(result->spans);
		result->span_counts = NULL;
		result->spans = NULL;
	}
	return result;
}

static void *editorSyntaxWorkerMain(void *arg) {
	(void)arg;
	while (1) {
		struct editorSyntaxWorkerJob job = {0};
		pthread_mutex_lock(&g_syntax_worker_mutex);
		while (!g_syntax_worker_stopping && !g_syntax_worker_has_pending) {
			pthread_cond_wait(&g_syntax_worker_cond, &g_syntax_worker_mutex);
		}
		if (g_syntax_worker_stopping && !g_syntax_worker_has_pending) {
			pthread_mutex_unlock(&g_syntax_worker_mutex);
			break;
		}
		job = g_syntax_worker_pending;
		memset(&g_syntax_worker_pending, 0, sizeof(g_syntax_worker_pending));
		g_syntax_worker_has_pending = 0;
		g_syntax_worker_running = 1;
		pthread_mutex_unlock(&g_syntax_worker_mutex);

		struct editorSyntaxWorkerResult *result = editorSyntaxWorkerRunJob(&job);
		editorSyntaxWorkerJobFree(&job);

		pthread_mutex_lock(&g_syntax_worker_mutex);
		editorSyntaxWorkerResultDestroy(g_syntax_worker_result);
		g_syntax_worker_result = result;
		g_syntax_worker_running = 0;
		pthread_cond_broadcast(&g_syntax_worker_cond);
		pthread_mutex_unlock(&g_syntax_worker_mutex);
	}
	return NULL;
}

int editorSyntaxBackgroundStart(void) {
	pthread_mutex_lock(&g_syntax_worker_mutex);
	if (g_syntax_worker_started) {
		g_syntax_worker_enabled = 1;
		pthread_mutex_unlock(&g_syntax_worker_mutex);
		return 1;
	}
	g_syntax_worker_stopping = 0;
	pthread_mutex_unlock(&g_syntax_worker_mutex);
	if (pthread_create(&g_syntax_worker_thread, NULL, editorSyntaxWorkerMain, NULL) != 0) {
		return 0;
	}
	pthread_mutex_lock(&g_syntax_worker_mutex);
	g_syntax_worker_started = 1;
	g_syntax_worker_enabled = 1;
	pthread_mutex_unlock(&g_syntax_worker_mutex);
	return 1;
}

void editorSyntaxBackgroundStop(void) {
	pthread_mutex_lock(&g_syntax_worker_mutex);
	if (!g_syntax_worker_started) {
		g_syntax_worker_enabled = 0;
		editorSyntaxWorkerJobFree(&g_syntax_worker_pending);
		g_syntax_worker_has_pending = 0;
		editorSyntaxWorkerResultDestroy(g_syntax_worker_result);
		g_syntax_worker_result = NULL;
		pthread_mutex_unlock(&g_syntax_worker_mutex);
		return;
	}
	g_syntax_worker_enabled = 0;
	g_syntax_worker_stopping = 1;
	pthread_cond_broadcast(&g_syntax_worker_cond);
	pthread_mutex_unlock(&g_syntax_worker_mutex);

	pthread_join(g_syntax_worker_thread, NULL);

	pthread_mutex_lock(&g_syntax_worker_mutex);
	g_syntax_worker_started = 0;
	g_syntax_worker_stopping = 0;
	g_syntax_worker_running = 0;
	editorSyntaxWorkerJobFree(&g_syntax_worker_pending);
	g_syntax_worker_has_pending = 0;
	editorSyntaxWorkerResultDestroy(g_syntax_worker_result);
	g_syntax_worker_result = NULL;
	pthread_mutex_unlock(&g_syntax_worker_mutex);
}

int editorSyntaxBackgroundEnabled(void) {
	pthread_mutex_lock(&g_syntax_worker_mutex);
	int enabled = g_syntax_worker_enabled && g_syntax_worker_started;
	pthread_mutex_unlock(&g_syntax_worker_mutex);
	return enabled;
}

void editorSyntaxBackgroundSetEnabledForTests(int enabled) {
	if (enabled) {
		(void)editorSyntaxBackgroundStart();
	} else {
		editorSyntaxBackgroundStop();
	}
}

int editorSyntaxWorkerSchedule(struct editorSyntaxWorkerJob *job) {
	if (job == NULL || job->text == NULL || job->language == EDITOR_SYNTAX_NONE) {
		return 0;
	}
	pthread_mutex_lock(&g_syntax_worker_mutex);
	if (!g_syntax_worker_enabled || !g_syntax_worker_started) {
		pthread_mutex_unlock(&g_syntax_worker_mutex);
		return 0;
	}
	editorSyntaxWorkerJobFree(&g_syntax_worker_pending);
	g_syntax_worker_pending = *job;
	memset(job, 0, sizeof(*job));
	g_syntax_worker_has_pending = 1;
	pthread_cond_broadcast(&g_syntax_worker_cond);
	pthread_mutex_unlock(&g_syntax_worker_mutex);
	return 1;
}

struct editorSyntaxWorkerResult *editorSyntaxWorkerTakeResult(void) {
	pthread_mutex_lock(&g_syntax_worker_mutex);
	struct editorSyntaxWorkerResult *result = g_syntax_worker_result;
	g_syntax_worker_result = NULL;
	pthread_mutex_unlock(&g_syntax_worker_mutex);
	return result;
}

int editorSyntaxWorkerHasWork(void) {
	pthread_mutex_lock(&g_syntax_worker_mutex);
	int has_work = g_syntax_worker_has_pending || g_syntax_worker_running ||
			g_syntax_worker_result != NULL;
	pthread_mutex_unlock(&g_syntax_worker_mutex);
	return has_work;
}

void editorSyntaxWorkerWaitForIdle(void) {
	pthread_mutex_lock(&g_syntax_worker_mutex);
	while (g_syntax_worker_has_pending || g_syntax_worker_running) {
		pthread_cond_wait(&g_syntax_worker_cond, &g_syntax_worker_mutex);
	}
	pthread_mutex_unlock(&g_syntax_worker_mutex);
}
