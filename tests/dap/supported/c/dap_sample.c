#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

enum run_mode {
	RUN_MODE_NORMAL = 0,
	RUN_MODE_BRANCH_A,
	RUN_MODE_BRANCH_B,
	RUN_MODE_ZERO_DIVISION,
};

struct item {
	int id;
	char name[32];
	double score;
	struct item *next;
};

struct worker_ctx {
	int thread_index;
	int iterations;
	volatile int progress;
	int checksum;
	char status[64];
};

static int g_counter = 7;
static volatile int g_watchdog = 0;
static const char *g_labels[] = {"alpha", "beta", "gamma"};

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

static enum run_mode parse_mode(const char *arg) {
	if (arg == NULL) {
		return RUN_MODE_NORMAL;
	}
	if (strcmp(arg, "branch-a") == 0) {
		return RUN_MODE_BRANCH_A;
	}
	if (strcmp(arg, "branch-b") == 0) {
		return RUN_MODE_BRANCH_B;
	}
	if (strcmp(arg, "zero-div") == 0) {
		return RUN_MODE_ZERO_DIVISION;
	}
	return RUN_MODE_NORMAL;
}

static struct item *item_new(int id, const char *name, double score) {
	struct item *node = calloc(1, sizeof(*node));
	if (node == NULL) {
		return NULL;
	}
	node->id = id;
	node->score = score;
	snprintf(node->name, sizeof(node->name), "%s", name != NULL ? name : "");
	return node;
}

static void item_list_free(struct item *head) {
	while (head != NULL) {
		struct item *next = head->next;
		free(head);
		head = next;
	}
}

static struct item *build_demo_list(void) {
	struct item *a = item_new(1, "first", 10.5);
	struct item *b = item_new(2, "second", 22.75);
	struct item *c = item_new(3, "third", 35.125);
	if (a == NULL || b == NULL || c == NULL) {
		item_list_free(a);
		item_list_free(b);
		item_list_free(c);
		return NULL;
	}
	a->next = b;
	b->next = c;
	return a;
}

static int factorial_recursive(int n) {
	/* DAP_BP_FACTORIAL_RECURSE */
	if (n <= 1) {
		return 1;
	}
	return n * factorial_recursive(n - 1);
}

static int fibonacci_iterative(int n) {
	int a = 0;
	int b = 1;
	for (int i = 0; i < n; i++) {
		/* DAP_BP_FIB_LOOP */
		int next = a + b;
		a = b;
		b = next;
	}
	return a;
}

static int sum_array(const int *values, size_t len) {
	int total = 0;
	for (size_t i = 0; i < len; i++) {
		total += values[i];
	}
	return total;
}

static int safe_divide(int a, int b, int *out) {
	if (out == NULL) {
		return 0;
	}
	if (b == 0) {
		return 0;
	}
	*out = a / b;
	return 1;
}

static int with_static_state(int x) {
	static int call_count = 0;
	call_count++;
	return x + call_count;
}

static int pointer_walk(const struct item *head, int target_id) {
	const struct item *it = head;
	while (it != NULL) {
		if (it->id == target_id) {
			return (int)it->score;
		}
		it = it->next;
	}
	return -1;
}

static int mutate_item(struct item *node, int delta) {
	/* DAP_BP_MUTATE_ITEM */
	if (node == NULL) {
		return 0;
	}
	node->id += delta;
	node->score += (double)delta * 0.25;
	return node->id;
}

static void *worker_main(void *arg) {
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

int main(int argc, char **argv) {
	/* DAP_BP_MAIN_ENTRY */
	setvbuf(stdout, NULL, _IOLBF, 0);
	enum run_mode mode = parse_mode(argc > 1 ? argv[1] : NULL);

	int numbers[] = {3, 5, 8, 13, 21, 34};
	struct item *items = build_demo_list();
	if (items == NULL) {
		fprintf(stderr, "failed to allocate demo list\n");
		return 2;
	}

	int rec = factorial_recursive(6);
	int fib = fibonacci_iterative(12);
	int total = sum_array(numbers, ARRAY_LEN(numbers));
	int found_score = pointer_walk(items, 2);
	int static_a = with_static_state(7);
	int static_b = with_static_state(11);

	int quotient = 0;
	int divide_ok = safe_divide(total, mode == RUN_MODE_ZERO_DIVISION ? 0 : 3, &quotient);

	struct worker_ctx ctx = {
	        .thread_index = 1,
	        .iterations = 40,
	        .progress = 0,
	        .checksum = 0,
	        .status = "",
	};
	pthread_t worker;
	int worker_started = (pthread_create(&worker, NULL, worker_main, &ctx) == 0);

	int branch_result = 0;
	if (mode == RUN_MODE_BRANCH_A) {
		/* DAP_BP_BRANCH_A */
		branch_result = mutate_item(items->next, 3) + quotient + total;
	} else if (mode == RUN_MODE_BRANCH_B) {
		/* DAP_BP_BRANCH_B */
		branch_result = mutate_item(items, -1) + rec + fib;
	} else if (mode == RUN_MODE_ZERO_DIVISION) {
		/* DAP_BP_ZERO_DIVISION_PATH */
		branch_result = divide_ok ? quotient : -999;
	} else {
		branch_result = found_score + static_a + static_b;
	}

	if (worker_started) {
		(void)pthread_join(worker, NULL);
	}

	int labels_len = (int)ARRAY_LEN(g_labels);
	int debug_sink = rec + fib + total + found_score + static_a + static_b + branch_result +
	                 ctx.checksum + (int)ctx.progress + labels_len + g_counter;
	if (divide_ok) {
		debug_sink += quotient;
	}
	if (items->next != NULL && items->next->next != NULL) {
		debug_sink += (int)items->next->next->score;
	}
	g_watchdog = debug_sink;

	printf("mode=%d rec=%d fib=%d total=%d quotient=%d divide_ok=%d\n", (int)mode, rec, fib,
	       total, quotient, divide_ok);
	printf("found_score=%d static=(%d,%d) branch=%d\n", found_score, static_a, static_b,
	       branch_result);
	printf("worker: %s checksum=%d progress=%d\n", ctx.status, ctx.checksum, (int)ctx.progress);

	/* DAP_BP_BEFORE_EXIT */
	printf("debug_sink=%d watchdog=%d\n", debug_sink, (int)g_watchdog);

	item_list_free(items);
	return 0;
}
