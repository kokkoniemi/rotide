#ifndef RENDER_WRITE_BUF_H
#define RENDER_WRITE_BUF_H

#include <stddef.h>

struct writeBuf {
	char *b;
	size_t len;
};

#define WRITEBUF_INIT                                                                              \
	{ NULL, 0 }

int wbAppend(struct writeBuf *wb, const char *s, size_t len);
void wbFree(struct writeBuf *wb);
int wbWriteAllToStdout(const struct writeBuf *wb);

#endif
