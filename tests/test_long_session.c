#include "test_case.h"
#include "test_helpers.h"

#include "editing/edit.h"
#include "editing/history.h"
#include "workspace/tabs.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

#ifdef __GLIBC__
#include <malloc.h>
#endif

static const char *k_session_fixture =
	"#include <stdio.h>\n"
	"\n"
	"int main(int argc, char **argv) {\n"
	"    for (int j = 0; j < argc; j++) {\n"
	"        printf(\"%s\\n\", argv[j]);\n"
	"    }\n"
	"    return 0;\n"
	"}\n";

static long current_rss_kib(void) {
	struct rusage ru;
	if (getrusage(RUSAGE_SELF, &ru) != 0) {
		return -1;
	}
	/* ru_maxrss is in KiB on Linux. */
	return ru.ru_maxrss;
}

static long current_live_alloc_bytes(void) {
#ifdef __GLIBC__
	struct mallinfo2 mi = mallinfo2();
	return (long)mi.uordblks;
#else
	return -1;
#endif
}

static int write_session_fixture(char *path_template) {
	int fd = mkstemp(path_template);
	if (fd == -1) {
		return 0;
	}
	int ok = write_all(fd, k_session_fixture, strlen(k_session_fixture)) == 0;
	if (close(fd) == -1) {
		ok = 0;
	}
	return ok;
}

/* Drive one open/edit/close iteration. Returns 0 on success, 1 on failure. */
static int run_open_edit_close(const char *path) {
	if (!editorTabOpenOrSwitchToFile(path)) {
		return 1;
	}
	/* Touch the buffer so undo history, syntax, and the dirty path all
	 * participate in the cycle. */
	editorInsertChar('x');
	editorUndo();
	if (!editorTabCloseActive()) {
		return 1;
	}
	return 0;
}

/* Slop bounds: glibc retains arenas; ASan retains shadow-mapped state;
 * background syntax workers warm up their queues. We are asserting "trend
 * to zero, not slope," so the bounds need to be wide enough to absorb
 * legitimate steady-state caches but narrow enough to catch per-iteration
 * leaks (which would produce delta >= K * leak_per_iter). At K=200, a
 * 1 KiB-per-iter leak would dwarf these bounds.
 */
#define LONG_SESSION_WARMUP_ITERATIONS 20
#define LONG_SESSION_MEASURED_ITERATIONS 200

/* ASan + UBSan shadow memory grows in lockstep with heap mappings; RSS under
 * sanitizers is noisier and runs roughly an order of magnitude wider per
 * iteration than the native build. mallinfo2's uordblks is unaffected.
 */
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define LONG_SESSION_MAX_RSS_GROWTH_KIB 32768             /* 32 MiB under sanitizers */
#else
#define LONG_SESSION_MAX_RSS_GROWTH_KIB 2048              /* 2 MiB; catches ~10 KiB/iter */
#endif
#define LONG_SESSION_MAX_LIVE_GROWTH_BYTES (256 * 1024)   /* 256 KiB; catches ~1.3 KiB/iter */

static int test_long_session_open_edit_close_cycles_have_flat_memory(void) {
	char path[] = "/tmp/rotide-longsess-XXXXXX";
	ASSERT_TRUE(write_session_fixture(path));

	for (int i = 0; i < LONG_SESSION_WARMUP_ITERATIONS; i++) {
		ASSERT_EQ_INT(0, run_open_edit_close(path));
	}

	long baseline_rss = current_rss_kib();
	long baseline_live = current_live_alloc_bytes();

	for (int i = 0; i < LONG_SESSION_MEASURED_ITERATIONS; i++) {
		ASSERT_EQ_INT(0, run_open_edit_close(path));
	}

	long final_rss = current_rss_kib();
	long final_live = current_live_alloc_bytes();

	long rss_delta = (baseline_rss >= 0 && final_rss >= 0)
		? final_rss - baseline_rss : 0;
	long live_delta = (baseline_live >= 0 && final_live >= 0)
		? final_live - baseline_live : 0;

	int failed = 0;
	if (getenv("ROTIDE_LONG_SESSION_REPORT") != NULL) {
		fprintf(stderr,
			"long_session_report: iterations=%d "
			"rss_baseline=%ld KiB rss_final=%ld KiB rss_delta=%ld KiB "
			"live_baseline=%ld live_final=%ld live_delta=%ld\n",
			LONG_SESSION_MEASURED_ITERATIONS,
			baseline_rss, final_rss, rss_delta,
			baseline_live, final_live, live_delta);
	}
	if (rss_delta > LONG_SESSION_MAX_RSS_GROWTH_KIB) {
		fprintf(stderr,
			"long_session: RSS grew %ld KiB over %d iterations "
			"(baseline=%ld KiB final=%ld KiB max=%d KiB)\n",
			rss_delta, LONG_SESSION_MEASURED_ITERATIONS,
			baseline_rss, final_rss, LONG_SESSION_MAX_RSS_GROWTH_KIB);
		failed = 1;
	}
	if (baseline_live >= 0 && live_delta > LONG_SESSION_MAX_LIVE_GROWTH_BYTES) {
		fprintf(stderr,
			"long_session: live alloc bytes grew %ld over %d iterations "
			"(baseline=%ld final=%ld max=%d)\n",
			live_delta, LONG_SESSION_MEASURED_ITERATIONS,
			baseline_live, final_live, LONG_SESSION_MAX_LIVE_GROWTH_BYTES);
		failed = 1;
	}

	(void)unlink(path);
	return failed;
}

const struct editorTestCase g_long_session_tests[] = {
	{"long_session_open_edit_close_cycles_have_flat_memory",
		test_long_session_open_edit_close_cycles_have_flat_memory},
};

const int g_long_session_test_count =
	(int)(sizeof(g_long_session_tests) / sizeof(g_long_session_tests[0]));
