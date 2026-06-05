#ifndef DAP_SAMPLE_NUMERIC_H
#define DAP_SAMPLE_NUMERIC_H

#include <stddef.h>

int factorial_recursive(int n);
int fibonacci_iterative(int n);
int sum_array(const int *values, size_t len);
int safe_divide(int a, int b, int *out);
int with_static_state(int x);

#endif
