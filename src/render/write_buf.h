#ifndef ROTIDE_RENDER_WRITE_BUF_H
#define ROTIDE_RENDER_WRITE_BUF_H

#include <stddef.h>

struct writeBuf {
	char *b;
	size_t len;
	size_t cap;
};

#define WRITEBUF_INIT {NULL, 0, 0}

int wbAppend(struct writeBuf *wb, const char *s, size_t len);
void wbFree(struct writeBuf *wb);
int wbWriteAllToStdout(const struct writeBuf *wb);

#endif
