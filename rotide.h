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
#define ROTIDE_UNDO_HISTORY_LIMIT 200
#define ROTIDE_OSC52_MAX_COPY_BYTES 100000

typedef void (*editorClipboardExternalSink)(const char *text, int len);

enum editorMouseEventKind {
	EDITOR_MOUSE_EVENT_NONE = 0,
	EDITOR_MOUSE_EVENT_LEFT_PRESS,
	EDITOR_MOUSE_EVENT_LEFT_DRAG,
	EDITOR_MOUSE_EVENT_LEFT_RELEASE,
	EDITOR_MOUSE_EVENT_WHEEL_UP,
	EDITOR_MOUSE_EVENT_WHEEL_DOWN
};

struct editorMouseEvent {
	enum editorMouseEventKind kind;
	int x;
	int y;
};

struct erow {
	int size;
	int rsize;
	char *chars;
	char *render;
};

struct editorSelectionRange {
	int start_cy;
	int start_cx;
	int end_cy;
	int end_cx;
};

enum editorEditKind {
	EDITOR_EDIT_NONE = 0,
	EDITOR_EDIT_INSERT_TEXT,
	EDITOR_EDIT_DELETE_TEXT,
	EDITOR_EDIT_NEWLINE
};

enum editorEditPendingMode {
	EDITOR_EDIT_PENDING_NONE = 0,
	EDITOR_EDIT_PENDING_CAPTURED,
	EDITOR_EDIT_PENDING_GROUPED,
	EDITOR_EDIT_PENDING_SKIPPED
};

struct editorSnapshot {
	char *text;
	int textlen;
	int cx;
	int cy;
	int dirty;
};

struct editorHistory {
	struct editorSnapshot entries[ROTIDE_UNDO_HISTORY_LIMIT];
	int start;
	int len;
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
	int selection_mode_active;
	int selection_anchor_cx;
	int selection_anchor_cy;
	int mouse_left_button_down;
	int mouse_drag_anchor_cx;
	int mouse_drag_anchor_cy;
	int mouse_drag_started;
	char *clipboard_text;
	int clipboard_textlen;
	editorClipboardExternalSink clipboard_external_sink;
	struct editorHistory undo_history;
	struct editorHistory redo_history;
	struct editorSnapshot edit_pending_snapshot;
	enum editorEditKind edit_group_kind;
	enum editorEditKind edit_pending_kind;
	enum editorEditPendingMode edit_pending_mode;
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
	DEL_KEY,
	MOUSE_EVENT,
	RESIZE_EVENT
};

#endif
