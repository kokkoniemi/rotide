// TODO(stability): Make filename prompt UTF-8-byte friendly so multibyte filenames can be entered consistently.
// TODO(stability): In screen refresh, always emit the full cursor-position escape sequence; never truncate it based on window width.
// TODO(stability): In readCursorPosition(), initialize buffers and strictly validate ESC[row;colR before parsing.
// TODO(stability): In readCursorPosition(), fail deterministically on short/malformed terminal responses without reading uninitialized data.
// TODO(stability): Add allocation-failure guards in prompt buffer growth and row/render allocation paths; preserve state and show a status error.
// TODO(stability): After atomic save rename, fsync the parent directory to improve crash durability guarantees.
// TODO(stability): Keep temp-file cleanup exhaustive on all save failure paths (no leftover .rotide-tmp-* files).
// TODO(stability): Normalize save failure status messages to surface actionable errno context (permission, missing path, read-only FS).
// TODO(maintainability): Apply const-correctness to internal row/string helper APIs where source buffers are read-only.
// TODO(quality): Add CI sanitizer job (ASan + UBSan) in addition to current build/test job.
// TODO(quality): Add README regression checklist for Unicode editing, scrolling, and save/quit smoke scenarios.
// TODO(feature): Add Ctrl-F incremental search with live match navigation and highlight.
// TODO(feature): Add search next/previous keybindings (Ctrl-N / Ctrl-P) after initial search.
// TODO(feature): Add Ctrl-G go-to-line prompt and cursor jump.
// TODO(feature): Add undo/redo stack support (Ctrl-Z / Ctrl-Y).
// TODO(feature): Add selection mode (anchor + cursor) with cut/copy/delete operations.
// TODO(feature): Add clipboard support (internal buffer first, then OSC52 terminal clipboard).
// TODO(feature): Add mouse click support for cursor placement and wheel scrolling.
// TODO(feature): Add mouse drag support for text selection.


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
