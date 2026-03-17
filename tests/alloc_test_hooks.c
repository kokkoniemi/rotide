#include "alloc_test_hooks.h"

#include "alloc_control.h"

static int editor_alloc_fail_after = -1;

static int editorTestAllocShouldFail(void) {
	if (editor_alloc_fail_after < 0) {
		return 0;
	}
	if (editor_alloc_fail_after == 0) {
		return 1;
	}

	editor_alloc_fail_after--;
	return 0;
}

void editorTestAllocFailAfter(int successful_allocations_before_failure) {
	editor_alloc_fail_after = successful_allocations_before_failure;
	if (successful_allocations_before_failure < 0) {
		editorAllocClearFailureProbe();
		return;
	}

	editorAllocSetFailureProbe(editorTestAllocShouldFail);
}

void editorTestAllocReset(void) {
	editor_alloc_fail_after = -1;
	editorAllocClearFailureProbe();
}
