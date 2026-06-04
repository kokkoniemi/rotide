#include "items.h"
#include "numeric.h"
#include "runmode.h"
#include "worker.h"

#include <pthread.h>
#include <stdio.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

static int g_counter = 7;
static volatile int g_watchdog = 0;
static const char *g_labels[] = {"alpha", "beta", "gamma"};

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
