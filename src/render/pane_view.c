#include "render/pane_view.h"

#include "config/theme_config.h"
#include "debug/dap_console.h"
#include "debug/dap_output.h"
#include "input/text_pairs.h"
#include "language/syntax.h"
#include "language/syntax_visible_cache.h"
#include "render/ansi_style.h"
#include "render/drawer_view.h"
#include "render/screen.h"
#include "render/tab_bar.h"
#include "render/terminal_view.h"
#include "render/viewport.h"
#include "render/write_buf.h"
#include "rotide.h"
#include "terminal/terminal_pane.h"
#include "workspace/drawer.h"
#include "workspace/git_view.h"
#include "workspace/layout.h"
#include "workspace/tabs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VT100_CLEAR_ROW_3 "\x1b[K"
/* "│" U+2502 BOX DRAWINGS LIGHT VERTICAL (UTF-8: e2 94 82) */
#define EDITOR_PANE_VBORDER "\xe2\x94\x82"
/* "─" U+2500 BOX DRAWINGS LIGHT HORIZONTAL (UTF-8: e2 94 80) */
#define EDITOR_PANE_HBORDER "\xe2\x94\x80"

int editorAppendCursorMove(struct writeBuf *wb, int row, int col);
extern int g_screen_drawing_current_line_highlight;
extern int g_screen_drawing_stopped_line_highlight;
extern int g_screen_drawing_git_row_bg_active;
extern struct editorThemeColor g_screen_drawing_git_row_bg;
extern int g_screen_drawing_focused_editor_pane;
extern int g_screen_drawing_file_row_origin_col;
extern int g_screen_drawing_file_row_screen_y;

struct paneViewSyntaxRowOverride {
	int valid;
	int row_idx;
	int span_count;
	struct editorRowSyntaxSpan spans[ROTIDE_MAX_SYNTAX_SPANS_PER_ROW];
};

struct paneViewSyntaxFrameEntry {
	const struct editorPaneNode *leaf;
	int body_rows;
	struct paneViewSyntaxRowOverride *rows;
	int bracket_match_active;
	int bracket_match_rows[2];
	int bracket_match_cols[2];
};

struct paneViewSyntaxFrame {
	struct paneViewSyntaxFrameEntry *entries;
	int count;
};

static struct paneViewSyntaxFrame g_pane_view_syntax_frame = {0};
static const struct paneViewSyntaxRowOverride *g_pane_view_active_row_syntax_override = NULL;
static const struct paneViewSyntaxFrameEntry *g_pane_view_active_bracket_entry = NULL;
static int g_pane_view_wrap_body_cols_override = 0;

/*
 * The single-pane render path does not build a per-pane syntax frame, so it
 * stashes the focused buffer's matching-bracket pair here for the duration of
 * one frame. The multi-pane path leaves this inactive and relies on the
 * per-pane frame entry instead.
 */
static int g_pane_view_single_bracket_active = 0;
static int g_pane_view_single_bracket_rows[2];
static int g_pane_view_single_bracket_cols[2];

void editorPaneSingleBracketMatchUpdate(void) {
	g_pane_view_single_bracket_active = editorBracketMatchComputeForCursor(
	        g_pane_view_single_bracket_rows, g_pane_view_single_bracket_cols);
}

void editorPaneSingleBracketMatchClear(void) {
	g_pane_view_single_bracket_active = 0;
}

int editorPaneWrapBodyColsOverride(void) {
	return g_pane_view_wrap_body_cols_override;
}

int editorPaneSyntaxRowOverrideCopy(int row_idx, struct editorRowSyntaxSpan *spans, int max_spans,
                                    int *span_count_out) {
	if (span_count_out != NULL) {
		*span_count_out = 0;
	}
	if (spans == NULL || max_spans <= 0 || span_count_out == NULL ||
	    g_pane_view_active_row_syntax_override == NULL ||
	    !g_pane_view_active_row_syntax_override->valid ||
	    g_pane_view_active_row_syntax_override->row_idx != row_idx) {
		return 0;
	}

	int span_count = g_pane_view_active_row_syntax_override->span_count;
	if (span_count < 0) {
		span_count = 0;
	}
	if (span_count > max_spans) {
		span_count = max_spans;
	}
	if (span_count > 0) {
		size_t copy_bytes = sizeof(*spans) * (size_t)span_count;
		memcpy(spans, g_pane_view_active_row_syntax_override->spans, copy_bytes);
	}
	*span_count_out = span_count;
	return 1;
}

int editorPaneActiveBracketColsForRow(int row_idx, int out_cols[2], int *count_out) {
	if (count_out != NULL) {
		*count_out = 0;
	}
	if (out_cols == NULL || count_out == NULL) {
		return 0;
	}

	const int *match_rows = NULL;
	const int *match_cols = NULL;
	if (g_pane_view_active_bracket_entry != NULL &&
	    g_pane_view_active_bracket_entry->bracket_match_active) {
		match_rows = g_pane_view_active_bracket_entry->bracket_match_rows;
		match_cols = g_pane_view_active_bracket_entry->bracket_match_cols;
	} else if (g_pane_view_single_bracket_active) {
		match_rows = g_pane_view_single_bracket_rows;
		match_cols = g_pane_view_single_bracket_cols;
	} else {
		return 0;
	}

	int count = 0;
	for (int i = 0; i < 2; i++) {
		if (match_rows[i] == row_idx) {
			out_cols[count++] = match_cols[i];
		}
	}
	*count_out = count;
	return count > 0;
}

static int paneViewTextBodyViewportColsForWidth(int pane_cols) {
	int gutter_cols = editorLineNumberGutterColsForCols(E.window_cols);
	if (gutter_cols > pane_cols) {
		gutter_cols = pane_cols;
	}
	int text_cols = pane_cols - gutter_cols;
	if (text_cols < 1) {
		text_cols = 1;
	}
	if (text_cols >= 3) {
		return text_cols - 2;
	}
	return text_cols;
}

static int paneViewBodyRowToBufferRow(int body_row_in_pane, int slice_cols, int *row_idx_out) {
	if (row_idx_out != NULL) {
		*row_idx_out = E.numrows;
	}
	if (body_row_in_pane < 0 || slice_cols <= 0) {
		return 0;
	}

	int saved_wrap_body_cols_override = g_pane_view_wrap_body_cols_override;
	g_pane_view_wrap_body_cols_override = paneViewTextBodyViewportColsForWidth(slice_cols);

	int row_idx = body_row_in_pane + E.rowoff;
	int segment_coloff = 0;
	if (E.line_wrap_enabled) {
		if (!editorViewportTextScreenRowToBufferRow(body_row_in_pane, &row_idx,
		                                            &segment_coloff)) {
			row_idx = E.numrows;
		}
	}
	g_pane_view_wrap_body_cols_override = saved_wrap_body_cols_override;
	(void)segment_coloff;
	if (row_idx_out != NULL) {
		*row_idx_out = row_idx;
	}
	return 1;
}

void editorPaneSyntaxFrameClear(void) {
	if (g_pane_view_syntax_frame.entries != NULL) {
		for (int i = 0; i < g_pane_view_syntax_frame.count; i++) {
			free(g_pane_view_syntax_frame.entries[i].rows);
			g_pane_view_syntax_frame.entries[i].rows = NULL;
		}
	}
	free(g_pane_view_syntax_frame.entries);
	g_pane_view_syntax_frame.entries = NULL;
	g_pane_view_syntax_frame.count = 0;
	g_pane_view_active_row_syntax_override = NULL;
	g_pane_view_active_bracket_entry = NULL;
}

static int paneViewSyntaxFrameBuildEntry(struct paneViewSyntaxFrameEntry *entry, int pane_cols) {
	if (entry == NULL || entry->body_rows <= 0 || pane_cols <= 0) {
		return 1;
	}
	entry->rows = calloc((size_t)entry->body_rows, sizeof(*entry->rows));
	if (entry->rows == NULL) {
		return 0;
	}

	int have_visible = 0;
	int first_row = 0;
	int end_row_exclusive = 0;
	for (int body_row = 0; body_row < entry->body_rows; body_row++) {
		int row_idx = E.numrows;
		if (!paneViewBodyRowToBufferRow(body_row, pane_cols, &row_idx)) {
			return 0;
		}
		if (row_idx < 0 || row_idx >= E.numrows) {
			continue;
		}
		entry->rows[body_row].valid = 1;
		entry->rows[body_row].row_idx = row_idx;
		if (!have_visible) {
			first_row = row_idx;
			end_row_exclusive = row_idx + 1;
			have_visible = 1;
		} else {
			if (row_idx < first_row) {
				first_row = row_idx;
			}
			if (row_idx + 1 > end_row_exclusive) {
				end_row_exclusive = row_idx + 1;
			}
		}
	}
	if (!have_visible) {
		return 1;
	}
	if (!editorSyntaxPrepareVisibleRowSpansForeground(first_row,
	                                                  end_row_exclusive - first_row)) {
		return 0;
	}
	for (int body_row = 0; body_row < entry->body_rows; body_row++) {
		if (!entry->rows[body_row].valid) {
			continue;
		}
		int span_count = 0;
		if (!editorSyntaxRowRenderSpans(entry->rows[body_row].row_idx,
		                                entry->rows[body_row].spans,
		                                ROTIDE_MAX_SYNTAX_SPANS_PER_ROW, &span_count)) {
			span_count = 0;
		}
		if (span_count < 0) {
			span_count = 0;
		}
		if (span_count > ROTIDE_MAX_SYNTAX_SPANS_PER_ROW) {
			span_count = ROTIDE_MAX_SYNTAX_SPANS_PER_ROW;
		}
		entry->rows[body_row].span_count = span_count;
	}
	return 1;
}

int editorPaneSyntaxFrameBuild(const struct editorLeafLayout *layout) {
	editorPaneSyntaxFrameClear();
	if (layout == NULL || layout->count <= 0) {
		return 1;
	}

	struct paneViewSyntaxFrameEntry *entries = calloc((size_t)layout->count, sizeof(*entries));
	if (entries == NULL) {
		return 0;
	}
	g_pane_view_syntax_frame.entries = entries;
	g_pane_view_syntax_frame.count = layout->count;

	for (int i = 0; i < layout->count; i++) {
		struct editorPaneNode *leaf = layout->rects[i].node;
		struct paneViewSyntaxFrameEntry *entry = &g_pane_view_syntax_frame.entries[i];
		entry->leaf = leaf;
		entry->body_rows = layout->rects[i].rect.h;
		if (leaf == NULL || leaf->is_split ||
		    leaf->as.leaf.kind != EDITOR_PANE_KIND_EDITOR || entry->body_rows <= 0 ||
		    layout->rects[i].rect.w <= 0) {
			continue;
		}

		int tab_idx =
		        leaf == E.focused_leaf ? E.active_tab : leaf->as.leaf.view.active_tab_idx;
		if (tab_idx < 0 || E.tabs == NULL || tab_idx >= E.tab_count) {
			continue;
		}

		struct editorViewSnapshot view_snap;
		editorViewSnapshotCapture(&view_snap);
		struct editorBuffer buffer_snap = {0};
		int active_tab = E.active_tab;
		editorBufferAliasSnapshot(&buffer_snap);

		int aliased_non_active_tab = tab_idx != active_tab;
		if (aliased_non_active_tab) {
			const struct editorBuffer *tab_buffer = editorTabBufferHandleAt(tab_idx);
			if (tab_buffer == NULL) {
				editorViewSnapshotRestore(&view_snap);
				continue;
			}
			E.active_tab = tab_idx;
			editorBufferAliasToActive(tab_buffer);
		}
		if (leaf != E.focused_leaf) {
			editorViewSnapshotFromPaneView(&leaf->as.leaf.view);
		}
		int ok = paneViewSyntaxFrameBuildEntry(entry, layout->rects[i].rect.w);
		entry->bracket_match_active = editorBracketMatchComputeForCursor(
		        entry->bracket_match_rows, entry->bracket_match_cols);

		editorBufferAliasToActive(&buffer_snap);
		E.active_tab = active_tab;
		editorViewSnapshotRestore(&view_snap);
		if (!ok) {
			editorPaneSyntaxFrameClear();
			return 0;
		}
	}
	return 1;
}

int editorDrawFocusedPaneSlice(struct writeBuf *wb, const struct editorPaneNode *leaf,
                               int body_row_in_pane, int slice_cols) {
	int gutter_cols = editorLineNumberGutterColsForCols(E.window_cols);
	if (gutter_cols > slice_cols) {
		gutter_cols = slice_cols;
	}
	int file_cols = slice_cols - gutter_cols;
	if (file_cols < 0) {
		file_cols = 0;
	}

	int saved_wrap_body_cols_override = g_pane_view_wrap_body_cols_override;
	const struct paneViewSyntaxRowOverride *saved_row_syntax_override =
	        g_pane_view_active_row_syntax_override;
	const struct paneViewSyntaxFrameEntry *saved_bracket_entry =
	        g_pane_view_active_bracket_entry;
	const struct paneViewSyntaxRowOverride *row_syntax_override = NULL;
	const struct paneViewSyntaxFrameEntry *bracket_entry = NULL;
	if (body_row_in_pane >= 0 && g_pane_view_syntax_frame.entries != NULL) {
		for (int i = 0; i < g_pane_view_syntax_frame.count; i++) {
			struct paneViewSyntaxFrameEntry *entry =
			        &g_pane_view_syntax_frame.entries[i];
			if (entry->leaf != leaf) {
				continue;
			}
			bracket_entry = entry;
			if (entry->rows != NULL && body_row_in_pane < entry->body_rows) {
				row_syntax_override = &entry->rows[body_row_in_pane];
			}
			break;
		}
	}
	g_pane_view_active_row_syntax_override = row_syntax_override;
	g_pane_view_active_bracket_entry = bracket_entry;
	g_pane_view_wrap_body_cols_override = paneViewTextBodyViewportColsForWidth(slice_cols);

	int y_offset = body_row_in_pane + E.rowoff;
	int segment_coloff = 0;
	if (E.line_wrap_enabled) {
		if (!editorViewportTextScreenRowToBufferRow(body_row_in_pane, &y_offset,
		                                            &segment_coloff)) {
			y_offset = E.numrows;
			segment_coloff = 0;
		}
	}

	int stopped_row = y_offset < E.numrows && editorDebugStoppedLineHighlightApplies(y_offset);
	struct editorThemeColor git_row_bg;
	int git_row = !stopped_row && y_offset < E.numrows &&
	              editorGitViewRowBgColor(y_offset, &git_row_bg);
	int highlight_row = !stopped_row && !git_row && y_offset < E.numrows &&
	                    editorCurrentLineHighlightApplies(y_offset, segment_coloff);
	if (stopped_row &&
	    !editorAppendThemeBackgroundRole(wb, EDITOR_THEME_UI_DEBUG_STOPPED_LINE_BG)) {
		goto fail;
	}
	if (git_row && !editorAppendThemeBackground(wb, git_row_bg)) {
		goto fail;
	}
	if (highlight_row &&
	    !editorAppendThemeBackgroundRole(wb, EDITOR_THEME_UI_CURRENT_LINE_BG)) {
		goto fail;
	}
	g_screen_drawing_current_line_highlight = highlight_row;
	g_screen_drawing_stopped_line_highlight = stopped_row;
	g_screen_drawing_git_row_bg_active = git_row;
	if (git_row) {
		g_screen_drawing_git_row_bg = git_row_bg;
	}
	g_screen_drawing_focused_editor_pane = leaf == E.focused_leaf;
	if (g_screen_drawing_file_row_origin_col >= 0) {
		g_screen_drawing_file_row_origin_col += gutter_cols;
	}
	if (!editorDrawLineNumberGutter(wb, y_offset, segment_coloff, gutter_cols)) {
		goto fail;
	}
	if (file_cols > 0) {
		if (y_offset < E.numrows) {
			if (E.line_wrap_enabled) {
				if (!editorDrawFileRowWrapped(wb, (size_t)y_offset, file_cols,
				                              segment_coloff)) {
					goto fail;
				}
			} else if (!editorDrawFileRow(wb, (size_t)y_offset, file_cols)) {
				goto fail;
			}
		} else {
			if (!editorAppendGrayBytes(wb, "~", 1)) {
				goto fail;
			}
			for (int pad = 1; pad < file_cols; pad++) {
				if (!wbAppend(wb, " ", 1)) {
					goto fail;
				}
			}
		}
	}
	g_screen_drawing_current_line_highlight = 0;
	g_screen_drawing_stopped_line_highlight = 0;
	g_screen_drawing_git_row_bg_active = 0;
	g_screen_drawing_focused_editor_pane = 0;
	g_screen_drawing_file_row_origin_col = -1;
	g_screen_drawing_file_row_screen_y = -1;
	if ((highlight_row || stopped_row || git_row) && !editorAppendThemeReset(wb)) {
		goto fail_reset;
	}
	g_pane_view_wrap_body_cols_override = saved_wrap_body_cols_override;
	g_pane_view_active_row_syntax_override = saved_row_syntax_override;
	g_pane_view_active_bracket_entry = saved_bracket_entry;
	return 1;

fail:
	g_screen_drawing_current_line_highlight = 0;
	g_screen_drawing_stopped_line_highlight = 0;
	g_screen_drawing_git_row_bg_active = 0;
	g_screen_drawing_focused_editor_pane = 0;
	g_screen_drawing_file_row_origin_col = -1;
	g_screen_drawing_file_row_screen_y = -1;
fail_reset:
	g_pane_view_wrap_body_cols_override = saved_wrap_body_cols_override;
	g_pane_view_active_row_syntax_override = saved_row_syntax_override;
	g_pane_view_active_bracket_entry = saved_bracket_entry;
	return 0;
}

void editorViewSnapshotCapture(struct editorViewSnapshot *snap) {
	snap->cx = E.cx;
	snap->cy = E.cy;
	snap->rx = E.rx;
	snap->rowoff = E.rowoff;
	snap->coloff = E.coloff;
	snap->wrapoff = E.wrapoff;
	snap->cursor_offset = E.cursor_offset;
	snap->viewport_mode = (int)E.viewport_mode;
	snap->selection_mode_active = E.selection_mode_active;
	snap->selection_anchor_offset = E.selection_anchor_offset;
	snap->column_select_active = E.column_select_active;
	snap->column_select_anchor_cy = E.column_select_anchor_cy;
	snap->column_select_anchor_rx = E.column_select_anchor_rx;
	snap->column_select_cursor_rx = E.column_select_cursor_rx;
}

void editorViewSnapshotRestore(const struct editorViewSnapshot *snap) {
	E.cx = snap->cx;
	E.cy = snap->cy;
	E.rx = snap->rx;
	E.rowoff = snap->rowoff;
	E.coloff = snap->coloff;
	E.wrapoff = snap->wrapoff;
	E.cursor_offset = snap->cursor_offset;
	E.viewport_mode = (enum editorViewportMode)snap->viewport_mode;
	E.selection_mode_active = snap->selection_mode_active;
	E.selection_anchor_offset = snap->selection_anchor_offset;
	E.column_select_active = snap->column_select_active;
	E.column_select_anchor_cy = snap->column_select_anchor_cy;
	E.column_select_anchor_rx = snap->column_select_anchor_rx;
	E.column_select_cursor_rx = snap->column_select_cursor_rx;
}

void editorViewSnapshotFromPaneView(const struct editorPaneView *view) {
	E.cx = view->cx;
	E.cy = view->cy;
	E.rx = view->rx;
	E.rowoff = view->rowoff;
	E.coloff = view->coloff;
	E.wrapoff = view->wrapoff;
	E.cursor_offset = view->cursor_offset;
	E.viewport_mode = (enum editorViewportMode)view->viewport_mode;
	E.selection_mode_active = view->selection_mode_active;
	E.selection_anchor_offset = view->selection_anchor_offset;
	E.column_select_active = view->column_select_active;
	E.column_select_anchor_cy = view->column_select_anchor_cy;
	E.column_select_anchor_rx = view->column_select_anchor_rx;
	E.column_select_cursor_rx = view->column_select_cursor_rx;
}

int editorDrawBlankCells(struct writeBuf *wb, int cells) {
	for (int i = 0; i < cells; i++) {
		if (!wbAppend(wb, " ", 1)) {
			return 0;
		}
	}
	return 1;
}

int editorDrawPaneViewSlice(struct writeBuf *wb, const struct editorPaneNode *leaf,
                            const struct editorPaneView *view, int body_row_in_pane,
                            int slice_cols) {
	if (view == NULL || view->active_tab_idx < 0 ||
	    (E.tab_count > 0 && view->active_tab_idx >= E.tab_count)) {
		return editorDrawBlankCells(wb, slice_cols);
	}

	if (view->active_tab_idx == E.active_tab) {
		struct editorViewSnapshot snap;
		editorViewSnapshotCapture(&snap);
		editorViewSnapshotFromPaneView(view);
		int ok = editorDrawFocusedPaneSlice(wb, leaf, body_row_in_pane, slice_cols);
		editorViewSnapshotRestore(&snap);
		return ok;
	}

	if (E.tabs == NULL || view->active_tab_idx >= E.tab_count) {
		return editorDrawBlankCells(wb, slice_cols);
	}

	struct editorViewSnapshot snap;
	editorViewSnapshotCapture(&snap);
	struct editorBuffer active_snap = {0};
	int active_tab = E.active_tab;
	editorBufferAliasSnapshot(&active_snap);

	const struct editorBuffer *tab_buffer = editorTabBufferHandleAt(view->active_tab_idx);
	if (tab_buffer == NULL) {
		editorViewSnapshotRestore(&snap);
		return editorDrawBlankCells(wb, slice_cols);
	}
	E.active_tab = view->active_tab_idx;
	editorBufferAliasToActive(tab_buffer);
	editorViewSnapshotFromPaneView(view);
	int ok = editorDrawFocusedPaneSlice(wb, leaf, body_row_in_pane, slice_cols);

	editorBufferAliasToActive(&active_snap);
	E.active_tab = active_tab;
	editorViewSnapshotRestore(&snap);
	return ok;
}

static int paneViewDrawGreeting(struct writeBuf *wb, int cols) {
	char greet[80];
	int greetlen = snprintf(greet, sizeof(greet), "RotIDE editor - version %s", ROTIDE_VERSION);
	if (greetlen > cols) {
		greetlen = cols;
	}
	int pad = (cols - greetlen) / 2;
	if (pad) {
		if (!editorAppendGrayBytes(wb, "~", 1)) {
			return 0;
		}
		pad--;
	}
	while (pad--) {
		if (!wbAppend(wb, " ", 1)) {
			return 0;
		}
	}
	return wbAppend(wb, greet, greetlen);
}

int editorBuildSinglePaneRowLine(struct writeBuf *wb, int y, int drawer_cols, int separator_cols,
                                 int text_cols) {
	int y_offset = y + E.rowoff;
	int segment_coloff = 0;
	if (E.line_wrap_enabled) {
		if (!editorViewportTextScreenRowToBufferRow(y, &y_offset, &segment_coloff)) {
			y_offset = E.numrows;
			segment_coloff = 0;
		}
	}

	if (!editorDrawDrawerRow(wb, y + 1, drawer_cols)) {
		return 0;
	}

	if (!editorDrawDrawerSeparatorCell(wb, separator_cols)) {
		return 0;
	}

	int gutter_cols = editorLineNumberGutterColsForCols(E.window_cols);
	int file_cols = text_cols - gutter_cols;
	if (file_cols < 1) {
		file_cols = 1;
	}
	int stopped_row = y_offset < E.numrows && editorDebugStoppedLineHighlightApplies(y_offset);
	struct editorThemeColor git_row_bg;
	int git_row = !stopped_row && y_offset < E.numrows &&
	              editorGitViewRowBgColor(y_offset, &git_row_bg);
	int highlight_row = !stopped_row && !git_row && y_offset < E.numrows &&
	                    editorCurrentLineHighlightApplies(y_offset, segment_coloff);
	if (stopped_row &&
	    !editorAppendThemeBackgroundRole(wb, EDITOR_THEME_UI_DEBUG_STOPPED_LINE_BG)) {
		return 0;
	}
	if (git_row && !editorAppendThemeBackground(wb, git_row_bg)) {
		return 0;
	}
	if (highlight_row &&
	    !editorAppendThemeBackgroundRole(wb, EDITOR_THEME_UI_CURRENT_LINE_BG)) {
		return 0;
	}
	g_screen_drawing_current_line_highlight = highlight_row;
	g_screen_drawing_stopped_line_highlight = stopped_row;
	g_screen_drawing_git_row_bg_active = git_row;
	if (git_row) {
		g_screen_drawing_git_row_bg = git_row_bg;
	}
	g_screen_drawing_focused_editor_pane = 1;
	g_screen_drawing_file_row_origin_col = drawer_cols + separator_cols + gutter_cols;
	g_screen_drawing_file_row_screen_y = y + 1;
	if (!editorDrawLineNumberGutter(wb, y_offset, segment_coloff, gutter_cols)) {
		goto clear_highlight;
	}
	if (y_offset < E.numrows) {
		if (E.line_wrap_enabled) {
			if (!editorDrawFileRowWrapped(wb, (size_t)y_offset, file_cols,
			                              segment_coloff)) {
				goto clear_highlight;
			}
		} else if (!editorDrawFileRow(wb, (size_t)y_offset, file_cols)) {
			goto clear_highlight;
		}
	} else if (E.numrows == 0 && y == E.window_rows / 3) {
		if (!paneViewDrawGreeting(wb, file_cols)) {
			goto clear_highlight;
		}
	} else if (!editorAppendGrayBytes(wb, "~", 1)) {
		goto clear_highlight;
	}
	g_screen_drawing_current_line_highlight = 0;
	g_screen_drawing_stopped_line_highlight = 0;
	g_screen_drawing_git_row_bg_active = 0;
	g_screen_drawing_focused_editor_pane = 0;
	g_screen_drawing_file_row_origin_col = -1;
	g_screen_drawing_file_row_screen_y = -1;
	if ((highlight_row || stopped_row || git_row) && !editorAppendThemeReset(wb)) {
		return 0;
	}

	if (!wbAppend(wb, VT100_CLEAR_ROW_3, 3)) {
		return 0;
	}
	if (!editorDrawDrawerSelectionOverflow(wb, y + 1, drawer_cols, separator_cols, text_cols,
	                                       y + 2, NULL)) {
		return 0;
	}

	return 1;

clear_highlight:
	g_screen_drawing_current_line_highlight = 0;
	g_screen_drawing_stopped_line_highlight = 0;
	g_screen_drawing_git_row_bg_active = 0;
	g_screen_drawing_focused_editor_pane = 0;
	g_screen_drawing_file_row_origin_col = -1;
	g_screen_drawing_file_row_screen_y = -1;
	return 0;
}

static int paneViewLeafAt(const struct editorLeafLayout *layout, int x, int y,
                          struct editorRect *out_rect) {
	for (int i = 0; i < layout->count; i++) {
		struct editorRect r = layout->rects[i].rect;
		if (r.x <= x && x < r.x + r.w && r.y <= y && y < r.y + r.h) {
			if (out_rect != NULL) {
				*out_rect = r;
			}
			return i;
		}
	}
	return -1;
}

enum paneViewBorderCellKind {
	PANE_VIEW_BORDER_CELL_NONE = 0,
	PANE_VIEW_BORDER_CELL_VERTICAL,
	PANE_VIEW_BORDER_CELL_HORIZONTAL
};

static enum paneViewBorderCellKind paneViewBorderCellAt(int x, int screen_y,
                                                        const struct editorBorderList *borders) {
	if (borders == NULL) {
		return PANE_VIEW_BORDER_CELL_NONE;
	}
	for (int i = 0; i < borders->count; i++) {
		struct editorRect r = borders->rects[i].rect;
		if (x < r.x || x >= r.x + r.w) {
			continue;
		}
		int in_row = screen_y >= r.y && screen_y < r.y + r.h;
		if (!in_row && borders->rects[i].orientation == EDITOR_SPLIT_VERTICAL &&
		    screen_y == r.y - 1) {
			in_row = 1;
		}
		if (!in_row) {
			continue;
		}
		return borders->rects[i].orientation == EDITOR_SPLIT_HORIZONTAL
		               ? PANE_VIEW_BORDER_CELL_HORIZONTAL
		               : PANE_VIEW_BORDER_CELL_VERTICAL;
	}
	return PANE_VIEW_BORDER_CELL_NONE;
}

static int paneViewDrawStripSpan(struct writeBuf *wb, struct editorPaneNode *leaf, int local_col,
                                 int strip_cols, int slice_cols, int trailing_hborder) {
	struct writeBuf strip = WRITEBUF_INIT;
	int ok = 0;
	if (slice_cols <= 0) {
		return 1;
	}
	if (local_col == 0 && slice_cols == strip_cols) {
		return editorDrawPaneTabStrip(wb, leaf, strip_cols, trailing_hborder);
	}
	if (!editorDrawPaneTabStrip(&strip, leaf, strip_cols, trailing_hborder)) {
		goto cleanup;
	}
	if ((size_t)local_col > strip.len) {
		if (!editorDrawBlankCells(wb, slice_cols)) {
			goto cleanup;
		}
		ok = 1;
		goto cleanup;
	}
	size_t available = strip.len - (size_t)local_col;
	size_t take = (size_t)slice_cols < available ? (size_t)slice_cols : available;
	if (take > 0 && !wbAppend(wb, strip.b + local_col, take)) {
		goto cleanup;
	}
	for (int i = (int)take; i < slice_cols; i++) {
		if (!wbAppend(wb, " ", 1)) {
			goto cleanup;
		}
	}
	ok = 1;
cleanup:
	wbFree(&strip);
	return ok;
}

int editorDrawMultiPaneTabStripRow(struct writeBuf *wb) {
	struct editorRect viewport;
	if (!editorLayoutEditorViewport(&viewport)) {
		return 0;
	}
	struct editorLeafLayout layout = {0};
	struct editorBorderList borders = {0};
	if (!editorLayoutComputeBorderedInto(E.layout_root, viewport, ROTIDE_PANE_BORDER_SIZE,
	                                     &layout) ||
	    !editorLayoutCollectBorders(E.layout_root, viewport, ROTIDE_PANE_BORDER_SIZE,
	                                &borders)) {
		editorBorderListFree(&borders);
		editorLeafLayoutFree(&layout);
		return 0;
	}

	int ok = 0;
	int x = viewport.x;
	while (x < viewport.x + viewport.w) {
		enum paneViewBorderCellKind border = paneViewBorderCellAt(x, 0, &borders);
		if (border == PANE_VIEW_BORDER_CELL_VERTICAL) {
			if (!wbAppend(wb, EDITOR_PANE_VBORDER, sizeof(EDITOR_PANE_VBORDER) - 1)) {
				goto cleanup;
			}
			x++;
			continue;
		}
		struct editorPaneNode *leaf = NULL;
		int local_col = 0;
		int strip_cols = 0;
		if (editorLayoutPaneTabStripAt(&layout, x, 0, &leaf, &local_col, &strip_cols)) {
			int slice_cols = strip_cols - local_col;
			if (!paneViewDrawStripSpan(wb, leaf, local_col, strip_cols, slice_cols,
			                           0)) {
				goto cleanup;
			}
			x += slice_cols;
			continue;
		}
		if (!wbAppend(wb, " ", 1)) {
			goto cleanup;
		}
		x++;
	}
	ok = 1;
cleanup:
	editorBorderListFree(&borders);
	editorLeafLayoutFree(&layout);
	return ok;
}

/* Emits display columns [col, col+slice_cols) of `text` (1 byte/col,
 * non-printable -> '?'), padding short lines with spaces. */
static int paneViewDapConsoleEmitSlice(struct writeBuf *wb, const char *text, int text_len, int col,
                                       int slice_cols) {
	for (int i = 0; i < slice_cols; i++) {
		int c = col + i;
		char ch = ' ';
		if (text != NULL && c >= 0 && c < text_len) {
			unsigned char b = (unsigned char)text[c];
			ch = (b >= 0x20 && b < 0x7f) ? (char)b : '?';
		}
		if (!wbAppend(wb, &ch, 1)) {
			return 0;
		}
	}
	return 1;
}

/* Renders one horizontal slice of the Debug Console tab body: the inline REPL
 * input occupies the last row, the dap_output transcript fills the rows above.
 * The pane's Terminal / Debug Console tab strip is drawn by the normal
 * separator-row renderer, not here. Emits exactly slice_cols columns. */
static int paneViewDrawDapConsoleSlice(struct writeBuf *wb, struct editorPaneNode *leaf,
                                       int row_in_pane, int col_in_pane, int slice_cols,
                                       const struct editorRect *rect) {
	if (slice_cols <= 0) {
		return 1;
	}
	struct editorDapConsolePane *console = editorDapConsoleForPane(leaf);
	if (!editorAppendThemeReset(wb)) {
		return 0;
	}
	int input_row = rect->h - 1;
	if (row_in_pane == input_row && input_row >= 0) {
		/* The caret is drawn by the hardware cursor (placed on this line when the
		 * console is focused), so the input line is just plain text. */
		const char *input_text = console != NULL ? console->input : "";
		char inbuf[(int)sizeof(console->input) + 4];
		int ilen = snprintf(inbuf, sizeof(inbuf), "> %s", input_text);
		if (ilen < 0) {
			ilen = 0;
		}
		return paneViewDapConsoleEmitSlice(wb, inbuf, ilen, col_in_pane, slice_cols);
	}

	int body_rows = rect->h - 1; /* transcript rows above the input line */
	if (body_rows < 1) {
		body_rows = 1;
	}
	int total = editorDapOutputLineCount();
	int scroll = console != NULL ? console->scroll : 0;
	int max_scroll = total - body_rows < 0 ? 0 : total - body_rows;
	if (scroll > max_scroll) {
		scroll = max_scroll;
	}
	if (scroll < 0) {
		scroll = 0;
	}
	const char *line = NULL;
	int len = 0;
	if (total == 0) {
		if (row_in_pane == 0) {
			line = "(no debug output)";
			len = (int)strlen(line);
		}
	} else {
		(void)editorDapOutputLine(total - body_rows + row_in_pane - scroll, &line, &len);
	}
	return paneViewDapConsoleEmitSlice(wb, line, len, col_in_pane, slice_cols);
}

int editorDrawMultiPaneRows(struct writeBuf *wb, const struct editorLeafLayout *layout,
                            const struct editorBorderList *borders,
                            struct editorRect focused_rect) {
	int ok = 0;
	int drawer_cols = editorDrawerWidthForCols(E.window_cols);
	int separator_cols = editorDrawerSeparatorWidthForCols(E.window_cols);
	int text_start_col = editorDrawerTextStartColForCols(E.window_cols);
	if (!editorPaneSyntaxFrameBuild(layout)) {
		goto cleanup;
	}

	for (int y_body = 0; y_body < E.window_rows; y_body++) {
		int screen_y = y_body + 1;
		int terminal_row = y_body + 2;
		if (!editorAppendCursorMove(wb, terminal_row, 1)) {
			goto cleanup;
		}
		if (!editorDrawDrawerRow(wb, y_body + 1, drawer_cols)) {
			goto cleanup;
		}
		if (!editorDrawDrawerSeparatorCell(wb, separator_cols)) {
			goto cleanup;
		}

		int focused_intersects =
		        screen_y >= focused_rect.y && screen_y < focused_rect.y + focused_rect.h;
		int x = text_start_col;
		while (x < E.window_cols) {
			enum paneViewBorderCellKind border =
			        paneViewBorderCellAt(x, screen_y, borders);
			if (border == PANE_VIEW_BORDER_CELL_VERTICAL) {
				if (!wbAppend(wb, EDITOR_PANE_VBORDER,
				              sizeof(EDITOR_PANE_VBORDER) - 1)) {
					goto cleanup;
				}
				x++;
				continue;
			}
			struct editorPaneNode *strip_leaf = NULL;
			int strip_local_col = 0;
			int strip_cols = 0;
			if (editorLayoutPaneTabStripAt(layout, x, screen_y, &strip_leaf,
			                               &strip_local_col, &strip_cols)) {
				int slice_cols = strip_cols - strip_local_col;
				if (!paneViewDrawStripSpan(wb, strip_leaf, strip_local_col,
				                           strip_cols, slice_cols, 1)) {
					goto cleanup;
				}
				x += slice_cols;
				continue;
			}
			if (border == PANE_VIEW_BORDER_CELL_HORIZONTAL) {
				if (!wbAppend(wb, EDITOR_PANE_HBORDER,
				              sizeof(EDITOR_PANE_HBORDER) - 1)) {
					goto cleanup;
				}
				x++;
				continue;
			}
			struct editorRect leaf_rect = {0};
			int leaf_idx = paneViewLeafAt(layout, x, screen_y, &leaf_rect);
			if (leaf_idx < 0) {
				if (!wbAppend(wb, " ", 1)) {
					goto cleanup;
				}
				x++;
				continue;
			}

			int slice_cols = leaf_rect.x + leaf_rect.w - x;
			if (slice_cols <= 0) {
				slice_cols = 1;
			}
			struct editorPaneNode *leaf_node = layout->rects[leaf_idx].node;
			struct editorTerminalPane *tab_term = editorTerminalPaneForPane(leaf_node);
			if (tab_term != NULL) {
				/* Active TERMINAL tab: the tab strip is in the border row
				 * above, so content starts at the pane's top row. */
				if (!editorDrawTerminalCells(wb, tab_term, screen_y - leaf_rect.y,
				                             x - leaf_rect.x, slice_cols)) {
					goto cleanup;
				}
			} else if (editorPaneActiveKind(leaf_node) ==
			           EDITOR_PANE_KIND_DEBUG_CONSOLE) {
				if (!paneViewDrawDapConsoleSlice(
				            wb, leaf_node, screen_y - leaf_rect.y, x - leaf_rect.x,
				            slice_cols, &leaf_rect)) {
					goto cleanup;
				}
			} else {
				int is_focused_slice =
				        focused_intersects && leaf_node == E.focused_leaf;
				if (is_focused_slice) {
					int body_row_in_pane = screen_y - focused_rect.y;
					g_screen_drawing_file_row_origin_col = x;
					g_screen_drawing_file_row_screen_y = screen_y;
					if (!editorDrawFocusedPaneSlice(
					            wb, leaf_node, body_row_in_pane, slice_cols)) {
						goto cleanup;
					}
				} else if (!editorDrawPaneViewSlice(
				                   wb, leaf_node, &leaf_node->as.leaf.view,
				                   screen_y - leaf_rect.y, slice_cols)) {
					goto cleanup;
				}
			}
			x += slice_cols;
		}
		if (!wbAppend(wb, VT100_CLEAR_ROW_3, 3)) {
			goto cleanup;
		}
	}

	ok = 1;
cleanup:
	editorPaneSyntaxFrameClear();
	return ok;
}
