#include "rotide.h"

#include <locale.h>
#include <stdlib.h>

#include "buffer.h"
#include "input.h"
#include "keymap.h"
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
	E.search_query = NULL;
	E.search_match_row = -1;
	E.search_match_start = 0;
	E.search_match_len = 0;
	E.search_direction = 1;
	E.search_saved_cx = 0;
	E.search_saved_cy = 0;
	E.selection_mode_active = 0;
	E.selection_anchor_cx = 0;
	E.selection_anchor_cy = 0;
	E.mouse_left_button_down = 0;
	E.mouse_drag_anchor_cx = 0;
	E.mouse_drag_anchor_cy = 0;
	E.mouse_drag_started = 0;
	E.clipboard_text = NULL;
	E.clipboard_textlen = 0;
	E.clipboard_external_sink = NULL;
	E.undo_history.start = 0;
	E.undo_history.len = 0;
	E.redo_history.start = 0;
	E.redo_history.len = 0;
	E.edit_pending_snapshot.text = NULL;
	E.edit_pending_snapshot.textlen = 0;
	E.edit_pending_snapshot.cx = 0;
	E.edit_pending_snapshot.cy = 0;
	E.edit_pending_snapshot.dirty = 0;
	E.edit_group_kind = EDITOR_EDIT_NONE;
	E.edit_pending_kind = EDITOR_EDIT_NONE;
	E.edit_pending_mode = EDITOR_EDIT_PENDING_NONE;
	editorKeymapInitDefaults(&E.keymap);
	editorClipboardSetExternalSink(editorClipboardSyncOsc52);

	if (!editorRefreshWindowSize()) {
		panic("readWindowSize");
	}
}

int main(int argc, char *argv[]) {
	setlocale(LC_CTYPE, "");
	setRawMode();
	initEditor();

	enum editorKeymapLoadStatus keymap_status = editorKeymapLoadConfigured(&E.keymap);
	if (argc >= 2) {
		editorOpen(argv[1]);
	}

	if (keymap_status == EDITOR_KEYMAP_LOAD_INVALID_PROJECT) {
		editorSetStatusMsg("Invalid keymap config, using defaults");
	} else if (keymap_status == EDITOR_KEYMAP_LOAD_INVALID_GLOBAL) {
		editorSetStatusMsg("Invalid global keymap config, ignoring ~/.rotide/config.toml");
	} else if (keymap_status == EDITOR_KEYMAP_LOAD_OUT_OF_MEMORY) {
		editorSetStatusMsg("Out of memory");
	} else {
		char help_msg[160];
		editorKeymapBuildHelpStatus(&E.keymap, help_msg, sizeof(help_msg));
		editorSetStatusMsg("%s", help_msg);
	}

	while (1) {
		editorRefreshScreen();
		editorProcessKeypress();
	}

	return EXIT_SUCCESS;
}
