#ifndef SIZE_UTILS_H
#define SIZE_UTILS_H

#include <limits.h>
#include <stddef.h>
#include <sys/types.h>

static inline int editorSizeAdd(size_t left, size_t right, size_t *out) {
	if (left > ((size_t)-1) - right) {
		return 0;
	}

	*out = left + right;
	return 1;
}

static inline int editorSizeSub(size_t left, size_t right, size_t *out) {
	if (left < right) {
		return 0;
	}

	*out = left - right;
	return 1;
}

static inline int editorSizeMul(size_t left, size_t right, size_t *out) {
	if (left != 0 && right > ((size_t)-1) / left) {
		return 0;
	}

	*out = left * right;
	return 1;
}

static inline int editorIntToSize(int value, size_t *out) {
	if (value < 0) {
		return 0;
	}

	*out = (size_t)value;
	return 1;
}

static inline int editorSsizeToSize(ssize_t value, size_t *out) {
	if (value < 0) {
		return 0;
	}

	*out = (size_t)value;
	return 1;
}

static inline int editorSizeToInt(size_t value, int *out) {
	if (value > (size_t)INT_MAX) {
		return 0;
	}

	*out = (int)value;
	return 1;
}

static inline int editorSizeWithinInt(size_t value) {
	return value <= (size_t)INT_MAX;
}

#endif
