/* libFuzzer harness for the DAP framing parser
 * (`src/debug/dap_client.c`).
 *
 * The DAP transport uses the same `Content-Length:`-framed wire format
 * as LSP, so the fuzz strategy mirrors `tests/fuzz/lsp/fuzz_lsp.c`:
 * pipe `(data, size)` into a memfd, rewind, and drain frames via
 * `editorDapClientReadFrame` until the parser refuses. ASan/UBSan
 * surface header overruns, digit-overflow in Content-Length, and any
 * malloc that an attacker-controlled length could provoke.
 *
 * The DAP and LSP parsers are duplicated production code today rather
 * than a shared module. Both harnesses are kept so each path gets its
 * own corpus and edge coverage — duplicate parsers tend to drift.
 *
 * Build: see the `fuzz-dap` Makefile target. clang + libFuzzer required.
 */

#include "debug/dap_client.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#define FUZZ_DAP_MAX_FRAMES 64

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	int fd = memfd_create("dap_fuzz", 0);
	if (fd < 0) {
		return 0;
	}

	if (size > 0) {
		const uint8_t *cursor = data;
		size_t remaining = size;
		while (remaining > 0) {
			ssize_t written = write(fd, cursor, remaining);
			if (written <= 0) {
				close(fd);
				return 0;
			}
			cursor += (size_t)written;
			remaining -= (size_t)written;
		}
	}

	if (lseek(fd, 0, SEEK_SET) != 0) {
		close(fd);
		return 0;
	}

	for (int i = 0; i < FUZZ_DAP_MAX_FRAMES; i++) {
		char *frame = editorDapClientReadFrame(fd);
		if (frame == NULL) {
			break;
		}
		volatile char sink = frame[0];
		(void)sink;
		free(frame);
	}

	close(fd);
	return 0;
}
