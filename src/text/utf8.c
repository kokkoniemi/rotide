#include "rotide.h"
#include "text/utf8.h"

#include <limits.h>
#include <wchar.h>

int editorIsUtf8ContinuationByte(unsigned char c) {
	return (c & 0xC0) == 0x80;
}

int editorUtf8DecodeCodepoint(const char *s, int len, unsigned int *cp) {
	if (len <= 0) {
		*cp = 0;
		return 0;
	}

	unsigned char b0 = (unsigned char)s[0];
	if (b0 < 0x80) {
		*cp = b0;
		return 1;
	}

	int expected_len = 0;
	unsigned int codepoint = 0;
	unsigned int min_codepoint = 0;

	// Determine UTF-8 sequence length and initial payload bits from leading byte.
	if ((b0 & 0xE0) == 0xC0) {
		expected_len = 2;
		codepoint = b0 & 0x1F;
		min_codepoint = 0x80;
	} else if ((b0 & 0xF0) == 0xE0) {
		expected_len = 3;
		codepoint = b0 & 0x0F;
		min_codepoint = 0x800;
	} else if ((b0 & 0xF8) == 0xF0) {
		expected_len = 4;
		codepoint = b0 & 0x07;
		min_codepoint = 0x10000;
	} else {
		*cp = b0;
		return 1;
	}

	// If sequence is truncated or malformed, treat first byte as standalone.
	if (len < expected_len) {
		*cp = b0;
		return 1;
	}

	for (int i = 1; i < expected_len; i++) {
		unsigned char bx = (unsigned char)s[i];
		if (!editorIsUtf8ContinuationByte(bx)) {
			*cp = b0;
			return 1;
		}
		codepoint = (codepoint << 6) | (unsigned int)(bx & 0x3F);
	}

	// Reject overlong forms, surrogate range, and out-of-range codepoints.
	if (codepoint < min_codepoint || codepoint > 0x10FFFF ||
			(codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
		*cp = b0;
		return 1;
	}

	*cp = codepoint;
	return expected_len;
}

int editorIsRegionalIndicatorCodepoint(unsigned int cp) {
	return cp >= 0x1F1E6 && cp <= 0x1F1FF;
}

int editorIsGraphemeExtendCodepoint(unsigned int cp) {
	// Unicode combining mark blocks that should stay in the same grapheme.
	if ((cp >= 0x0300 && cp <= 0x036F) ||
			(cp >= 0x1AB0 && cp <= 0x1AFF) ||
			(cp >= 0x1DC0 && cp <= 0x1DFF) ||
			(cp >= 0x20D0 && cp <= 0x20FF) ||
			(cp >= 0xFE20 && cp <= 0xFE2F)) {
		return 1;
	}
	// Variation selectors modify the previous glyph and should not split clusters.
	if ((cp >= 0xFE00 && cp <= 0xFE0F) ||
			(cp >= 0xE0100 && cp <= 0xE01EF)) {
		return 1;
	}
	// Emoji skin-tone modifiers are attached to the previous emoji.
	if (cp >= 0x1F3FB && cp <= 0x1F3FF) {
		return 1;
	}
	// Keep ZWNJ with the current cluster for cursor stepping consistency.
	if (cp == 0x200C) {
		return 1;
	}
	if (cp > (unsigned int)WCHAR_MAX) {
		return 0;
	}
	// Fallback: many libc locales report combining marks with width 0.
	wchar_t wc = (wchar_t)cp;
	int width = wcwidth(wc);
	return width == 0 && cp != 0x200D;
}

int editorCharDisplayWidth(const char *s, int len) {
	unsigned char c = (unsigned char)s[0];
	if (c < 0x80) {
		return 1;
	}
	// Continuation bytes are part of a previous codepoint and should not
	// advance visual columns when scanning byte-by-byte.
	if (editorIsUtf8ContinuationByte(c)) {
		return 0;
	}

	mbstate_t ps = {0};
	wchar_t wc;
	size_t read = mbrtowc(&wc, s, len, &ps);
	if (read == (size_t)-1 || read == (size_t)-2) {
		return 1;
	}
	int width = wcwidth(wc);
	if (width < 0) {
		return 1;
	}
	return width;
}
