#ifndef ROTIDE_TEXT_UTF8_H
#define ROTIDE_TEXT_UTF8_H

#include <stddef.h>

int editorIsUtf8ContinuationByte(unsigned char c);
int editorUtf8DecodeCodepoint(const char *s, int len, unsigned int *cp);
/* Encodes a Unicode code point into 1-4 UTF-8 bytes written to `out`.
 * Returns the byte count, or 0 for code points outside [0, 0x110000). */
int editorUtf8EncodeCodepoint(unsigned int cp, char *out);
int editorIsGraphemeExtendCodepoint(unsigned int cp);
int editorIsRegionalIndicatorCodepoint(unsigned int cp);
int editorCharDisplayWidth(const char *s, int len);

#endif
