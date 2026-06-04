#ifndef DAP_SAMPLE_RUNMODE_H
#define DAP_SAMPLE_RUNMODE_H

enum run_mode {
	RUN_MODE_NORMAL = 0,
	RUN_MODE_BRANCH_A,
	RUN_MODE_BRANCH_B,
	RUN_MODE_ZERO_DIVISION,
};

enum run_mode parse_mode(const char *arg);

#endif
