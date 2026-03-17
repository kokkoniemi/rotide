#ifndef ROTIDE_H
#define ROTIDE_H

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <termios.h>
#include <time.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define ROTIDE_VERSION "0.0.1"
#define ROTIDE_TAB_WIDTH 8

struct erow {
	int size;
	int rsize;
	char *chars;
	char *render;
};

struct editorConfig {
	int window_rows;
	int window_cols;
	int cx;
	int cy;
	int rx;
	int rowoff;
	int coloff;
	int numrows;
	struct erow *rows;
	int dirty;
	char *filename;
	char statusmsg[80];
	time_t statusmsg_time;
	char *search_query;
	int search_match_row;
	int search_match_start;
	int search_match_len;
	int search_direction;
	int search_saved_cx;
	int search_saved_cy;
	struct termios orig_attrs;
};

extern struct editorConfig E;

enum editorKey {
	BACKSPACE = 127,
	ARROW_LEFT = 90000,
	ARROW_DOWN,
	ARROW_UP,
	ARROW_RIGHT,
	PAGE_UP,
	PAGE_DOWN,
	HOME_KEY,
	END_KEY,
	DEL_KEY
};

#endif
