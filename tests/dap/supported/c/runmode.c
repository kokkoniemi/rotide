#include "runmode.h"

#include <string.h>

enum run_mode parse_mode(const char *arg) {
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
