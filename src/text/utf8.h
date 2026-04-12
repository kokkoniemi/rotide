#ifndef TEXT_UTF8_H
#define TEXT_UTF8_H

#include <stddef.h>

int editorIsUtf8ContinuationByte(unsigned char c);
int editorUtf8DecodeCodepoint(const char *s, int len, unsigned int *cp);
int editorIsGraphemeExtendCodepoint(unsigned int cp);
int editorIsRegionalIndicatorCodepoint(unsigned int cp);
int editorCharDisplayWidth(const char *s, int len);

#endif
