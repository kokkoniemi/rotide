/* libFuzzer harness for the LSP framing parser
 * (`src/language/lsp_framing.c`).
 *
 * Fuzz strategy: pipe `(data, size)` into a memfd, rewind it, then drain
 * frames via `editorLspReadFrame` until the parser refuses. ASan/UBSan
 * surface buffer overruns in header scanning, integer overflow in
 * Content-Length parsing, malloc-allocations driven by attacker-controlled
 * lengths, and bad cleanup after partial frames.
 *
 * memfd_create + lseek lets us feed a single contiguous byte stream
 * without worrying about pipe(7) capacity limits (some seeds want to
 * exercise frames larger than the default 64 KiB pipe buffer). poll(2)
 * on a regular fd always returns ready immediately, so the parser's
 * blocking read loop terminates cleanly at EOF with errno=EPIPE.
 *
 * Build: see the `fuzz-lsp` Makefile target. clang + libFuzzer required.
 */

#include "language/lsp_framing.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

/* Cap the number of frames per input so a malformed stream that decodes
 * into ten million empty-payload frames can't dominate fuzz throughput. */
#define FUZZ_LSP_MAX_FRAMES 64

/* Short per-call timeout. poll() on a memfd returns POLLIN immediately
 * regardless, but we still want the parser to give up promptly if it
 * ever reaches a path that actually waits. */
#define FUZZ_LSP_TIMEOUT_MS 50

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	int fd = memfd_create("lsp_fuzz", 0);
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

	for (int i = 0; i < FUZZ_LSP_MAX_FRAMES; i++) {
		char *frame = editorLspReadFrame(fd, FUZZ_LSP_TIMEOUT_MS);
		if (frame == NULL) {
			break;
		}
		/* Touch the payload so ASan flags a write past the declared
		 * length. The parser is supposed to NUL-terminate at exactly
		 * payload_len. We don't know payload_len here, but a missing
		 * terminator would have been caught by the read loop already. */
		volatile char sink = frame[0];
		(void)sink;
		free(frame);
	}

	close(fd);
	return 0;
}
