#include "numeric.h"

int factorial_recursive(int n) {
	/* DAP_BP_FACTORIAL_RECURSE */
	if (n <= 1) {
		return 1;
	}
	return n * factorial_recursive(n - 1);
}

int fibonacci_iterative(int n) {
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

int sum_array(const int *values, size_t len) {
	int total = 0;
	for (size_t i = 0; i < len; i++) {
		total += values[i];
	}
	return total;
}

int safe_divide(int a, int b, int *out) {
	if (out == NULL) {
		return 0;
	}
	if (b == 0) {
		return 0;
	}
	*out = a / b;
	return 1;
}

int with_static_state(int x) {
	static int call_count = 0;
	call_count++;
	return x + call_count;
}
