#include "render/status_bar.h"

#include "config/theme_config.h"
#include "debug/dap.h"
#include "input/input_system.h"
#include "render/ansi_style.h"
#include "render/display_text.h"
#include "render/write_buf.h"
#include "rotide.h"
#include "terminal/terminal_pane.h"
#include "workspace/drawer.h"
#include "workspace/git.h"
#include "workspace/tabs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define VT100_CLEAR_ROW_3 "\x1b[K"
#define VT100_BOLD_ON "\x1b[1m"
#define VT100_BOLD_OFF "\x1b[22m"

/* Nerd-font glyphs for the debug controls (same PUA range the
 * drawer icons use). Each renders in one display column. */
#define STATUS_DAP_ICON_CONT "\xEF\x81\x8B"    /* U+F04B play */
#define STATUS_DAP_ICON_OVER "\xEF\x81\xA1"    /* U+F061 arrow-right */
#define STATUS_DAP_ICON_INTO "\xEF\x85\x89"    /* U+F149 level-down */
#define STATUS_DAP_ICON_OUT "\xEF\x85\x88"     /* U+F148 level-up */
#define STATUS_DAP_ICON_PAUSE "\xEF\x81\x8C"   /* U+F04C pause */
#define STATUS_DAP_ICON_RESTART "\xEF\x80\xA1" /* U+F021 refresh */
#define STATUS_DAP_ICON_STOP "\xEF\x81\x8D"    /* U+F04D stop */

/* Git action glyphs (same nerd-font PUA range as the drawer icons). */
#define STATUS_GIT_ICON_STAGE "\xEF\x81\xA7"     /* U+F067 plus */
#define STATUS_GIT_ICON_UNSTAGE "\xEF\x81\xA8"   /* U+F068 minus */
#define STATUS_GIT_ICON_STAGE_ALL "\xEF\x81\x95" /* U+F055 plus-circle */
#define STATUS_GIT_ICON_DISCARD "\xEF\x87\xB8"   /* U+F1F8 trash */
#define STATUS_GIT_ICON_COMMIT "\xEF\x80\x8C"    /* U+F00C check */
#define STATUS_GIT_ICON_AMEND "\xEF\x81\x84"     /* U+F044 pencil-square */
#define STATUS_GIT_ICON_BRANCH "\xEF\x84\xA6"    /* U+F126 code-branch */
#define STATUS_GIT_ICON_LOG "\xEF\x87\x9A"       /* U+F1DA history */
#define STATUS_GIT_ICON_STASH "\xEF\x86\x87"     /* U+F187 archive */
#define STATUS_GIT_ICON_SHOW "\xEF\x81\xAE"      /* U+F06E eye */
#define STATUS_GIT_ICON_CHECKOUT "\xEF\x81\xA1"  /* U+F061 arrow-right */
#define STATUS_GIT_ICON_TAG "\xEF\x80\xAB"       /* U+F02B tag */
#define STATUS_GIT_ICON_CHERRY "\xEF\x83\x85"    /* U+F0C5 copy */
#define STATUS_GIT_ICON_REVERT "\xEF\x83\xA2"    /* U+F0E2 undo */
#define STATUS_GIT_ICON_APPLY "\xEF\x80\x99"     /* U+F019 download */
#define STATUS_GIT_ICON_POP "\xEF\x80\x9A"       /* U+F01A arrow-circle-down */
#define STATUS_GIT_ICON_REFRESH "\xEF\x80\xA1"   /* U+F021 refresh */
#define STATUS_GIT_ICON_EXPAND "\xEF\x81\xA5"    /* U+F065 expand */
#define STATUS_GIT_ICON_COMPRESS "\xEF\x81\xA6"  /* U+F066 compress */
#define STATUS_GIT_ICON_ABORT "\xEF\x80\x8D"     /* U+F00D close */

#define STATUS_INPUT_SEGMENT_MAX_COLS 24

/*
 * Clickable action buttons recorded during the most recent status-bar render,
 * shared by every segment that draws them (debug/DAP controls, git actions, and
 * terminal mode/pane controls). Columns are 0-based offsets within the status
 * row. The mouse layer maps a left-press column to the button's action; see
 * editorStatusBarButtonAt.
 */
#define STATUS_BAR_MAX_BUTTONS 12
struct statusBarButton {
	int start_col;
	int end_col;
	enum editorAction action;
};
static struct statusBarButton g_status_bar_buttons[STATUS_BAR_MAX_BUTTONS];
static int g_status_bar_button_count;
/*
 * Column offset added to every recorded button span. The segment renders with
 * a local column that starts at 0, but the segment itself may sit further right
 * (e.g. after the input-system badge); this offset keeps the clickable spans in
 * absolute status-row columns so the mouse layer resolves hits correctly.
 */
static int g_status_bar_button_col_offset;

static void statusBarButtonsReset(int col_offset) {
	g_status_bar_button_count = 0;
	g_status_bar_button_col_offset = col_offset;
}

/*
 * Appends a control button at *col (capped at max_col), records the clickable
 * span, and advances *col. `icon` (a one-column nerd glyph), `label`, and
 * `hotkey` are each optional; single spaces separate the present parts, and
 * the hotkey renders italic so it reads as a hint without overriding the
 * theme's status foreground.
 * `icon_color`, if non-NULL, tints the icon glyph, restoring the status style
 * afterward. A button with no room is silently dropped. Returns 0 only on a
 * write failure. Labels and hotkeys must be ASCII (measured in bytes).
 */
static int statusBarAppendButton(struct writeBuf *wb, int *col, int max_col, const char *icon,
                                 const struct editorThemeColor *icon_color, const char *label,
                                 const char *hotkey, enum editorAction action) {
	int has_icon = icon != NULL && icon[0] != '\0';
	int label_cols = label != NULL ? (int)strlen(label) : 0;
	int hotkey_cols = hotkey != NULL ? (int)strlen(hotkey) : 0;
	int sep = (has_icon && label_cols > 0) ? 1 : 0;
	int hotkey_sep = (hotkey_cols > 0 && (has_icon || label_cols > 0)) ? 1 : 0;
	int cols = (has_icon ? 1 : 0) + sep + label_cols + hotkey_sep + hotkey_cols;
	if (cols == 0 || *col + cols > max_col) {
		return 1;
	}
	int start_col = *col;
	if (has_icon) {
		if (icon_color != NULL && !editorAppendThemeForeground(wb, *icon_color)) {
			return 0;
		}
		if (!wbAppend(wb, icon, strlen(icon))) {
			return 0;
		}
		/* Restore the status foreground so the label / rest of the bar is plain. */
		if (icon_color != NULL && !editorAppendThemeStyle(wb, EDITOR_THEME_STYLE_STATUS)) {
			return 0;
		}
	}
	if (sep && !wbAppend(wb, " ", 1)) {
		return 0;
	}
	if (label_cols > 0 && !wbAppend(wb, label, (size_t)label_cols)) {
		return 0;
	}
	if (hotkey_cols > 0) {
		if (hotkey_sep && !wbAppend(wb, " ", 1)) {
			return 0;
		}
		if (!wbAppend(wb, "\x1b[3m", 4) || !wbAppend(wb, hotkey, (size_t)hotkey_cols) ||
		    !wbAppend(wb, "\x1b[23m", 5)) {
			return 0;
		}
	}
	if (g_status_bar_button_count < STATUS_BAR_MAX_BUTTONS) {
		g_status_bar_buttons[g_status_bar_button_count].start_col =
		        g_status_bar_button_col_offset + start_col;
		g_status_bar_buttons[g_status_bar_button_count].end_col =
		        g_status_bar_button_col_offset + start_col + cols;
		g_status_bar_buttons[g_status_bar_button_count].action = action;
		g_status_bar_button_count++;
	}
	*col += cols;
	/* Three trailing spaces between buttons: a wider gap than the single space
	 * separating a label from its hotkey, so each hotkey groups visually with
	 * the button it belongs to. */
	if (*col + 3 <= max_col && !wbAppend(wb, "   ", 3)) {
		return 0;
	}
	*col += (*col + 3 <= max_col) ? 3 : 0;
	return 1;
}

/*
 * Renders the debug control segment at the left of the status bar when a DAP
 * session is active: a PAUSED/RUNNING badge then context-appropriate buttons.
 * Stopped → Cont/Over/Into/Out/Restart/Stop; running → Pause/Restart/Stop.
 * Records button spans for the mouse layer (offset by `col_offset`, the segment's
 * absolute start column); writes the columns consumed to *col_io. `max_col`
 * bounds the segment width so it never overruns the right side.
 */
static int statusBarAppendDebugSegment(struct writeBuf *wb, int max_col, int *col_io,
                                       int col_offset) {
	statusBarButtonsReset(col_offset);
	int col = 0;
	if (col + 1 <= max_col && !wbAppend(wb, " ", 1)) {
		return 0;
	}
	col += (col + 1 <= max_col) ? 1 : 0;

	const char *badge = editorDapIsStopped() ? "PAUSED" : "RUNNING";
	int blen = (int)strlen(badge);
	if (col + blen <= max_col) {
		if (!wbAppend(wb, VT100_BOLD_ON, (int)strlen(VT100_BOLD_ON)) ||
		    !wbAppend(wb, badge, (size_t)blen) ||
		    !wbAppend(wb, VT100_BOLD_OFF, (int)strlen(VT100_BOLD_OFF))) {
			return 0;
		}
		col += blen;
		if (col + 2 <= max_col && !wbAppend(wb, "  ", 2)) {
			return 0;
		}
		col += (col + 2 <= max_col) ? 2 : 0;
	}

	/* Accent the play/restart/stop glyphs from the theme's ANSI palette so they
	 * harmonize with the active theme (the palette has no orange, so restart uses
	 * the warm yellow slot). */
	struct editorThemeColor color_cont = editorThemeResolveAnsi(EDITOR_THEME_ANSI_GREEN, 1);
	struct editorThemeColor color_restart = editorThemeResolveAnsi(EDITOR_THEME_ANSI_YELLOW, 1);
	struct editorThemeColor color_stop = editorThemeResolveAnsi(EDITOR_THEME_ANSI_RED, 1);

	int nerd = E.nerd_fonts_enabled;
	if (editorDapIsStopped()) {
		if (!statusBarAppendButton(wb, &col, max_col, nerd ? STATUS_DAP_ICON_CONT : NULL,
		                           &color_cont, "Cont", NULL, EDITOR_ACTION_DAP_CONTINUE) ||
		    !statusBarAppendButton(wb, &col, max_col, nerd ? STATUS_DAP_ICON_OVER : NULL,
		                           NULL, "Over", NULL, EDITOR_ACTION_DAP_STEP_OVER) ||
		    !statusBarAppendButton(wb, &col, max_col, nerd ? STATUS_DAP_ICON_INTO : NULL,
		                           NULL, "Into", NULL, EDITOR_ACTION_DAP_STEP_INTO) ||
		    !statusBarAppendButton(wb, &col, max_col, nerd ? STATUS_DAP_ICON_OUT : NULL,
		                           NULL, "Out", NULL, EDITOR_ACTION_DAP_STEP_OUT)) {
			return 0;
		}
	} else if (!statusBarAppendButton(wb, &col, max_col, nerd ? STATUS_DAP_ICON_PAUSE : NULL,
	                                  NULL, "Pause", NULL, EDITOR_ACTION_DAP_PAUSE)) {
		return 0;
	}
	/* Restart and Stop are icon-only (and accent-colored) when nerd fonts are on. */
	if (!statusBarAppendButton(wb, &col, max_col, nerd ? STATUS_DAP_ICON_RESTART : NULL,
	                           &color_restart, nerd ? NULL : "Restart", NULL,
	                           EDITOR_ACTION_DAP_RESTART) ||
	    !statusBarAppendButton(wb, &col, max_col, nerd ? STATUS_DAP_ICON_STOP : NULL,
	                           &color_stop, nerd ? NULL : "Stop", NULL,
	                           EDITOR_ACTION_DAP_STOP)) {
		return 0;
	}
	*col_io = col;
	return 1;
}

/* Which git surface owns the status bar's action segment, if any. */
enum statusBarGitContext {
	STATUS_GIT_CONTEXT_NONE = 0,
	STATUS_GIT_CONTEXT_DRAWER,
	STATUS_GIT_CONTEXT_DIFF,
	STATUS_GIT_CONTEXT_BRANCHES,
	STATUS_GIT_CONTEXT_LOG,
	STATUS_GIT_CONTEXT_STASH,
	STATUS_GIT_CONTEXT_COMMIT
};

static enum statusBarGitContext statusBarGitContext(void) {
	if (E.primary_focus == EDITOR_PRIMARY_FOCUS_DRAWER) {
		if (E.drawer_mode == EDITOR_DRAWER_MODE_GIT && !editorDrawerIsCollapsed()) {
			return STATUS_GIT_CONTEXT_DRAWER;
		}
		return STATUS_GIT_CONTEXT_NONE;
	}
	switch (E.tab_kind) {
		case EDITOR_TAB_GIT_DIFF:
			return STATUS_GIT_CONTEXT_DIFF;
		case EDITOR_TAB_GIT_BRANCHES:
			return STATUS_GIT_CONTEXT_BRANCHES;
		case EDITOR_TAB_GIT_LOG:
			return STATUS_GIT_CONTEXT_LOG;
		case EDITOR_TAB_GIT_STASH:
			return STATUS_GIT_CONTEXT_STASH;
		case EDITOR_TAB_GIT_COMMIT:
			return STATUS_GIT_CONTEXT_COMMIT;
		default:
			return STATUS_GIT_CONTEXT_NONE;
	}
}

struct statusBarGitButton {
	const char *icon;
	const char *label;
	const char *hotkey;
	enum editorAction action;
};

static void statusBarGitButtonAdd(struct statusBarGitButton *buttons, int *count, int max,
                                  const char *icon, const char *label, const char *hotkey,
                                  enum editorAction action) {
	if (*count >= max) {
		return;
	}
	buttons[*count].icon = icon;
	buttons[*count].label = label;
	buttons[*count].hotkey = hotkey;
	buttons[*count].action = action;
	(*count)++;
}

/*
 * Builds the git drawer's button list from what is actually possible right
 * now: stage or unstage (by the selected row's group) and discard only when a
 * file row is selected, group-wide stage/unstage when a group header is
 * selected, commit only when something is staged. View openers and amend are
 * always available.
 */
static int statusBarGitDrawerButtons(struct statusBarGitButton *buttons, int max) {
	int count = 0;
	int has_staged = 0;
	for (int i = 0; i < E.git_entry_count; i++) {
		char x = E.git_entries[i].index_status;
		if (x != ' ' && x != '?' && x != '\0') {
			has_staged = 1;
			break;
		}
	}

	int entry_idx = 0;
	int staged_group = 0;
	int group_items = 0;
	if (editorDrawerGitSelectedFile(&entry_idx, &staged_group)) {
		if (staged_group) {
			statusBarGitButtonAdd(buttons, &count, max, STATUS_GIT_ICON_UNSTAGE,
			                      "Unstage", "u", EDITOR_ACTION_GIT_UNSTAGE);
		} else {
			statusBarGitButtonAdd(buttons, &count, max, STATUS_GIT_ICON_STAGE, "Stage",
			                      "s", EDITOR_ACTION_GIT_STAGE);
		}
		statusBarGitButtonAdd(buttons, &count, max, STATUS_GIT_ICON_DISCARD, "Discard", "d",
		                      EDITOR_ACTION_GIT_DISCARD);
	} else if (editorDrawerGitSelectedGroup(&staged_group, &group_items) && group_items > 0) {
		/* Group header: the action applies to every file in the group. */
		if (staged_group) {
			statusBarGitButtonAdd(buttons, &count, max, STATUS_GIT_ICON_UNSTAGE,
			                      "Unstage all", "u", EDITOR_ACTION_GIT_UNSTAGE);
		} else {
			statusBarGitButtonAdd(buttons, &count, max, STATUS_GIT_ICON_STAGE,
			                      "Stage all", "s", EDITOR_ACTION_GIT_STAGE);
		}
	}
	if (has_staged) {
		statusBarGitButtonAdd(buttons, &count, max, STATUS_GIT_ICON_COMMIT, "Commit", "c",
		                      EDITOR_ACTION_GIT_COMMIT);
	}
	statusBarGitButtonAdd(buttons, &count, max, STATUS_GIT_ICON_AMEND, "Amend", "A",
	                      EDITOR_ACTION_GIT_COMMIT_AMEND);
	statusBarGitButtonAdd(buttons, &count, max, STATUS_GIT_ICON_BRANCH, "Branches", "B",
	                      EDITOR_ACTION_GIT_BRANCHES);
	statusBarGitButtonAdd(buttons, &count, max, STATUS_GIT_ICON_LOG, "Log", "L",
	                      EDITOR_ACTION_GIT_LOG);
	statusBarGitButtonAdd(buttons, &count, max, STATUS_GIT_ICON_STASH, "Stash", "S",
	                      EDITOR_ACTION_GIT_STASHES);
	return count;
}

/*
 * Renders the git action segment at the left of the status bar: one clickable
 * button per action available on the focused git surface. Labels start with
 * the key that triggers them (Stage → s, Amend → A, …) so the keys teach
 * themselves; buttons that do not fit are dropped from the right.
 */
static int statusBarAppendGitSegment(struct writeBuf *wb, enum statusBarGitContext context,
                                     int max_col, int *col_io, int col_offset) {
	static const struct statusBarGitButton k_branches[] = {
	        {STATUS_GIT_ICON_CHECKOUT, "Checkout", "enter", EDITOR_ACTION_GIT_VIEW_ACTIVATE},
	        {STATUS_GIT_ICON_STAGE, "New", "n", EDITOR_ACTION_GIT_BRANCH_NEW},
	        {STATUS_GIT_ICON_DISCARD, "Delete", "d", EDITOR_ACTION_GIT_BRANCH_DELETE},
	        {STATUS_GIT_ICON_REFRESH, "Refresh", "R", EDITOR_ACTION_GIT_REFRESH},
	};
	static const struct statusBarGitButton k_log[] = {
	        {STATUS_GIT_ICON_SHOW, "Show", "enter", EDITOR_ACTION_GIT_VIEW_ACTIVATE},
	        {STATUS_GIT_ICON_CHERRY, "Cherry-pick", "c", EDITOR_ACTION_GIT_CHERRY_PICK},
	        {STATUS_GIT_ICON_REVERT, "Revert", "r", EDITOR_ACTION_GIT_REVERT},
	        {STATUS_GIT_ICON_TAG, "Tag", "t", EDITOR_ACTION_GIT_TAG},
	        {STATUS_GIT_ICON_REFRESH, "Refresh", "R", EDITOR_ACTION_GIT_REFRESH},
	};
	static const struct statusBarGitButton k_stash[] = {
	        {STATUS_GIT_ICON_SHOW, "Show", "enter", EDITOR_ACTION_GIT_VIEW_ACTIVATE},
	        {STATUS_GIT_ICON_APPLY, "Apply", "a", EDITOR_ACTION_GIT_STASH_APPLY},
	        {STATUS_GIT_ICON_POP, "Pop", "p", EDITOR_ACTION_GIT_STASH_POP},
	        {STATUS_GIT_ICON_DISCARD, "Drop", "d", EDITOR_ACTION_GIT_STASH_DROP},
	        {STATUS_GIT_ICON_REFRESH, "Refresh", "R", EDITOR_ACTION_GIT_REFRESH},
	};
	static const struct statusBarGitButton k_commit[] = {
	        {STATUS_GIT_ICON_COMMIT, "Commit", "save", EDITOR_ACTION_SAVE},
	        {STATUS_GIT_ICON_ABORT, "Abort", "close", EDITOR_ACTION_CLOSE_TAB},
	};
	static const struct statusBarGitButton k_diff_hunks[] = {
	        {STATUS_GIT_ICON_EXPAND, "Show whole", "z", EDITOR_ACTION_GIT_DIFF_TOGGLE_CONTEXT},
	        {STATUS_GIT_ICON_REFRESH, "Refresh", "R", EDITOR_ACTION_GIT_REFRESH},
	};
	static const struct statusBarGitButton k_diff_whole[] = {
	        {STATUS_GIT_ICON_COMPRESS, "Show hunks", "z",
	         EDITOR_ACTION_GIT_DIFF_TOGGLE_CONTEXT},
	        {STATUS_GIT_ICON_REFRESH, "Refresh", "R", EDITOR_ACTION_GIT_REFRESH},
	};

	struct statusBarGitButton drawer_buttons[STATUS_BAR_MAX_BUTTONS];
	const struct statusBarGitButton *buttons = NULL;
	int count = 0;
	switch (context) {
		case STATUS_GIT_CONTEXT_DRAWER:
			buttons = drawer_buttons;
			count = statusBarGitDrawerButtons(drawer_buttons, STATUS_BAR_MAX_BUTTONS);
			break;
		case STATUS_GIT_CONTEXT_BRANCHES:
			buttons = k_branches;
			count = (int)(sizeof(k_branches) / sizeof(k_branches[0]));
			break;
		case STATUS_GIT_CONTEXT_LOG:
			buttons = k_log;
			count = (int)(sizeof(k_log) / sizeof(k_log[0]));
			break;
		case STATUS_GIT_CONTEXT_STASH:
			buttons = k_stash;
			count = (int)(sizeof(k_stash) / sizeof(k_stash[0]));
			break;
		case STATUS_GIT_CONTEXT_COMMIT:
			buttons = k_commit;
			count = (int)(sizeof(k_commit) / sizeof(k_commit[0]));
			break;
		case STATUS_GIT_CONTEXT_DIFF:
			buttons = E.git_view_whole_file ? k_diff_whole : k_diff_hunks;
			count = 2;
			break;
		default:
			return 1;
	}

	statusBarButtonsReset(col_offset);
	int col = 0;
	if (col + 1 <= max_col && !wbAppend(wb, " ", 1)) {
		return 0;
	}
	col += (col + 1 <= max_col) ? 1 : 0;
	int nerd = E.nerd_fonts_enabled;
	for (int i = 0; i < count; i++) {
		if (!statusBarAppendButton(wb, &col, max_col, nerd ? buttons[i].icon : NULL, NULL,
		                           buttons[i].label, buttons[i].hotkey,
		                           buttons[i].action)) {
			return 0;
		}
	}
	*col_io = col;
	return 1;
}

/* The focused terminal, when a terminal tab owns the keyboard (not the drawer). */
static struct editorTerminalPane *statusBarFocusedTerminal(void) {
	if (E.primary_focus == EDITOR_PRIMARY_FOCUS_DRAWER) {
		return NULL;
	}
	return editorTerminalPaneForPane(E.focused_leaf);
}

struct statusBarTermButton {
	const char *label;
	const char *hotkey;
	enum editorAction action;
};

/*
 * Renders the terminal action segment: a mode badge (INSERT/NORMAL for Vim, TERM
 * for CUA) then mode-aware buttons. Labels and hotkeys teach the keys (Normal →
 * ^WN, Insert → i, panes → ^Wh/^Wl/...). The buttons dispatch editorActions
 * through the same clickable-span table the git/debug segments use, so they act
 * as a mouse escape hatch a fullscreen child cannot intercept. `col_offset` is
 * the segment's absolute start column; writes consumed cols to *col_io.
 */
static int statusBarAppendTerminalSegment(struct writeBuf *wb, int max_col, int *col_io,
                                          int col_offset) {
	struct editorTerminalPane *terminal = statusBarFocusedTerminal();
	if (terminal == NULL) {
		return 1;
	}
	int is_vim = editorInputSystemActive() == &editorVimInputSystem;
	int normal = is_vim && terminal->input_mode == EDITOR_TERMINAL_INPUT_NORMAL;

	statusBarButtonsReset(col_offset);
	int col = 0;
	if (col + 1 <= max_col && !wbAppend(wb, " ", 1)) {
		return 0;
	}
	col += (col + 1 <= max_col) ? 1 : 0;

	const char *badge = !is_vim ? "TERM" : (normal ? "NORMAL" : "INSERT");
	int blen = (int)strlen(badge);
	if (col + blen <= max_col) {
		if (!wbAppend(wb, VT100_BOLD_ON, (int)strlen(VT100_BOLD_ON)) ||
		    !wbAppend(wb, badge, (size_t)blen) ||
		    !wbAppend(wb, VT100_BOLD_OFF, (int)strlen(VT100_BOLD_OFF))) {
			return 0;
		}
		col += blen;
		if (col + 2 <= max_col && !wbAppend(wb, "  ", 2)) {
			return 0;
		}
		col += (col + 2 <= max_col) ? 2 : 0;
	}

	struct statusBarTermButton buttons[5];
	int count = 0;
	if (normal) {
		buttons[count++] = (struct statusBarTermButton){"Insert", "i",
		                                                EDITOR_ACTION_TERMINAL_MODE_INSERT};
		buttons[count++] =
		        (struct statusBarTermButton){"Left", "^Wh", EDITOR_ACTION_FOCUS_LEFT_PANE};
		buttons[count++] = (struct statusBarTermButton){"Right", "^Wl",
		                                                EDITOR_ACTION_FOCUS_RIGHT_PANE};
		buttons[count++] =
		        (struct statusBarTermButton){"Up", "^Wk", EDITOR_ACTION_FOCUS_UP_PANE};
		buttons[count++] =
		        (struct statusBarTermButton){"Down", "^Wj", EDITOR_ACTION_FOCUS_DOWN_PANE};
	} else if (is_vim) {
		buttons[count++] = (struct statusBarTermButton){"Normal", "^WN",
		                                                EDITOR_ACTION_TERMINAL_MODE_NORMAL};
		buttons[count++] =
		        (struct statusBarTermButton){"Left", "^Wh", EDITOR_ACTION_FOCUS_LEFT_PANE};
		buttons[count++] = (struct statusBarTermButton){"Right", "^Wl",
		                                                EDITOR_ACTION_FOCUS_RIGHT_PANE};
	} else {
		/* CUA: no modes; the buttons are the click-only escape to move panes. */
		buttons[count++] =
		        (struct statusBarTermButton){"Left", NULL, EDITOR_ACTION_FOCUS_LEFT_PANE};
		buttons[count++] =
		        (struct statusBarTermButton){"Right", NULL, EDITOR_ACTION_FOCUS_RIGHT_PANE};
	}

	for (int i = 0; i < count; i++) {
		if (!statusBarAppendButton(wb, &col, max_col, NULL, NULL, buttons[i].label,
		                           buttons[i].hotkey, buttons[i].action)) {
			return 0;
		}
	}
	*col_io = col;
	return 1;
}

int editorStatusBarButtonAt(int col, int *action_out) {
	for (int i = 0; i < g_status_bar_button_count; i++) {
		if (col >= g_status_bar_buttons[i].start_col &&
		    col < g_status_bar_buttons[i].end_col) {
			if (action_out != NULL) {
				*action_out = (int)g_status_bar_buttons[i].action;
			}
			return 1;
		}
	}
	return 0;
}

static int statusBarPrepareInputSegment(char **text_out, int *content_cols_out, int *total_cols_out,
                                        int available_cols) {
	if (text_out != NULL) {
		*text_out = NULL;
	}
	if (content_cols_out != NULL) {
		*content_cols_out = 0;
	}
	if (total_cols_out != NULL) {
		*total_cols_out = 0;
	}
	if (available_cols <= 2) {
		return 1;
	}

	const struct editorInputSystem *system = editorInputSystemActive();
	if (system == NULL || system->status_segment == NULL) {
		return 1;
	}

	char segment[64];
	segment[0] = '\0';
	system->status_segment(segment, sizeof(segment));
	segment[sizeof(segment) - 1] = '\0';
	if (segment[0] == '\0') {
		return 1;
	}

	int sanitized_cols = 0;
	char *sanitized = editorSanitizeTextDup(segment, &sanitized_cols);
	if (sanitized == NULL) {
		return 0;
	}
	if (sanitized_cols <= 0) {
		free(sanitized);
		return 1;
	}

	int content_cols = sanitized_cols;
	if (content_cols > STATUS_INPUT_SEGMENT_MAX_COLS) {
		content_cols = STATUS_INPUT_SEGMENT_MAX_COLS;
	}
	/* Drop the block entirely rather than show a half-truncated mode label. */
	if (content_cols <= 0 || content_cols + 2 > available_cols) {
		free(sanitized);
		return 1;
	}

	if (text_out != NULL) {
		*text_out = sanitized;
	} else {
		free(sanitized);
	}
	if (content_cols_out != NULL) {
		*content_cols_out = content_cols;
	}
	if (total_cols_out != NULL) {
		*total_cols_out = content_cols + 2;
	}
	return 1;
}

int editorDrawStatusBar(struct writeBuf *wb, int scroll_progress_percent) {
	if (!editorAppendThemeStyle(wb, EDITOR_THEME_STYLE_STATUS)) {
		return 0;
	}
	char rightbuf[80];
	char diagbuf[48];
	const char *filename = editorActiveBufferDisplayName();
	const char *dirtyflag = "";
	diagbuf[0] = '\0';
	if (E.dirty) {
		dirtyflag = "[+]";
	}
	if (E.lsp_diagnostic_count > 0) {
		(void)snprintf(diagbuf, sizeof(diagbuf), " [E:%d W:%d]",
		               E.lsp_diagnostic_error_count, E.lsp_diagnostic_warning_count);
	}

	int progress = scroll_progress_percent;
	if (progress < 0) {
		progress = 0;
	} else if (progress > 100) {
		progress = 100;
	}
	int cursor_col = E.rx + 1;
	if (cursor_col < 1) {
		cursor_col = 1;
	}
	const char *git_branch = editorGitBranch();
	int rlen;
	if (git_branch != NULL) {
		char branch_trunc[25];
		(void)snprintf(branch_trunc, sizeof(branch_trunc), "%s", git_branch);
		const char *dirty_marker = E.git_entry_count > 0 ? "+" : "";
		char ahead_behind[32] = "";
		if (E.git_ahead > 0 && E.git_behind > 0) {
			(void)snprintf(ahead_behind, sizeof(ahead_behind), " ↑%d↓%d", E.git_ahead,
			               E.git_behind);
		} else if (E.git_ahead > 0) {
			(void)snprintf(ahead_behind, sizeof(ahead_behind), " ↑%d", E.git_ahead);
		} else if (E.git_behind > 0) {
			(void)snprintf(ahead_behind, sizeof(ahead_behind), " ↓%d", E.git_behind);
		}
		rlen = snprintf(rightbuf, sizeof(rightbuf), " %s%s%s  %d,%d    %d%%", branch_trunc,
		                dirty_marker, ahead_behind, E.cy + 1, cursor_col, progress);
	} else {
		rlen = snprintf(rightbuf, sizeof(rightbuf), "%d,%d    %d%%", E.cy + 1, cursor_col,
		                progress);
	}
	if (rlen < 0) {
		rlen = 0;
		rightbuf[0] = '\0';
	}

	/* Position the right segment by its on-screen width */
	int right_cols = editorDisplayTextCols(rightbuf);
	int right_start_col = E.window_cols - right_cols;
	if (right_start_col < 0) {
		right_start_col = 0;
	}

	/* The input-system badge (e.g. the Vim mode label) is normally the far-left
	 * element of the status bar. It is suppressed while a terminal is focused: the
	 * global editor mode is irrelevant there, and the terminal segment renders its
	 * own per-tab mode badge instead. */
	const struct editorInputSystem *active_system = editorInputSystemActive();
	int terminal_focused = statusBarFocusedTerminal() != NULL;
	char *input_segment = NULL;
	int input_segment_cols = 0;
	int input_segment_total_cols = 0;
	if (!terminal_focused &&
	    !statusBarPrepareInputSegment(&input_segment, &input_segment_cols,
	                                  &input_segment_total_cols, right_start_col)) {
		return 0;
	}

	int ok = 0;

	/* Render the input badge first so it occupies the leftmost columns. */
	if (input_segment_total_cols > 0) {
		int color_idx = -1;
		int colored = 0;
		if (active_system != NULL && active_system->status_segment_color != NULL) {
			color_idx = active_system->status_segment_color();
		}
		colored = color_idx >= 0 && color_idx < EDITOR_THEME_ANSI_COUNT;
		if (colored) {
			struct editorThemeColor bg = E.theme.status_segment_bg[color_idx];
			if (editorThemeColorIsDefault(bg)) {
				bg = editorThemeResolveAnsi((unsigned)color_idx, 0);
			}
			struct editorThemeColor fg = E.theme.styles[EDITOR_THEME_STYLE_STATUS].bg;
			if (editorThemeColorIsDefault(fg)) {
				fg = editorThemeResolveAnsi(EDITOR_THEME_ANSI_BRIGHT_WHITE, 1);
			}
			if (!editorAppendThemeBackground(wb, bg) ||
			    !editorAppendThemeForeground(wb, fg) ||
			    !wbAppend(wb, VT100_BOLD_ON, (int)strlen(VT100_BOLD_ON))) {
				goto cleanup;
			}
		}
		int segment_written = 0;
		if (!wbAppend(wb, " ", 1) ||
		    !editorAppendDisplayPrefix(wb, input_segment, input_segment_cols,
		                               &segment_written)) {
			goto cleanup;
		}
		for (; segment_written < input_segment_cols; segment_written++) {
			if (!wbAppend(wb, " ", 1)) {
				goto cleanup;
			}
		}
		if (!wbAppend(wb, " ", 1)) {
			goto cleanup;
		}
		if (colored && (!wbAppend(wb, VT100_BOLD_OFF, (int)strlen(VT100_BOLD_OFF)) ||
		                !editorAppendThemeStyle(wb, EDITOR_THEME_STYLE_STATUS))) {
			goto cleanup;
		}
	}

	/* The context-sensitive segment of the status bar */
	int debug_cols = 0;
	enum statusBarGitContext git_context = statusBarGitContext();
	int segment_max = right_start_col - input_segment_total_cols;
	if (segment_max < 0) {
		segment_max = 0;
	}
	if (editorDapIsRunning()) {
		if (!statusBarAppendDebugSegment(wb, segment_max, &debug_cols,
		                                 input_segment_total_cols)) {
			goto cleanup;
		}
	} else if (git_context != STATUS_GIT_CONTEXT_NONE) {
		if (!statusBarAppendGitSegment(wb, git_context, segment_max, &debug_cols,
		                               input_segment_total_cols)) {
			goto cleanup;
		}
		filename = "";
	} else if (terminal_focused) {
		if (!statusBarAppendTerminalSegment(wb, segment_max, &debug_cols,
		                                    input_segment_total_cols)) {
			goto cleanup;
		}
		filename = "";
	} else {
		statusBarButtonsReset(0);
	}

	int dirty_cols = (int)strlen(dirtyflag);
	int diag_cols = (int)strlen(diagbuf);
	int left_budget = right_start_col - input_segment_total_cols - debug_cols;
	if (left_budget < 0) {
		left_budget = 0;
	}
	int reserved_for_dirty = 0;
	int include_dirty_sep = 0;
	if (dirty_cols > 0) {
		if (left_budget >= dirty_cols + 1) {
			reserved_for_dirty = dirty_cols + 1;
			include_dirty_sep = 1;
		} else if (left_budget >= dirty_cols) {
			reserved_for_dirty = dirty_cols;
		}
	}

	int path_budget = left_budget - reserved_for_dirty;
	if (path_budget < 0) {
		path_budget = 0;
	}
	if (diag_cols > 0 && path_budget >= diag_cols) {
		path_budget -= diag_cols;
	}

	int left_cols = 0;
	if (!editorAppendSanitizedStatusPath(wb, filename, path_budget, &left_cols)) {
		goto cleanup;
	}
	if (diagbuf[0] != '\0' && left_cols < left_budget) {
		int appended = 0;
		if (!editorAppendSanitizedText(wb, diagbuf, left_budget - left_cols, &appended)) {
			goto cleanup;
		}
		left_cols += appended;
	}

	if (reserved_for_dirty > 0) {
		if (include_dirty_sep && left_cols < left_budget) {
			if (!wbAppend(wb, " ", 1)) {
				goto cleanup;
			}
			left_cols++;
		}

		for (int i = 0; dirtyflag[i] != '\0' && left_cols < left_budget; i++) {
			if (!wbAppend(wb, &dirtyflag[i], 1)) {
				goto cleanup;
			}
			left_cols++;
		}
	}

	for (; left_cols < left_budget; left_cols++) {
		if (!wbAppend(wb, " ", 1)) {
			goto cleanup;
		}
	}

	if (rlen > 0 && !wbAppend(wb, rightbuf, (size_t)rlen)) {
		goto cleanup;
	}
	if (!editorAppendThemeReset(wb)) {
		goto cleanup;
	}
	ok = wbAppend(wb, "\r\n", 2);

cleanup:
	free(input_segment);
	return ok;
}

int editorDrawMessageBar(struct writeBuf *wb) {
	if (!wbAppend(wb, VT100_CLEAR_ROW_3, 3)) {
		return 0;
	}
	if (E.statusmsg[0] != '\0' && time(NULL) - E.statusmsg_time < 5) {
		// Truncate by display columns after escaping, not by raw byte count.
		if (!editorAppendSanitizedText(wb, E.statusmsg, E.window_cols, NULL)) {
			return 0;
		}
	}

	return 1;
}

static int statusBarAppendCursorMove(struct writeBuf *wb, int row, int col) {
	char buf[32];
	int len = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", row, col);
	if (len <= 0 || len >= (int)sizeof(buf)) {
		return 0;
	}
	return wbAppend(wb, buf, (size_t)len);
}

int editorDrawDiagnosticPopdownMessage(struct writeBuf *wb, const char *message,
                                       int cursor_screen_row, int cursor_screen_col,
                                       int *screen_top_out, int *row_count_out) {
	if (message == NULL || message[0] == '\0') {
		return 1;
	}

	int text_start_col = editorTextBodyStartColForCols(E.window_cols);
	int gutter_cols = editorLineNumberGutterColsForCols(E.window_cols);
	int terminal_col_zero = text_start_col + gutter_cols + cursor_screen_col;
	int message_cols = 0;
	char *sanitized = editorSanitizeDiagnosticMessageDup(message, &message_cols);
	if (sanitized == NULL) {
		return 0;
	}
	int available_cols = E.window_cols - text_start_col;
	int cols = message_cols + 2;
	if (cols > available_cols) {
		cols = available_cols;
	}
	if (cols < 4) {
		free(sanitized);
		return 1;
	}
	int content_cols = cols - 2;
	int row_count = editorDisplayWrapLineCount(sanitized, content_cols);
	if (row_count <= 0) {
		free(sanitized);
		return 1;
	}

	int rows_below = E.window_rows - (cursor_screen_row + 1);
	int rows_above = cursor_screen_row;
	int popdown_screen_row = -1;
	int visible_rows = row_count;
	if (row_count <= rows_below) {
		popdown_screen_row = cursor_screen_row + 1;
	} else if (row_count <= rows_above) {
		popdown_screen_row = cursor_screen_row - row_count;
	} else if (rows_below >= rows_above && rows_below > 0) {
		popdown_screen_row = cursor_screen_row + 1;
		visible_rows = rows_below;
	} else if (rows_above > 0) {
		popdown_screen_row = cursor_screen_row - rows_above;
		visible_rows = rows_above;
	} else {
		free(sanitized);
		return 1;
	}

	if (terminal_col_zero + cols > E.window_cols) {
		terminal_col_zero = E.window_cols - cols;
	}
	if (terminal_col_zero < text_start_col) {
		terminal_col_zero = text_start_col;
	}

	int terminal_col = terminal_col_zero + 1;
	int text_len = (int)strlen(sanitized);
	int start_idx = 0;
	int rows_drawn = 0;
	for (; rows_drawn < visible_rows && start_idx < text_len; rows_drawn++) {
		int terminal_row = popdown_screen_row + rows_drawn + 2;
		int end_idx = start_idx;
		int wrote = 0;
		editorDisplayWrapNextLine(sanitized, text_len, start_idx, content_cols, &end_idx,
		                          &wrote);
		if (end_idx <= start_idx) {
			break;
		}
		if (!statusBarAppendCursorMove(wb, terminal_row, terminal_col) ||
		    !editorAppendThemeStyle(wb, EDITOR_THEME_STYLE_STATUS) ||
		    !wbAppend(wb, " ", 1) ||
		    !wbAppend(wb, &sanitized[start_idx], (size_t)(end_idx - start_idx))) {
			free(sanitized);
			return 0;
		}
		int padding = cols - 1 - wrote;
		while (padding > 0) {
			if (!wbAppend(wb, " ", 1)) {
				free(sanitized);
				return 0;
			}
			padding--;
		}
		if (!editorAppendThemeReset(wb)) {
			free(sanitized);
			return 0;
		}
		start_idx = end_idx;
	}

	free(sanitized);
	if (screen_top_out != NULL) {
		*screen_top_out = popdown_screen_row + 2;
	}
	if (row_count_out != NULL) {
		*row_count_out = rows_drawn;
	}
	return 1;
}
