/* nanosleep() is POSIX.1b; request it explicitly so the unit builds cleanly
 * under -std=c11 (strict ANSI) on its own, without leaning on another header. */
#define _POSIX_C_SOURCE 199309L

#include "worker.h"

#include <errno.h>
#include <stdio.h>
#include <time.h>

static void pause_ms(int ms) {
	struct timespec ts = {0};
	if (ms <= 0) {
		return;
	}
	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (long)(ms % 1000) * 1000000L;
	while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
	}
}

void *worker_main(void *arg) {
	struct worker_ctx *ctx = (struct worker_ctx *)arg;
	if (ctx == NULL) {
		return NULL;
	}
	ctx->checksum = 0;
	for (int i = 0; i < ctx->iterations; i++) {
		/* DAP_BP_WORKER_LOOP */
		ctx->progress = i + 1;
		ctx->checksum += ((ctx->thread_index + 1) * (i + 3)) % 17;
		if ((i % 9) == 0) {
			pause_ms(2);
		}
	}
	snprintf(ctx->status, sizeof(ctx->status), "worker-%d done (%d/%d)", ctx->thread_index,
	         ctx->progress, ctx->iterations);
	return NULL;
}
