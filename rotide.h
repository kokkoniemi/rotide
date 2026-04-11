#ifndef ROTIDE_H
#define ROTIDE_H

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <limits.h>
#include <stddef.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define ROTIDE_VERSION "0.0.1"
#define ROTIDE_TAB_WIDTH 8
#define ROTIDE_UNDO_HISTORY_LIMIT 200
#define ROTIDE_OSC52_MAX_COPY_BYTES ((size_t)100000)
#define ROTIDE_MAX_TEXT_BYTES ((size_t)INT_MAX)
#define ROTIDE_KEYMAP_MAX_BINDINGS 64
#define ROTIDE_MAX_TABS 128
#define ROTIDE_TAB_TITLE_MAX_COLS 25
#define ROTIDE_TAB_TRUNC_MARKER "..."
#define ROTIDE_DRAWER_DEFAULT_WIDTH 24
#define ROTIDE_MAX_SYNTAX_SPANS_PER_ROW 256
#define ROTIDE_ALT_LETTER_KEY_BASE 91000
#define ROTIDE_CTRL_ALT_LETTER_KEY_BASE 91026
#define ROTIDE_TASK_LOG_MAX_BYTES ((size_t)131072)

#define EDITOR_ALT_LETTER_KEY(ch) (ROTIDE_ALT_LETTER_KEY_BASE + ((int)(ch) - (int)'a'))
#define EDITOR_CTRL_ALT_LETTER_KEY(ch) \
	(ROTIDE_CTRL_ALT_LETTER_KEY_BASE + ((int)(ch) - (int)'a'))

#define EDITOR_IS_ALT_LETTER_KEY(key) \
	((key) >= ROTIDE_ALT_LETTER_KEY_BASE && (key) < ROTIDE_ALT_LETTER_KEY_BASE + 26)
#define EDITOR_IS_CTRL_ALT_LETTER_KEY(key) \
	((key) >= ROTIDE_CTRL_ALT_LETTER_KEY_BASE && \
			(key) < ROTIDE_CTRL_ALT_LETTER_KEY_BASE + 26)

#define EDITOR_ALT_LETTER_FROM_KEY(key) \
	((char)('a' + ((int)(key) - ROTIDE_ALT_LETTER_KEY_BASE)))
#define EDITOR_CTRL_ALT_LETTER_FROM_KEY(key) \
	((char)('a' + ((int)(key) - ROTIDE_CTRL_ALT_LETTER_KEY_BASE)))

typedef void (*editorClipboardExternalSink)(const char *text, size_t len);

enum editorMouseEventKind {
	EDITOR_MOUSE_EVENT_NONE = 0,
	EDITOR_MOUSE_EVENT_LEFT_PRESS,
	EDITOR_MOUSE_EVENT_LEFT_DRAG,
	EDITOR_MOUSE_EVENT_LEFT_RELEASE,
	EDITOR_MOUSE_EVENT_WHEEL_UP,
	EDITOR_MOUSE_EVENT_WHEEL_DOWN,
	EDITOR_MOUSE_EVENT_WHEEL_LEFT,
	EDITOR_MOUSE_EVENT_WHEEL_RIGHT
};

struct editorMouseEvent {
	enum editorMouseEventKind kind;
	int x;
	int y;
};

struct erow {
	int size;
	int rsize;
	int render_display_cols;
	char *chars;
	char *render;
};

struct editorSelectionRange {
	int start_cy;
	int start_cx;
	int end_cy;
	int end_cx;
};

struct editorDrawerNode;
struct editorSyntaxState;

enum editorPaneFocus {
	EDITOR_PANE_TEXT = 0,
	EDITOR_PANE_DRAWER
};

enum editorCursorStyle {
	EDITOR_CURSOR_STYLE_BLOCK = 0,
	EDITOR_CURSOR_STYLE_BAR,
	EDITOR_CURSOR_STYLE_UNDERLINE
};

enum editorSyntaxLanguage {
	EDITOR_SYNTAX_NONE = 0,
	EDITOR_SYNTAX_C,
	EDITOR_SYNTAX_GO,
	EDITOR_SYNTAX_SHELL,
	EDITOR_SYNTAX_HTML,
	EDITOR_SYNTAX_JAVASCRIPT,
	EDITOR_SYNTAX_CSS
};

enum editorSyntaxHighlightClass {
	EDITOR_SYNTAX_HL_NONE = 0,
	EDITOR_SYNTAX_HL_COMMENT,
	EDITOR_SYNTAX_HL_KEYWORD,
	EDITOR_SYNTAX_HL_TYPE,
	EDITOR_SYNTAX_HL_FUNCTION,
	EDITOR_SYNTAX_HL_STRING,
	EDITOR_SYNTAX_HL_NUMBER,
	EDITOR_SYNTAX_HL_CONSTANT,
	EDITOR_SYNTAX_HL_PREPROCESSOR,
	EDITOR_SYNTAX_HL_OPERATOR,
	EDITOR_SYNTAX_HL_PUNCTUATION,
	EDITOR_SYNTAX_HL_CLASS_COUNT
};

enum editorThemeColor {
	EDITOR_THEME_COLOR_DEFAULT = 0,
	EDITOR_THEME_COLOR_BLACK,
	EDITOR_THEME_COLOR_RED,
	EDITOR_THEME_COLOR_GREEN,
	EDITOR_THEME_COLOR_YELLOW,
	EDITOR_THEME_COLOR_BLUE,
	EDITOR_THEME_COLOR_MAGENTA,
	EDITOR_THEME_COLOR_CYAN,
	EDITOR_THEME_COLOR_WHITE,
	EDITOR_THEME_COLOR_BRIGHT_BLACK,
	EDITOR_THEME_COLOR_BRIGHT_RED,
	EDITOR_THEME_COLOR_BRIGHT_GREEN,
	EDITOR_THEME_COLOR_BRIGHT_YELLOW,
	EDITOR_THEME_COLOR_BRIGHT_BLUE,
	EDITOR_THEME_COLOR_BRIGHT_MAGENTA,
	EDITOR_THEME_COLOR_BRIGHT_CYAN,
	EDITOR_THEME_COLOR_BRIGHT_WHITE,
	EDITOR_THEME_COLOR_COUNT
};

enum editorViewportMode {
	EDITOR_VIEWPORT_FOLLOW_CURSOR = 0,
	EDITOR_VIEWPORT_FREE_SCROLL
};

enum editorTabKind {
	EDITOR_TAB_FILE = 0,
	EDITOR_TAB_TASK_LOG
};

struct editorRowSyntaxSpan {
	int start_render_idx;
	int end_render_idx;
	enum editorSyntaxHighlightClass highlight_class;
};

struct editorDrawerEntryView {
	const char *name;
	const char *path;
	int depth;
	int is_dir;
	int is_expanded;
	int is_selected;
	int has_scan_error;
	int is_root;
	int parent_visible_idx;
	int is_last_sibling;
	int is_active_file;
};

struct editorTabLayoutEntry {
	int tab_idx;
	int start_col;
	int width_cols;
	int show_left_overflow;
	int show_right_overflow;
};

enum editorAction {
	EDITOR_ACTION_QUIT = 0,
	EDITOR_ACTION_SAVE,
	EDITOR_ACTION_NEW_TAB,
	EDITOR_ACTION_CLOSE_TAB,
	EDITOR_ACTION_NEXT_TAB,
	EDITOR_ACTION_PREV_TAB,
	EDITOR_ACTION_FOCUS_DRAWER,
	EDITOR_ACTION_RESIZE_DRAWER_NARROW,
	EDITOR_ACTION_RESIZE_DRAWER_WIDEN,
	EDITOR_ACTION_FIND,
	EDITOR_ACTION_GOTO_LINE,
	EDITOR_ACTION_GOTO_DEFINITION,
	EDITOR_ACTION_TOGGLE_SELECTION,
	EDITOR_ACTION_COPY_SELECTION,
	EDITOR_ACTION_CUT_SELECTION,
	EDITOR_ACTION_DELETE_SELECTION,
	EDITOR_ACTION_PASTE,
	EDITOR_ACTION_UNDO,
	EDITOR_ACTION_REDO,
	EDITOR_ACTION_MOVE_HOME,
	EDITOR_ACTION_MOVE_END,
	EDITOR_ACTION_PAGE_UP,
	EDITOR_ACTION_PAGE_DOWN,
	EDITOR_ACTION_SCROLL_LEFT,
	EDITOR_ACTION_SCROLL_RIGHT,
	EDITOR_ACTION_MOVE_UP,
	EDITOR_ACTION_MOVE_DOWN,
	EDITOR_ACTION_MOVE_LEFT,
	EDITOR_ACTION_MOVE_RIGHT,
	EDITOR_ACTION_NEWLINE,
	EDITOR_ACTION_ESCAPE,
	EDITOR_ACTION_REDRAW,
	EDITOR_ACTION_DELETE_CHAR,
	EDITOR_ACTION_BACKSPACE,
	EDITOR_ACTION_COUNT
};

struct editorKeyBinding {
	int key;
	enum editorAction action;
};

struct editorKeymap {
	struct editorKeyBinding bindings[ROTIDE_KEYMAP_MAX_BINDINGS];
	size_t len;
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
	size_t textlen;
	int cx;
	int cy;
	int dirty;
};

struct editorHistory {
	struct editorSnapshot entries[ROTIDE_UNDO_HISTORY_LIMIT];
	int start;
	int len;
};

struct editorTabState {
	enum editorTabKind tab_kind;
	char *tab_title;
	char *generated_text;
	size_t generated_text_len;
	int cx;
	int cy;
	int rx;
	int rowoff;
	int coloff;
	int numrows;
	struct erow *rows;
	size_t *row_start_bytes;
	int row_start_bytes_count;
	int row_start_bytes_valid;
	int max_render_cols;
	int max_render_cols_valid;
	int dirty;
	char *filename;
	enum editorSyntaxLanguage syntax_language;
	struct editorSyntaxState *syntax_state;
	int lsp_doc_open;
	int lsp_doc_version;
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
	struct editorHistory undo_history;
	struct editorHistory redo_history;
	struct editorSnapshot edit_pending_snapshot;
	enum editorEditKind edit_group_kind;
	enum editorEditKind edit_pending_kind;
	enum editorEditPendingMode edit_pending_mode;
};

struct editorConfig {
	int window_rows;
	int window_cols;
	enum editorTabKind tab_kind;
	char *tab_title;
	char *generated_text;
	size_t generated_text_len;
	int cx;
	int cy;
	int rx;
	int rowoff;
	int coloff;
	int numrows;
	struct erow *rows;
	size_t *row_start_bytes;
	int row_start_bytes_count;
	int row_start_bytes_valid;
	int max_render_cols;
	int max_render_cols_valid;
	int dirty;
	char *filename;
	enum editorSyntaxLanguage syntax_language;
	struct editorSyntaxState *syntax_state;
	int lsp_enabled;
	char lsp_gopls_command[PATH_MAX];
	char lsp_gopls_install_command[PATH_MAX];
	int lsp_doc_open;
	int lsp_doc_version;
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
	size_t clipboard_textlen;
	editorClipboardExternalSink clipboard_external_sink;
	struct editorHistory undo_history;
	struct editorHistory redo_history;
	struct editorSnapshot edit_pending_snapshot;
	enum editorEditKind edit_group_kind;
	enum editorEditKind edit_pending_kind;
	enum editorEditPendingMode edit_pending_mode;
	struct editorTabState *tabs;
	int tab_count;
	int tab_capacity;
	int active_tab;
	int tab_view_start;
	int close_confirmed;
	pid_t task_pid;
	int task_output_fd;
	int task_running;
	int task_tab_idx;
	int task_output_truncated;
	size_t task_output_bytes;
	int task_exit_code;
	char task_success_status[80];
	char task_failure_status[80];
	char *recovery_path;
	time_t recovery_last_autosave_time;
	char *drawer_root_path;
	struct editorDrawerNode *drawer_root;
	int drawer_selected_index;
	int drawer_rowoff;
	int drawer_last_click_visible_idx;
	long long drawer_last_click_ms;
	int drawer_width_cols;
	int drawer_width_user_set;
	int drawer_resize_active;
	enum editorCursorStyle cursor_style;
	enum editorThemeColor syntax_theme[EDITOR_SYNTAX_HL_CLASS_COUNT];
	enum editorViewportMode viewport_mode;
	enum editorPaneFocus pane_focus;
	struct editorKeymap keymap;
	struct termios orig_attrs;
};

extern struct editorConfig E;

enum editorKey {
	BACKSPACE = 127,
	ARROW_LEFT = 90000,
	ARROW_DOWN,
	ARROW_UP,
	ARROW_RIGHT,
	ALT_ARROW_LEFT,
	ALT_ARROW_RIGHT,
	ALT_ARROW_DOWN,
	ALT_ARROW_UP,
	ALT_SHIFT_ARROW_LEFT,
	ALT_SHIFT_ARROW_RIGHT,
	ALT_SHIFT_ARROW_DOWN,
	ALT_SHIFT_ARROW_UP,
	CTRL_ARROW_LEFT,
	CTRL_ARROW_RIGHT,
	CTRL_ARROW_DOWN,
	CTRL_ARROW_UP,
	CTRL_ALT_ARROW_LEFT,
	CTRL_ALT_ARROW_RIGHT,
	CTRL_ALT_ARROW_DOWN,
	CTRL_ALT_ARROW_UP,
	PAGE_UP,
	PAGE_DOWN,
	HOME_KEY,
	END_KEY,
	DEL_KEY,
	MOUSE_EVENT,
	RESIZE_EVENT,
	INPUT_EOF_EVENT,
	TASK_EVENT
};

#endif
