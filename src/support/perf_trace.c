#include "support/perf_trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int g_perf_enabled = 0;

struct perfFrameAccum {
	long pump_us;
	long refresh_us;
	int bytes_pumped;
	int fds_ready;
};

static struct perfFrameAccum g_perf_frame;

long editorPerfMonotonicUs(void) {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		return 0;
	}
	return (long)ts.tv_sec * 1000000L + (long)(ts.tv_nsec / 1000L);
}

void editorPerfInit(void) {
	const char *v = getenv("ROTIDE_PERF");
	g_perf_enabled = v != NULL && v[0] == '1' && v[1] == '\0';
}

int editorPerfEnabled(void) {
	return g_perf_enabled;
}

void editorPerfBeginFrame(void) {
	if (!g_perf_enabled) {
		return;
	}
	g_perf_frame.pump_us = 0;
	g_perf_frame.refresh_us = 0;
	g_perf_frame.bytes_pumped = 0;
	g_perf_frame.fds_ready = 0;
}

void editorPerfRecordPumpBytes(int bytes) {
	if (!g_perf_enabled || bytes <= 0) {
		return;
	}
	g_perf_frame.bytes_pumped += bytes;
}

void editorPerfRecordPumpUs(long us) {
	if (!g_perf_enabled || us <= 0) {
		return;
	}
	g_perf_frame.pump_us += us;
}

void editorPerfRecordRefreshUs(long us) {
	if (!g_perf_enabled || us <= 0) {
		return;
	}
	g_perf_frame.refresh_us += us;
}

void editorPerfRecordFdsReady(int count) {
	if (!g_perf_enabled || count <= 0) {
		return;
	}
	g_perf_frame.fds_ready += count;
}

void editorPerfEndFrame(void) {
	if (!g_perf_enabled) {
		return;
	}
	long ms = editorPerfMonotonicUs() / 1000L;
	(void)fprintf(stderr, "ROTIDE_PERF,%ld,%ld,%ld,%d,%d\n", ms, g_perf_frame.pump_us,
	              g_perf_frame.refresh_us, g_perf_frame.bytes_pumped, g_perf_frame.fds_ready);
}
