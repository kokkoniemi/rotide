/* libFuzzer harness for the theme TOML parser
 * (`src/config/theme_parse.c`).
 *
 * The theme parser is a hand-rolled line-based TOML subset: tables,
 * `key = "value"`, `#` comments, hex colors, ANSI color names. Real
 * input comes from `~/.rotide/themes/*.toml` plus a tiny `[theme]`
 * stanza in the main config. Untrusted in the sense that a typo or a
 * shared theme file from the internet shouldn't crash the editor.
 *
 * Fuzz strategy: wrap `(data, size)` in an fmemopen stream and feed
 * it through `editorThemeApplyStreamFuzz`, which calls the same
 * `editorThemeApplyStream` that production uses. ASan/UBSan surface
 * any out-of-bounds writes inside `editorThemeParseEntry`,
 * `editorConfigParseQuotedValue`, the hex-color decoder, or the
 * table-name dispatch.
 *
 * Build: see the `fuzz-toml-theme` Makefile target. clang + libFuzzer
 * required.
 */

#define _GNU_SOURCE

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

extern void editorThemeApplyStreamFuzz(FILE *fp);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	if (size == 0) {
		return 0;
	}

	/* fmemopen with mode "r" gives us a read-only stream over the
	 * input buffer. The parser uses fgets/fgetc/ferror, all of which
	 * fmemopen supports. */
	FILE *fp = fmemopen((void *)data, size, "r");
	if (fp == NULL) {
		return 0;
	}

	editorThemeApplyStreamFuzz(fp);
	fclose(fp);
	return 0;
}
