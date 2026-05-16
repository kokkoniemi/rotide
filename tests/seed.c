#include "seed.h"

static unsigned long long g_seed = 0;

unsigned long long rotide_test_seed(void) {
	return g_seed;
}

void rotide_test_seed_set(unsigned long long seed) {
	g_seed = seed;
}
