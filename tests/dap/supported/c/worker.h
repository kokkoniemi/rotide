#ifndef DAP_SAMPLE_WORKER_H
#define DAP_SAMPLE_WORKER_H

struct worker_ctx {
	int thread_index;
	int iterations;
	volatile int progress;
	int checksum;
	char status[64];
};

void *worker_main(void *arg);

#endif
