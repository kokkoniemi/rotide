#ifndef ROTIDE_H
#define ROTIDE_H

#include "config/theme_config.h"
#include "debug/dap.h"
#include "language/syntax.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>

#define CTRL_KEY(k) ((k) & 0x1f)
#define ROTIDE_VERSION "0.0.1"
#define ROTIDE_TAB_WIDTH 8
#define ROTIDE_INDENT_WIDTH_DEFAULT 4
#define ROTIDE_INDENT_WIDTH_MAX 16
#define ROTIDE_UNDO_HISTORY_LIMIT 200
#define ROTIDE_OSC52_MAX_COPY_BYTES ((size_t)100000)
#define ROTIDE_MAX_TEXT_BYTES ((size_t)INT_MAX)
#define ROTIDE_KEYMAP_MAX_BINDINGS 128
#define ROTIDE_MAX_TABS 128
#define ROTIDE_TAB_TITLE_MAX_COLS 25
#define ROTIDE_TAB_TRUNC_MARKER "..."
#define ROTIDE_DRAWER_DEFAULT_WIDTH 24
#define ROTIDE_DRAWER_COLLAPSED_WIDTH 3
#define ROTIDE_ALT_LETTER_KEY_BASE 91000
#define ROTIDE_CTRL_ALT_LETTER_KEY_BASE 91026
#define ROTIDE_TASK_LOG_MAX_BYTES ((size_t)131072)

#define EDITOR_ALT_LETTER_KEY(ch) (ROTIDE_ALT_LETTER_KEY_BASE + ((int)(ch) - (int)'a'))
#define EDITOR_CTRL_ALT_LETTER_KEY(ch) (ROTIDE_CTRL_ALT_LETTER_KEY_BASE + ((int)(ch) - (int)'a'))

#define EDITOR_IS_ALT_LETTER_KEY(key)                                                              \
	((key) >= ROTIDE_ALT_LETTER_KEY_BASE && (key) < ROTIDE_ALT_LETTER_KEY_BASE + 26)
#define EDITOR_IS_CTRL_ALT_LETTER_KEY(key)                                                         \
	((key) >= ROTIDE_CTRL_ALT_LETTER_KEY_BASE && (key) < ROTIDE_CTRL_ALT_LETTER_KEY_BASE + 26)

#define EDITOR_ALT_LETTER_FROM_KEY(key) ((char)('a' + ((int)(key) - ROTIDE_ALT_LETTER_KEY_BASE)))
#define EDITOR_CTRL_ALT_LETTER_FROM_KEY(key)                                                       \
	((char)('a' + ((int)(key) - ROTIDE_CTRL_ALT_LETTER_KEY_BASE)))

typedef void (*editorClipboardExternalSink)(const char *text, size_t len);

enum editorMouseEventKind {
	EDITOR_MOUSE_EVENT_NONE = 0,
	EDITOR_MOUSE_EVENT_LEFT_PRESS,
	EDITOR_MOUSE_EVENT_LEFT_DRAG,
	EDITOR_MOUSE_EVENT_LEFT_RELEASE,
	EDITOR_MOUSE_EVENT_RIGHT_PRESS,
	EDITOR_MOUSE_EVENT_MOTION,
	EDITOR_MOUSE_EVENT_WHEEL_UP,
	EDITOR_MOUSE_EVENT_WHEEL_DOWN,
	EDITOR_MOUSE_EVENT_WHEEL_LEFT,
	EDITOR_MOUSE_EVENT_WHEEL_RIGHT
};

enum editorMouseModifierFlags {
	EDITOR_MOUSE_MOD_NONE = 0,
	EDITOR_MOUSE_MOD_SHIFT = 1 << 0,
	EDITOR_MOUSE_MOD_ALT = 1 << 1,
	EDITOR_MOUSE_MOD_CTRL = 1 << 2
};

struct editorMouseEvent {
	enum editorMouseEventKind kind;
	int x;
	int y;
	int modifiers;
};

struct editorTextSource;
typedef const char *(*editorTextSourceReadFn)(const struct editorTextSource *source,
                                              size_t byte_index, uint32_t *bytes_read);

struct editorTextSource {
	editorTextSourceReadFn read;
	const void *context;
	size_t length;
};

struct editorRow {
	int rsize;
	int render_display_cols;
	char *render;
	int wrap_cache_body_cols;
	int wrap_cache_segment_count;
	int wrap_cache_indent_cols;
	int wrap_cache_capacity;
	int *wrap_cache_segments;
};

struct editorSelectionRange {
	int start_cy;
	int start_cx;
	int end_cy;
	int end_cx;
};

struct editorColumnSelectionRect {
	int top_cy;
	int bottom_cy;
	int left_rx;
	int right_rx;
};

struct editorLspDiagnostic {
	int start_line;
	int start_character;
	int end_line;
	int end_character;
	int severity;
	char *message;
};

struct editorDrawerNode;
struct editorSyntaxState;
struct editorDocument;
struct editorLspSymbol;

struct editorPopupItem {
	char *label;
	char *detail;
};

enum editorPopupKind {
	EDITOR_POPUP_KIND_AUTOCOMPLETE = 0,
	EDITOR_POPUP_KIND_DRAWER_MENU,
	EDITOR_POPUP_KIND_EDITOR_CONTEXT_MENU,
	EDITOR_POPUP_KIND_TAB_CONTEXT_MENU
};

struct editorPopupState {
	int visible;
	enum editorPopupKind kind;
	int anchor_row;
	int anchor_col;
	int selected_index;
	int row_offset;
	struct editorPopupItem *items;
	int item_count;
};

enum editorPrimaryFocus { EDITOR_PRIMARY_FOCUS_TEXT = 0, EDITOR_PRIMARY_FOCUS_DRAWER };

struct editorPaneNode;

enum editorCursorStyle {
	EDITOR_CURSOR_STYLE_BLOCK = 0,
	EDITOR_CURSOR_STYLE_BAR,
	EDITOR_CURSOR_STYLE_UNDERLINE
};

enum editorViewportMode { EDITOR_VIEWPORT_FOLLOW_CURSOR = 0, EDITOR_VIEWPORT_FREE_SCROLL };

enum editorTabKind {
	EDITOR_TAB_FILE = 0,
	EDITOR_TAB_TASK_LOG,
	EDITOR_TAB_UNSUPPORTED_FILE,
	EDITOR_TAB_GIT_DIFF
};

enum editorGitStatus {
	EDITOR_GIT_STATUS_CLEAN = 0,
	EDITOR_GIT_STATUS_MODIFIED,
	EDITOR_GIT_STATUS_UNTRACKED,
	EDITOR_GIT_STATUS_CONFLICT
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
	int is_search_header;
	int is_placeholder;
	enum editorGitStatus git_status;
	int line;
	int character;
	int lsp_problem_severity;
	int lsp_problem_kind_len;
};

struct editorProjectSearchResult {
	char *path;
	int line;
	int col;
	char *line_text;
	char *display;
};

struct editorTabLayoutEntry {
	int tab_idx;
	int start_col;
	int width_cols;
	int show_left_overflow;
	int show_right_overflow;
	int is_active;
};

struct editorFileDiskState {
	int known;
	int exists;
	dev_t dev;
	ino_t ino;
	off_t size;
	struct timespec mtime;
	struct timespec ctime;
};

enum editorAction {
	EDITOR_ACTION_QUIT = 0,
	EDITOR_ACTION_SAVE,
	EDITOR_ACTION_NEW_TAB,
	EDITOR_ACTION_CLOSE_TAB,
	EDITOR_ACTION_NEXT_TAB,
	EDITOR_ACTION_PREV_TAB,
	EDITOR_ACTION_FOCUS_DRAWER,
	EDITOR_ACTION_TOGGLE_DRAWER,
	EDITOR_ACTION_MAIN_MENU,
	EDITOR_ACTION_CONTEXT_MENU,
	EDITOR_ACTION_RESIZE_DRAWER_NARROW,
	EDITOR_ACTION_RESIZE_DRAWER_WIDEN,
	EDITOR_ACTION_TOGGLE_LINE_WRAP,
	EDITOR_ACTION_TOGGLE_LINE_NUMBERS,
	EDITOR_ACTION_TOGGLE_CURRENT_LINE_HIGHLIGHT,
	EDITOR_ACTION_FIND_FILE,
	EDITOR_ACTION_PROJECT_SEARCH,
	EDITOR_ACTION_FIND,
	EDITOR_ACTION_GOTO_LINE,
	EDITOR_ACTION_GOTO_MATCHING_BRACKET,
	EDITOR_ACTION_GOTO_DEFINITION,
	EDITOR_ACTION_GOTO_IMPLEMENTATION,
	EDITOR_ACTION_GOTO_SYMBOL,
	EDITOR_ACTION_ESLINT_FIX,
	EDITOR_ACTION_TOGGLE_SELECTION,
	EDITOR_ACTION_SELECT_ALL,
	EDITOR_ACTION_SELECT_LEFT,
	EDITOR_ACTION_SELECT_RIGHT,
	EDITOR_ACTION_SELECT_UP,
	EDITOR_ACTION_SELECT_DOWN,
	EDITOR_ACTION_SELECT_WORD_LEFT,
	EDITOR_ACTION_SELECT_WORD_RIGHT,
	EDITOR_ACTION_SELECT_HOME,
	EDITOR_ACTION_SELECT_END,
	EDITOR_ACTION_COPY_SELECTION,
	EDITOR_ACTION_CUT_SELECTION,
	EDITOR_ACTION_DELETE_SELECTION,
	EDITOR_ACTION_PASTE,
	EDITOR_ACTION_UNDO,
	EDITOR_ACTION_REDO,
	EDITOR_ACTION_MOVE_HOME,
	EDITOR_ACTION_MOVE_END,
	EDITOR_ACTION_MOVE_WORD_LEFT,
	EDITOR_ACTION_MOVE_WORD_RIGHT,
	EDITOR_ACTION_PAGE_UP,
	EDITOR_ACTION_PAGE_DOWN,
	EDITOR_ACTION_SCROLL_LEFT,
	EDITOR_ACTION_SCROLL_RIGHT,
	EDITOR_ACTION_SCROLL_UP,
	EDITOR_ACTION_SCROLL_DOWN,
	EDITOR_ACTION_MOVE_UP,
	EDITOR_ACTION_MOVE_DOWN,
	EDITOR_ACTION_MOVE_LEFT,
	EDITOR_ACTION_MOVE_RIGHT,
	EDITOR_ACTION_NEWLINE,
	EDITOR_ACTION_ESCAPE,
	EDITOR_ACTION_REDRAW,
	EDITOR_ACTION_DELETE_CHAR,
	EDITOR_ACTION_BACKSPACE,
	EDITOR_ACTION_MOVE_LINE_UP,
	EDITOR_ACTION_MOVE_LINE_DOWN,
	EDITOR_ACTION_TOGGLE_COMMENT,
	EDITOR_ACTION_COLUMN_SELECT_UP,
	EDITOR_ACTION_COLUMN_SELECT_DOWN,
	EDITOR_ACTION_COLUMN_SELECT_LEFT,
	EDITOR_ACTION_COLUMN_SELECT_RIGHT,
	EDITOR_ACTION_FIND_REPLACE,
	EDITOR_ACTION_DRAWER_CREATE_FILE,
	EDITOR_ACTION_DRAWER_CREATE_FOLDER,
	EDITOR_ACTION_DRAWER_RENAME,
	EDITOR_ACTION_DRAWER_DELETE,
	EDITOR_ACTION_GIT_DRAWER,
	EDITOR_ACTION_LSP_DRAWER,
	EDITOR_ACTION_DAP_DRAWER,
	EDITOR_ACTION_DAP_START,
	EDITOR_ACTION_DAP_STOP,
	EDITOR_ACTION_DAP_CONTINUE,
	EDITOR_ACTION_DAP_PAUSE,
	EDITOR_ACTION_DAP_STEP_OVER,
	EDITOR_ACTION_DAP_STEP_INTO,
	EDITOR_ACTION_DAP_STEP_OUT,
	EDITOR_ACTION_DAP_TOGGLE_BREAKPOINT,
	EDITOR_ACTION_SPLIT_HORIZONTAL,
	EDITOR_ACTION_SPLIT_VERTICAL,
	EDITOR_ACTION_CLOSE_PANE,
	EDITOR_ACTION_FOCUS_LEFT_PANE,
	EDITOR_ACTION_FOCUS_RIGHT_PANE,
	EDITOR_ACTION_FOCUS_UP_PANE,
	EDITOR_ACTION_FOCUS_DOWN_PANE,
	EDITOR_ACTION_MOVE_TAB_LEFT_PANE,
	EDITOR_ACTION_MOVE_TAB_RIGHT_PANE,
	EDITOR_ACTION_MOVE_TAB_UP_PANE,
	EDITOR_ACTION_MOVE_TAB_DOWN_PANE,
	EDITOR_ACTION_PANE_GROW,
	EDITOR_ACTION_PANE_SHRINK,
	EDITOR_ACTION_TERMINAL_OPEN,
	EDITOR_ACTION_TERMINAL_OPEN_VERTICAL,
	EDITOR_ACTION_TERMINAL_PREFIX,
	EDITOR_ACTION_OPEN_SETTINGS,
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

enum editorDrawerMode {
	EDITOR_DRAWER_MODE_TREE = 0,
	EDITOR_DRAWER_MODE_MAIN_MENU,
	EDITOR_DRAWER_MODE_FILE_SEARCH,
	EDITOR_DRAWER_MODE_PROJECT_SEARCH,
	EDITOR_DRAWER_MODE_GIT,
	EDITOR_DRAWER_MODE_LSP,
	EDITOR_DRAWER_MODE_DAP
};

struct editorGitEntry {
	char *rel_path;
	enum editorGitStatus status;
	char index_status;
	char worktree_status;
};

struct editorHistoryEntry {
	enum editorEditKind kind;
	size_t start_offset;
	char *removed_text;
	size_t removed_len;
	char *inserted_text;
	size_t inserted_len;
	size_t before_cursor_offset;
	size_t after_cursor_offset;
	int before_dirty;
	int after_dirty;
};

struct editorHistory {
	struct editorHistoryEntry entries[ROTIDE_UNDO_HISTORY_LIMIT];
	int start;
	int len;
};

#define EDITOR_ACTIVE_BUFFER_CORE_FIELDS(X)                                                        \
	X(enum editorTabKind, tab_kind)                                                            \
	X(int, is_preview)                                                                         \
	X(char *, tab_title)                                                                       \
	X(size_t, cursor_offset)                                                                   \
	X(int, cx)                                                                                 \
	X(int, cy)                                                                                 \
	X(int, rx)                                                                                 \
	X(int, rowoff)                                                                             \
	X(int, coloff)                                                                             \
	X(int, wrapoff)                                                                            \
	X(int, numrows)                                                                            \
	X(struct editorRow *, rows)                                                                \
	X(struct editorDocument *, document)                                                       \
	X(int, dirty)                                                                              \
	X(char *, filename)                                                                        \
	X(struct editorFileDiskState, disk_state)                                                  \
	X(int, disk_conflict)                                                                      \
	X(enum editorSyntaxLanguage, syntax_language)                                              \
	X(struct editorSyntaxState *, syntax_state)                                                \
	X(int, syntax_parse_failures)                                                              \
	X(uint64_t, syntax_revision)                                                               \
	X(uint64_t, syntax_generation)                                                             \
	X(int, syntax_background_pending)                                                          \
	X(uint64_t, syntax_pending_revision)                                                       \
	X(int, syntax_pending_first_row)                                                           \
	X(int, syntax_pending_row_count)

#define EDITOR_ACTIVE_BUFFER_LSP_FIELDS(X)                                                         \
	X(int, lsp_doc_open)                                                                       \
	X(int, lsp_doc_version)                                                                    \
	X(int, lsp_eslint_doc_open)                                                                \
	X(int, lsp_eslint_doc_version)                                                             \
	X(struct editorLspDiagnostic *, lsp_diagnostics)                                           \
	X(int, lsp_diagnostic_count)                                                               \
	X(int, lsp_diagnostic_error_count)                                                         \
	X(int, lsp_diagnostic_warning_count)                                                       \
	X(struct editorLspSymbol *, lsp_symbols)                                                   \
	X(int, lsp_symbol_count)

#define EDITOR_ACTIVE_BUFFER_SEARCH_FIELDS(X)                                                      \
	X(char *, search_query)                                                                    \
	X(size_t, search_match_offset)                                                             \
	X(int, search_match_len)                                                                   \
	X(int, search_direction)                                                                   \
	X(size_t, search_saved_offset)                                                             \
	X(int, selection_mode_active)                                                              \
	X(size_t, selection_anchor_offset)                                                         \
	X(int, column_select_active)                                                               \
	X(int, column_select_anchor_cy)                                                            \
	X(int, column_select_anchor_rx)                                                            \
	X(int, column_select_cursor_rx)                                                            \
	X(int, mouse_left_button_down)                                                             \
	X(size_t, mouse_drag_anchor_offset)                                                        \
	X(int, mouse_drag_started)

#define EDITOR_ACTIVE_BUFFER_EDIT_FIELDS(X)                                                        \
	X(struct editorHistory, undo_history)                                                      \
	X(struct editorHistory, redo_history)                                                      \
	X(struct editorHistoryEntry, edit_pending_entry)                                           \
	X(int, edit_pending_entry_valid)                                                           \
	X(enum editorEditKind, edit_group_kind)                                                    \
	X(enum editorEditKind, edit_pending_kind)                                                  \
	X(enum editorEditPendingMode, edit_pending_mode)

#define EDITOR_ACTIVE_BUFFER_FIELDS(X)                                                             \
	EDITOR_ACTIVE_BUFFER_CORE_FIELDS(X)                                                        \
	EDITOR_ACTIVE_BUFFER_LSP_FIELDS(X)                                                         \
	EDITOR_ACTIVE_BUFFER_SEARCH_FIELDS(X)                                                      \
	EDITOR_ACTIVE_BUFFER_EDIT_FIELDS(X)

#define EDITOR_DECLARE_FIELD(type, name) type name;

struct editorBuffer {
	EDITOR_ACTIVE_BUFFER_FIELDS(EDITOR_DECLARE_FIELD)
};

struct editorTabState {
	union {
		struct editorBuffer buffer;
		struct {
			EDITOR_ACTIVE_BUFFER_FIELDS(EDITOR_DECLARE_FIELD)
		};
	};
};

/*
 * The global editor state, instantiated once as `extern struct editorConfig E`.
 * Fields are grouped by C4 container so ownership is visible at a glance:
 *
 *   - Environment: terminal dimensions, the global keymap, the cached theme,
 *     and the saved termios from editorSetRawMode.
 *   - Active buffer: the per-tab editing state of whichever tab is in focus.
 *     Aliased into the active tab's editorBuffer via an X-macro union so the
 *     two views (E.cx vs E.tabs[i].cx, etc.) stay byte-identical.
 *   - Config-derived settings: LSP enable flags + commands, DAP adapter/launch
 *     tables, editor preferences. Populated by the TOML loaders in src/config/.
 *   - Workspace: tab list, drawer model, project/file search results, Git
 *     status, recovery + workspace-state paths, task-log subprocess state.
 *   - Debug: live DAP session state (threads, frames, scopes, variables,
 *     breakpoints, output buffer, owned terminal pane).
 *   - Input transient state: status/message bar, click timing, hover-link
 *     position, paste-active gate, terminal-prefix arm flag.
 *
 * New per-tab buffer fields go in the EDITOR_ACTIVE_BUFFER_*_FIELDS macros
 * above, not here. New cross-container fields should land in a domain header
 * if at all possible, and only as a last resort here.
 */
struct editorConfig {
	/* --- Environment (process/terminal-wide) --- */
	int window_rows;
	int window_cols;

	/* --- Active buffer (aliased onto E.tabs[active_tab].buffer) --- */
	union {
		struct editorBuffer active_buffer;
		struct {
			EDITOR_ACTIVE_BUFFER_FIELDS(EDITOR_DECLARE_FIELD)
		};
	};

	/* --- Config-derived: LSP --- */
	int lsp_gopls_enabled;
	int lsp_clangd_enabled;
	int lsp_html_enabled;
	int lsp_css_enabled;
	int lsp_json_enabled;
	int lsp_javascript_enabled;
	int lsp_eslint_enabled;
	char lsp_gopls_command[PATH_MAX];
	char lsp_gopls_install_command[PATH_MAX];
	char lsp_clangd_command[PATH_MAX];
	char lsp_html_command[PATH_MAX];
	char lsp_css_command[PATH_MAX];
	char lsp_json_command[PATH_MAX];
	char lsp_javascript_command[PATH_MAX];
	char lsp_javascript_install_command[PATH_MAX];
	char lsp_eslint_command[PATH_MAX];
	char lsp_vscode_langservers_install_command[PATH_MAX];
	int lsp_autocomplete_enabled;
	int lsp_autocomplete_max_items;

	/* --- Config-derived: DAP adapter and launch tables --- */
	struct editorDapAdapterConfig dap_adapters[ROTIDE_DAP_MAX_ADAPTERS];
	int dap_adapter_count;
	struct editorDapLaunchConfig dap_defaults[ROTIDE_DAP_MAX_CONFIGS];
	int dap_default_count;
	struct editorDapLaunchConfig dap_launches[ROTIDE_DAP_MAX_CONFIGS];
	int dap_launch_count;
	int dap_project_config_exists;
	int dap_project_config_invalid;
	char dap_project_config_path[PATH_MAX];

	/* --- Input transient: status/message bar, hover, clipboard --- */
	char statusmsg[80];
	time_t statusmsg_time;
	int hover_link_active;
	int hover_link_row;
	int hover_link_cx_start;
	int hover_link_cx_end;
	char *clipboard_text;
	size_t clipboard_textlen;
	editorClipboardExternalSink clipboard_external_sink;

	/* --- Workspace: tabs --- */
	struct editorTabState *tabs;
	int tab_count;
	int tab_capacity;
	int active_tab;
	int close_confirmed;

	/* --- Workspace: task-log subprocess --- */
	pid_t task_pid;
	int task_output_fd;
	int task_running;
	int task_tab_idx;
	int task_output_truncated;
	size_t task_output_bytes;
	int task_exit_code;
	char task_success_status[80];
	char task_failure_status[80];

	/* --- Workspace: recovery + persisted state paths --- */
	char *recovery_path;
	char *workspace_state_path;
	time_t recovery_last_autosave_time;

	/* --- Workspace: drawer (model + selection + width) --- */
	char *drawer_root_path;
	struct editorDrawerNode *drawer_root;
	enum editorDrawerMode drawer_mode;
	unsigned int drawer_menu_expanded;
	unsigned int drawer_git_expanded;
	unsigned int drawer_lsp_expanded;
	unsigned int drawer_dap_expanded;
	int drawer_selected_index;
	int drawer_rowoff;
	int drawer_last_click_visible_idx;
	long long drawer_last_click_ms;
	int drawer_width_cols;
	int drawer_width_user_set;
	int drawer_collapsed;
	int drawer_resize_active;
	/*
	 * Split-border drag-resize: when split_resize_active is non-zero,
	 * mouse-drag events update split_resize_node->as.split.ratio instead
	 * of moving the cursor. Cleared on left-release and whenever the tree
	 * mutates so the pointer cannot dangle.
	 */
	int split_resize_active;
	struct editorPaneNode *split_resize_node;
	int tab_drag_armed;
	int tab_drag_active;
	struct editorPaneNode *tab_drag_source_leaf;
	int tab_drag_source_tab_idx;
	int tab_drag_start_x;
	int tab_drag_start_y;
	int drawer_drag_armed;
	int drawer_drag_active;
	int drawer_drag_source_visible_idx;
	char *drawer_drag_source_path;
	int drawer_drag_just_opened_preview;
	int drawer_drag_start_x;
	int drawer_drag_start_y;

	/* --- Input transient: text/tab click tracking for multi-clicks --- */
	int text_last_click_cy;
	int text_last_click_cx;
	long long text_last_click_ms;
	int text_click_count;
	int tab_last_click_idx;
	long long tab_last_click_ms;

	/* --- Workspace: drawer file-search mode --- */
	char *drawer_search_query;
	size_t drawer_search_query_len;
	char **drawer_search_paths;
	int drawer_search_path_count;
	int drawer_search_path_capacity;
	int *drawer_search_filtered_indices;
	int drawer_search_filtered_count;
	int drawer_search_filtered_capacity;
	char *drawer_search_previewed_path;
	int drawer_search_active_tab_before;
	char **recent_file_paths;
	int recent_file_count;
	int recent_file_capacity;

	/* --- Workspace: drawer project-search mode --- */
	char *drawer_project_search_query;
	size_t drawer_project_search_query_len;
	struct editorProjectSearchResult *drawer_project_search_results;
	int drawer_project_search_result_count;
	int drawer_project_search_result_capacity;
	char *drawer_project_search_previewed_path;
	int drawer_project_search_previewed_line;
	int drawer_project_search_previewed_col;
	int drawer_project_search_active_tab_before;

	/* --- Workspace: Git status snapshot --- */
	char *git_repo_root;
	char *git_branch;
	struct editorGitEntry *git_entries;
	int git_entry_count;
	int git_entry_capacity;

	/* --- Debug: live DAP session state --- */
	struct editorDapBreakpoint dap_breakpoints[ROTIDE_DAP_MAX_BREAKPOINTS];
	int dap_breakpoint_count;
	struct editorDapThread dap_threads[ROTIDE_DAP_MAX_THREADS];
	int dap_thread_count;
	struct editorDapStackFrame dap_stack_frames[ROTIDE_DAP_MAX_STACK_FRAMES];
	int dap_stack_frame_count;
	struct editorDapScope dap_scopes[ROTIDE_DAP_MAX_SCOPES];
	int dap_scope_count;
	struct editorDapVariable dap_variables[ROTIDE_DAP_MAX_VARIABLES];
	int dap_variable_count;
	char dap_output[ROTIDE_DAP_OUTPUT_MAX];
	size_t dap_output_len;
	int dap_running;
	int dap_stopped;
	int dap_selected_launch;
	/*
	 * If non-NULL, points at a terminal pane leaf that the DAP launch
	 * opened (via console="terminal") to host the inferior's tty. The
	 * pane is closed when the DAP session ends, unless the user has
	 * focused the pane and is interacting with it.
	 */
	struct editorPaneNode *dap_terminal_leaf;

	/* --- Config-derived: editor preferences --- */
	enum editorCursorStyle cursor_style;
	int cursor_blink_enabled;
	int line_wrap_enabled;
	int line_numbers_enabled;
	int current_line_highlight_enabled;
	int nerd_fonts_enabled;
	int auto_indent_enabled;
	int indent_use_tabs;
	int indent_width;
	int column_select_drag_modifier;

	/* --- Environment: theme, viewport, primary focus, layout root --- */
	struct editorTheme theme;
	enum editorViewportMode viewport_mode;
	enum editorPrimaryFocus primary_focus;
	struct editorPaneNode *layout_root;
	struct editorPaneNode *focused_leaf;

	/* --- Input transient: one-shot terminal-prefix and paste gates --- */
	/*
	 * When non-zero, the next keypress is interpreted as a rotide keymap
	 * action even though the focused pane is a terminal (which normally
	 * routes keys straight to its PTY). Set by the terminal_prefix action
	 * and cleared after the following key is dispatched.
	 */
	int terminal_prefix_armed;
	/*
	 * Non-zero between BRACKETED_PASTE_START_EVENT and
	 * BRACKETED_PASTE_END_EVENT. Used by terminal panes so the
	 * libvterm paste markers are sent to the child only when the
	 * editor was actually told a paste is in progress.
	 */
	int paste_active;

	/* --- Environment: keymap, popup overlay, saved termios --- */
	struct editorKeymap keymap;
	struct editorPopupState popup;
	struct termios orig_attrs;
};

#undef EDITOR_DECLARE_FIELD

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
	CTRL_SHIFT_ALT_ARROW_LEFT,
	CTRL_SHIFT_ALT_ARROW_RIGHT,
	CTRL_SHIFT_ALT_ARROW_DOWN,
	CTRL_SHIFT_ALT_ARROW_UP,
	SHIFT_ARROW_LEFT,
	SHIFT_ARROW_RIGHT,
	SHIFT_ARROW_DOWN,
	SHIFT_ARROW_UP,
	CTRL_SHIFT_ARROW_LEFT,
	CTRL_SHIFT_ARROW_RIGHT,
	CTRL_SHIFT_ARROW_DOWN,
	CTRL_SHIFT_ARROW_UP,
	SHIFT_HOME_KEY,
	SHIFT_END_KEY,
	PAGE_UP,
	PAGE_DOWN,
	HOME_KEY,
	END_KEY,
	DEL_KEY,
	MOUSE_EVENT,
	RESIZE_EVENT,
	INPUT_EOF_EVENT,
	TASK_EVENT,
	SYNTAX_EVENT,
	WATCH_EVENT,
	TERMINAL_EVENT,
	BRACKETED_PASTE_START_EVENT,
	BRACKETED_PASTE_END_EVENT
};

#endif
