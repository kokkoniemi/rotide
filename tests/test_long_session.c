#include "test_case.h"
#include "test_helpers.h"
#include "test_support.h"

#include "editing/edit.h"
#include "editing/history.h"
#include "language/lsp.h"
#include "language/syntax.h"
#include "terminal/terminal_pane.h"
#include "workspace/tabs.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
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

/* ASan + UBSan shadow memory grows in lockstep with heap mappings; RSS under
 * sanitizers is noisier and runs roughly an order of magnitude wider per
 * iteration than the native build. mallinfo2's uordblks is unaffected.
 */
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define LONG_SESSION_DEFAULT_RSS_KIB 32768            /* 32 MiB under sanitizers */
#else
#define LONG_SESSION_DEFAULT_RSS_KIB 2048             /* 2 MiB; catches ~10 KiB/iter at K=200 */
#endif
#define LONG_SESSION_DEFAULT_LIVE_BYTES (256 * 1024)  /* 256 KiB; catches ~1.3 KiB/iter at K=200 */

struct longSessionScenario {
	const char *name;
	int warmup_iterations;
	int measured_iterations;
	long max_rss_growth_kib;
	long max_live_growth_bytes;
	int (*step)(void *ctx);
	void *ctx;
};

/* Run one scenario. Returns 0 if growth bounds held, 1 otherwise. */
static int run_growth_scenario(const struct longSessionScenario *scenario) {
	for (int i = 0; i < scenario->warmup_iterations; i++) {
		if (scenario->step(scenario->ctx) != 0) {
			fprintf(stderr, "long_session: %s warmup step %d failed\n",
				scenario->name, i);
			return 1;
		}
	}

	long baseline_rss = current_rss_kib();
	long baseline_live = current_live_alloc_bytes();

	for (int i = 0; i < scenario->measured_iterations; i++) {
		if (scenario->step(scenario->ctx) != 0) {
			fprintf(stderr, "long_session: %s measured step %d failed\n",
				scenario->name, i);
			return 1;
		}
	}

	long final_rss = current_rss_kib();
	long final_live = current_live_alloc_bytes();

	long rss_delta = (baseline_rss >= 0 && final_rss >= 0)
		? final_rss - baseline_rss : 0;
	long live_delta = (baseline_live >= 0 && final_live >= 0)
		? final_live - baseline_live : 0;

	if (getenv("ROTIDE_LONG_SESSION_REPORT") != NULL) {
		fprintf(stderr,
			"long_session_report: scenario=%s iterations=%d "
			"rss_baseline=%ld KiB rss_final=%ld KiB rss_delta=%ld KiB "
			"live_baseline=%ld live_final=%ld live_delta=%ld\n",
			scenario->name, scenario->measured_iterations,
			baseline_rss, final_rss, rss_delta,
			baseline_live, final_live, live_delta);
	}

	int failed = 0;
	if (rss_delta > scenario->max_rss_growth_kib) {
		fprintf(stderr,
			"long_session: %s RSS grew %ld KiB over %d iterations "
			"(baseline=%ld KiB final=%ld KiB max=%ld KiB)\n",
			scenario->name, rss_delta, scenario->measured_iterations,
			baseline_rss, final_rss, scenario->max_rss_growth_kib);
		failed = 1;
	}
	if (baseline_live >= 0 && live_delta > scenario->max_live_growth_bytes) {
		fprintf(stderr,
			"long_session: %s live alloc bytes grew %ld over %d iterations "
			"(baseline=%ld final=%ld max=%ld)\n",
			scenario->name, live_delta, scenario->measured_iterations,
			baseline_live, final_live, scenario->max_live_growth_bytes);
		failed = 1;
	}
	return failed;
}

static int step_open_edit_close(void *ctx) {
	const char *path = (const char *)ctx;
	if (!editorTabOpenOrSwitchToFile(path)) {
		return 1;
	}
	editorInsertChar('x');
	editorUndo();
	if (!editorTabCloseActive()) {
		return 1;
	}
	return 0;
}

static int test_long_session_open_edit_close_cycles_have_flat_memory(void) {
	char path[] = "/tmp/rotide-longsess-XXXXXX";
	ASSERT_TRUE(write_session_fixture(path));

	struct longSessionScenario scenario = {
		.name = "open_edit_close",
		.warmup_iterations = 20,
		.measured_iterations = 200,
		.max_rss_growth_kib = LONG_SESSION_DEFAULT_RSS_KIB,
		.max_live_growth_bytes = LONG_SESSION_DEFAULT_LIVE_BYTES,
		.step = step_open_edit_close,
		.ctx = path,
	};
	int failed = run_growth_scenario(&scenario);
	(void)unlink(path);
	return failed;
}

static int step_syntax_reparse(void *ctx) {
	const char *path = (const char *)ctx;
	if (!editorTabOpenOrSwitchToFile(path)) {
		return 1;
	}
	/* Drive several edits that force incremental reparses. Stay on row 0
	 * to keep cursor math cheap; the syntax tree still re-evaluates the
	 * edited region.
	 */
	E.cy = 0;
	E.cx = 0;
	for (int i = 0; i < 5; i++) {
		editorInsertChar('a');
		editorInsertChar('b');
		editorInsertChar('c');
		editorUndo();
		editorUndo();
		editorUndo();
	}
	if (!editorTabCloseActive()) {
		return 1;
	}
	return 0;
}

static int test_long_session_syntax_reparse_cycles_have_flat_memory(void) {
	char path[64];
	ASSERT_TRUE(write_temp_c_file(path, sizeof(path), k_session_fixture));

	struct longSessionScenario scenario = {
		.name = "syntax_reparse",
		.warmup_iterations = 10,
		.measured_iterations = 100,
		/* Tree-sitter parse state (interned strings, parser stacks) takes
		 * longer than other scenarios to reach steady-state, and ASan's
		 * shadow tracks every malloc region it ever saw. live_delta
		 * (mallinfo2) is the regression signal here. */
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
		.max_rss_growth_kib = 131072,                  /* 128 MiB under sanitizers */
#else
		.max_rss_growth_kib = LONG_SESSION_DEFAULT_RSS_KIB,
#endif
		.max_live_growth_bytes = LONG_SESSION_DEFAULT_LIVE_BYTES,
		.step = step_syntax_reparse,
		.ctx = path,
	};
	int failed = run_growth_scenario(&scenario);
	(void)unlink(path);
	return failed;
}

static int step_terminal_pane(void *ctx) {
	(void)ctx;
	struct editorTerminalPane *t = editorTerminalPaneCreate("true", 40, 8);
	if (t == NULL) {
		return 1;
	}
	/* Pump until child exits or a small timeout (`true` exits immediately
	 * but the pump still has to ingest the SIGCHLD + drain the PTY). */
	int waited_ms = 0;
	while (waited_ms < 500 && !t->exited) {
		(void)editorTerminalPanePump(t);
		struct timespec ts = {0, 2 * 1000 * 1000};
		nanosleep(&ts, NULL);
		waited_ms += 2;
	}
	editorTerminalPaneFree(t);
	return 0;
}

static int test_long_session_terminal_pane_cycles_have_flat_memory(void) {
	struct longSessionScenario scenario = {
		.name = "terminal_pane",
		.warmup_iterations = 5,
		/* Each iter forks+execs a process, so keep K small. 40 iterations
		 * is enough to surface a per-pane retention of ~10 KiB within
		 * the default live-bytes slop. */
		.measured_iterations = 40,
		.max_rss_growth_kib = LONG_SESSION_DEFAULT_RSS_KIB,
		.max_live_growth_bytes = LONG_SESSION_DEFAULT_LIVE_BYTES,
		.step = step_terminal_pane,
		.ctx = NULL,
	};
	return run_growth_scenario(&scenario);
}

static int step_lsp_open_close(void *ctx) {
	const char *path = (const char *)ctx;
	if (!editorTabOpenOrSwitchToFile(path)) {
		return 1;
	}
	if (!editorTabCloseActive()) {
		return 1;
	}
	return 0;
}

static int test_long_session_lsp_open_close_cycles_have_flat_memory(void) {
	editorLspTestSetMockEnabled(1);
	E.lsp_clangd_enabled = 1;

	char path[64];
	ASSERT_TRUE(write_temp_c_file(path, sizeof(path), k_session_fixture));

	struct longSessionScenario scenario = {
		.name = "lsp_open_close",
		.warmup_iterations = 10,
		.measured_iterations = 100,
		.max_rss_growth_kib = LONG_SESSION_DEFAULT_RSS_KIB,
		.max_live_growth_bytes = LONG_SESSION_DEFAULT_LIVE_BYTES,
		.step = step_lsp_open_close,
		.ctx = path,
	};
	int failed = run_growth_scenario(&scenario);
	(void)unlink(path);
	return failed;
}

const struct editorTestCase g_long_session_tests[] = {
	{"long_session_open_edit_close_cycles_have_flat_memory",
		test_long_session_open_edit_close_cycles_have_flat_memory},
	{"long_session_syntax_reparse_cycles_have_flat_memory",
		test_long_session_syntax_reparse_cycles_have_flat_memory},
	{"long_session_terminal_pane_cycles_have_flat_memory",
		test_long_session_terminal_pane_cycles_have_flat_memory},
	{"long_session_lsp_open_close_cycles_have_flat_memory",
		test_long_session_lsp_open_close_cycles_have_flat_memory},
};

const int g_long_session_test_count =
	(int)(sizeof(g_long_session_tests) / sizeof(g_long_session_tests[0]));
