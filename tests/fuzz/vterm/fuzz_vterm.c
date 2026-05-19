/* libFuzzer harness for the vendored libvterm parser.
 *
 * Fuzz strategy: feed `(data, size)` straight into a fresh VTerm via
 * vterm_input_write, flush damage, then read every cell back. ASan/UBSan
 * surfaces buffer overruns, integer overflows, and use-after-frees in
 * the parser/state/screen pipeline.
 *
 * The harness does not exercise the editorTerminalPane PTY plumbing —
 * spawning a child process per fuzz iteration would cap throughput at
 * tens-of-iters-per-second and the pane is a thin wrapper around this
 * same code path. Pane-level invariants (scrollback cap, focus
 * tracking) are validated by the existing test_terminal_pane suite.
 *
 * Build: see the `fuzz-vterm` Makefile target. clang + libFuzzer
 * required.
 */

#include "vterm.h"

#include <stddef.h>
#include <stdint.h>

#define FUZZ_VTERM_ROWS 24
#define FUZZ_VTERM_COLS 80

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	VTerm *vt = vterm_new(FUZZ_VTERM_ROWS, FUZZ_VTERM_COLS);
	if (vt == NULL) {
		return 0;
	}
	vterm_set_utf8(vt, 1);

	VTermScreen *screen = vterm_obtain_screen(vt);
	if (screen != NULL) {
		vterm_screen_reset(screen, 1);
	}

	if (size > 0) {
		vterm_input_write(vt, (const char *)data, size);
	}

	if (screen != NULL) {
		vterm_screen_flush_damage(screen);
		/* Read every cell so an out-of-bounds write inside the parser
		 * shows up as an ASan read-error here instead of being silently
		 * absorbed by libvterm's internal grid storage. */
		for (int r = 0; r < FUZZ_VTERM_ROWS; r++) {
			for (int c = 0; c < FUZZ_VTERM_COLS; c++) {
				VTermPos pos = {.row = r, .col = c};
				VTermScreenCell cell = {0};
				(void)vterm_screen_get_cell(screen, pos, &cell);
			}
		}
	}

	vterm_free(vt);
	return 0;
}
