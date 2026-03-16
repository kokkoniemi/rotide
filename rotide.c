#include "rotide.h"

#include <locale.h>
#include <stdlib.h>

#include "buffer.h"
#include "input.h"
#include "output.h"
#include "terminal.h"

struct editorConfig E;

void initEditor(void) {
	E.cx = 0;
	E.cy = 0;
	E.rx = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.numrows = 0;
	E.rows = NULL;
	E.dirty = 0;
	E.filename = NULL;
	E.statusmsg[0] = '\0';
	E.statusmsg_time = 0;

	if (readWindowSize(&E.window_rows, &E.window_cols) == -1) {
		panic("readWindowSize");
	}
	E.window_rows -= 2;
}

int main(int argc, char *argv[]) {
	setlocale(LC_CTYPE, "");
	setRawMode();
	initEditor();
	if (argc >= 2) {
		editorOpen(argv[1]);
	}

	editorSetStatusMsg("Help: Ctrl-S = save; Ctrl-Q = quit");

	while (1) {
		editorRefreshScreen();
		editorProcessKeypress();
	}

	return EXIT_SUCCESS;
}
