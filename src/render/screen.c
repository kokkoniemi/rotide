#include "render/screen.h"

#include "config/theme_config.h"
#include "debug/dap.h"
#include "debug/dap_console.h"
#include "editing/document_position.h"
#include "editing/edit.h"
#include "editing/selection.h"
#include "input/input_system.h"
#include "language/syntax.h"
#include "language/syntax_visible_cache.h"
#include "render/ansi_style.h"
#include "render/display_text.h"
#include "render/pane_view.h"
#include "render/popup.h"
#include "render/status_bar.h"
#include "render/tab_bar.h"
#include "render/viewport.h"
#include "render/wrap.h"
#include "render/write_buf.h"
#include "rotide.h"
#include "support/alloc.h"
#include "support/size_utils.h"
#include "terminal/terminal_pane.h"
#include "text/document.h"
#include "text/row.h"
#include "text/utf8.h"
#include "vterm.h"
#include "workspace/drawer.h"
#include "workspace/file_search.h"
#include "workspace/git.h"
#include "workspace/layout.h"
#include "workspace/project_search.h"
#include "workspace/tabs.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define VT100_CLEAR_ROW_3 "\x1b[K"
#define VT100_RESET_CURSOR_POS_3 "\x1b[H"
#define VT100_HIDE_CURSOR_6 "\x1b[?25l"
#define VT100_SHOW_CURSOR_6 "\x1b[?25h"
#define VT100_CURSOR_BLINKING_BLOCK_5 "\x1b[1 q"
#define VT100_CURSOR_STEADY_BLOCK_5 "\x1b[2 q"
#define VT100_CURSOR_BLINKING_UNDERLINE_5 "\x1b[3 q"
#define VT100_CURSOR_STEADY_UNDERLINE_5 "\x1b[4 q"
#define VT100_CURSOR_BLINKING_BAR_5 "\x1b[5 q"
#define VT100_CURSOR_STEADY_BAR_5 "\x1b[6 q"
#define VT100_CURSOR_COLOR_WHITE "\x1b]12;white\a"
#define VT100_CURSOR_COLOR_DEFAULT "\x1b]112\a"
#define VT100_ITALIC_ON_4 "\x1b[3m"
#define VT100_ITALIC_OFF_5 "\x1b[23m"
#define VT100_BOLD_ON_4 "\x1b[1m"
#define VT100_BOLD_OFF_5 "\x1b[22m"
#define VT100_UNDERLINE_ON_4 "\x1b[4m"
#define VT100_UNDERLINE_OFF_5 "\x1b[24m"
#define VT100_UNDERLINE_COLOR_RED_9 "\x1b[58;5;1m"
#define VT100_UNDERLINE_COLOR_DEFAULT_5 "\x1b[59m"
#define VT100_BG_CURRENT_LINE "\x1b[48;5;236m"
#define EDITOR_DIAGNOSTIC_MAX_RENDER_SPANS_PER_ROW 64
#define TEXT_OVERFLOW_LEFT_UTF8 "\xE2\x86\x90"
#define TEXT_OVERFLOW_RIGHT_UTF8 "\xE2\x86\x92"
#define TEXT_WRAP_CONTINUATION_UTF8 "\xE2\x86\xB3"
#define TEXT_DAP_BREAKPOINT_UTF8 "\xE2\x97\x8F"   /* U+25CF BLACK CIRCLE */
#define TEXT_DAP_STOPPED_LINE_UTF8 "\xE2\x96\xB6" /* U+25B6 BLACK RIGHT-POINTING TRIANGLE */

int editorAppendGrayBytes(struct writeBuf *wb, const char *text, size_t len) {
	return editorAppendThemeForegroundRole(wb, EDITOR_THEME_UI_PLACEHOLDER) &&
	       wbAppend(wb, text, len) && editorAppendThemeBaseForeground(wb);
}

/*** Output ***/

struct screenFileRowFrameCache {
	int valid;
	int row_capacity;
	int row_count;
	int window_rows;
	int window_cols;
	char **rows;
	size_t *row_lens;
};

struct screenRenderSliceArgs {
	struct writeBuf *wb;
	const struct editorRow *row;
	const struct editorRowSyntaxSpan *syntax_spans;
	int syntax_span_count;
	const struct editorRowSyntaxSpan *diagnostic_spans;
	int diagnostic_span_count;
	int hover_render_start;
	int hover_render_end;
	int bracket_render_idx[2];
};

static struct screenFileRowFrameCache g_screen_file_row_frame_cache = {0};
static int g_screen_last_refresh_file_row_draw_count = 0;
int g_screen_drawing_current_line_highlight = 0;
int g_screen_drawing_stopped_line_highlight = 0;
int g_screen_drawing_focused_editor_pane = 0;
int g_screen_drawing_file_row_origin_col = -1;
int g_screen_drawing_file_row_screen_y = -1;
static int g_screen_popup_prev_screen_top = 0;
static int g_screen_popup_prev_row_count = 0;

struct screenGitBlameIndicatorHit {
	int active;
	int screen_y;
	int start_col;
	int end_col;
	int row_idx;
	int anchor_col;
};

static struct screenGitBlameIndicatorHit g_screen_git_blame_indicator_hit = {0};

static void screenFileRowFrameCacheClearRowsFrom(int start_row) {
	if (start_row < 0) {
		start_row = 0;
	}
	if (start_row > g_screen_file_row_frame_cache.row_capacity) {
		start_row = g_screen_file_row_frame_cache.row_capacity;
	}
	for (int i = start_row; i < g_screen_file_row_frame_cache.row_capacity; i++) {
		free(g_screen_file_row_frame_cache.rows[i]);
		g_screen_file_row_frame_cache.rows[i] = NULL;
		g_screen_file_row_frame_cache.row_lens[i] = 0;
	}
	if (g_screen_file_row_frame_cache.row_count < start_row) {
		g_screen_file_row_frame_cache.row_count = start_row;
	}
}

static void screenFileRowFrameCacheReset(void) {
	screenFileRowFrameCacheClearRowsFrom(0);
	g_screen_file_row_frame_cache.valid = 0;
	g_screen_file_row_frame_cache.row_count = 0;
	g_screen_file_row_frame_cache.window_rows = 0;
	g_screen_file_row_frame_cache.window_cols = 0;
	g_screen_popup_prev_screen_top = 0;
	g_screen_popup_prev_row_count = 0;
}

static int screenAppendTextRowReset(struct writeBuf *wb) {
	if (!editorAppendThemeReset(wb)) {
		return 0;
	}
	/* The debug stopped-line tint takes precedence over the cursor-line tint. */
	if (g_screen_drawing_stopped_line_highlight) {
		return editorAppendThemeBackgroundRole(wb, EDITOR_THEME_UI_DEBUG_STOPPED_LINE_BG);
	}
	if (g_screen_drawing_current_line_highlight &&
	    !editorAppendThemeBackgroundRole(wb, EDITOR_THEME_UI_CURRENT_LINE_BG)) {
		return 0;
	}
	return 1;
}

static int screenFileRowFrameCacheEnsureCapacity(int needed_rows) {
	if (needed_rows <= g_screen_file_row_frame_cache.row_capacity) {
		return 1;
	}
	if (needed_rows <= 0) {
		return 1;
	}

	size_t cap_size = 0;
	size_t rows_bytes = 0;
	if (!editorIntToSize(needed_rows, &cap_size) ||
	    !editorSizeMul(sizeof(*g_screen_file_row_frame_cache.rows), cap_size, &rows_bytes)) {
		return 0;
	}

	int old_capacity = g_screen_file_row_frame_cache.row_capacity;
	char **new_rows = editorRealloc(g_screen_file_row_frame_cache.rows, rows_bytes);
	if (new_rows == NULL) {
		return 0;
	}
	g_screen_file_row_frame_cache.rows = new_rows;

	size_t lens_bytes = 0;
	if (!editorSizeMul(sizeof(*g_screen_file_row_frame_cache.row_lens), cap_size,
	                   &lens_bytes)) {
		return 0;
	}
	size_t *new_lens = editorRealloc(g_screen_file_row_frame_cache.row_lens, lens_bytes);
	if (new_lens == NULL) {
		return 0;
	}
	g_screen_file_row_frame_cache.row_lens = new_lens;

	for (int i = old_capacity; i < needed_rows; i++) {
		g_screen_file_row_frame_cache.rows[i] = NULL;
		g_screen_file_row_frame_cache.row_lens[i] = 0;
	}

	g_screen_file_row_frame_cache.row_capacity = needed_rows;
	return 1;
}

static int screenFileRowFrameCacheStoreRow(int row_idx, const char *row_data, size_t row_len) {
	if (row_idx < 0 || row_data == NULL ||
	    row_idx >= g_screen_file_row_frame_cache.row_capacity) {
		return 0;
	}

	char *copy = NULL;
	if (row_len > 0) {
		copy = editorMalloc(row_len);
		if (copy == NULL) {
			return 0;
		}
		memcpy(copy, row_data, row_len);
	}

	free(g_screen_file_row_frame_cache.rows[row_idx]);
	g_screen_file_row_frame_cache.rows[row_idx] = copy;
	g_screen_file_row_frame_cache.row_lens[row_idx] = row_len;
	return 1;
}

int editorAppendCursorMove(struct writeBuf *wb, int row, int col) {
	char buf[32];
	int len = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", row, col);
	if (len <= 0 || len >= (int)sizeof(buf)) {
		return 0;
	}
	return wbAppend(wb, buf, (size_t)len);
}

static void screenRenderSliceBounds(const struct editorRow *row, int coloff, int cols,
                                    int *start_out, int *end_out) {
	int rx = 0;
	int start = -1;
	int end = row->rsize;

	// Map display columns back to byte offsets in row->render.
	// This keeps horizontal scrolling aligned even when wide/zero-width
	// codepoints appear in the rendered data.
	for (int i = 0; i < row->rsize; i++) {
		int width = editorCharDisplayWidth(&row->render[i], row->rsize - i);

		if (start == -1 && rx + width > coloff) {
			start = i;
		}
		if (start != -1 && width > 0 && rx + width > coloff + cols) {
			end = i;
			break;
		}

		rx += width;
	}

	*start_out = start;
	*end_out = end;
}

static int screenSelectionSpanForRow(int row_idx, int *start_out, int *end_out) {
	if (row_idx < 0 || row_idx >= E.numrows) {
		return 0;
	}

	if (E.column_select_active) {
		struct editorColumnSelectionRect rect;
		if (!editorColumnSelectionGetRect(&rect)) {
			return 0;
		}
		if (row_idx < rect.top_cy || row_idx > rect.bottom_cy) {
			return 0;
		}
		int cx_start = 0;
		int cx_end = 0;
		if (!editorColumnSelectionRowSpan(row_idx, rect.left_rx, rect.right_rx, &cx_start,
		                                  &cx_end)) {
			return 0;
		}
		if (cx_end <= cx_start) {
			return 0;
		}
		*start_out = cx_start;
		*end_out = cx_end;
		return 1;
	}

	struct editorSelectionRange selection;
	if (!editorGetSelectionRange(&selection)) {
		return 0;
	}
	if (row_idx < selection.start_cy || row_idx > selection.end_cy) {
		return 0;
	}

	int start = 0;
	int end = (int)editorDocumentLineLength(E.document, row_idx);
	if (selection.start_cy == selection.end_cy) {
		start = selection.start_cx;
		end = selection.end_cx;
	} else {
		if (row_idx == selection.start_cy) {
			start = selection.start_cx;
		}
		if (row_idx == selection.end_cy && selection.end_cy < E.numrows) {
			end = selection.end_cx;
		}
	}

	if (end <= start) {
		return 0;
	}

	*start_out = start;
	*end_out = end;
	return 1;
}

static int screenSearchSpanForRow(int row_idx, int *start_out, int *end_out) {
	if (row_idx < 0 || row_idx >= E.numrows || E.search_match_len <= 0) {
		return 0;
	}

	int start_row = 0;
	int start_col = 0;
	if (!editorBufferOffsetToPos(E.search_match_offset, &start_row, &start_col)) {
		return 0;
	}

	size_t end_offset = E.search_match_offset + (size_t)E.search_match_len;
	int end_row = 0;
	int end_col = 0;
	if (!editorBufferOffsetToPos(end_offset, &end_row, &end_col)) {
		return 0;
	}

	if (row_idx < start_row || row_idx > end_row) {
		return 0;
	}

	int start = 0;
	int end = (int)editorDocumentLineLength(E.document, row_idx);
	if (start_row == end_row) {
		start = start_col;
		end = end_col;
	} else {
		if (row_idx == start_row) {
			start = start_col;
		}
		if (row_idx == end_row && end_row < E.numrows) {
			end = end_col;
		}
	}

	if (end <= start) {
		return 0;
	}

	*start_out = start;
	*end_out = end;
	return 1;
}

static enum editorSyntaxHighlightClass
screenSyntaxClassAtRenderIdx(const struct editorRowSyntaxSpan *spans, int span_count,
                             int render_idx) {
	enum editorSyntaxHighlightClass highlight_class = EDITOR_SYNTAX_HL_NONE;
	for (int i = 0; i < span_count; i++) {
		if (spans[i].end_render_idx <= spans[i].start_render_idx) {
			continue;
		}
		if (render_idx >= spans[i].start_render_idx &&
		    render_idx < spans[i].end_render_idx) {
			highlight_class = spans[i].highlight_class;
		}
	}
	return highlight_class;
}

static int screenDiagnosticSeverityIsError(int severity) {
	return severity == 1;
}

static int screenDiagnosticRangeForRow(const struct editorLspDiagnostic *diagnostic, int row_idx,
                                       int row_size, int *start_out, int *end_out) {
	if (diagnostic == NULL || start_out == NULL || end_out == NULL ||
	    row_idx < diagnostic->start_line || row_idx > diagnostic->end_line ||
	    diagnostic->end_line < diagnostic->start_line) {
		return 0;
	}

	int start = row_idx == diagnostic->start_line ? diagnostic->start_character : 0;
	int end = row_idx == diagnostic->end_line ? diagnostic->end_character : row_size;
	if (start < 0) {
		start = 0;
	}
	if (end < 0) {
		end = 0;
	}
	if (start > row_size) {
		start = row_size;
	}
	if (end > row_size) {
		end = row_size;
	}
	if (end <= start && start < row_size) {
		end = start + 1;
	}
	if (end <= start) {
		return 0;
	}

	*start_out = start;
	*end_out = end;
	return 1;
}

static int screenDiagnosticAtRenderIdx(const struct editorRowSyntaxSpan *spans, int span_count,
                                       int render_idx) {
	for (int i = 0; i < span_count; i++) {
		if (spans[i].end_render_idx <= spans[i].start_render_idx) {
			continue;
		}
		if (render_idx >= spans[i].start_render_idx &&
		    render_idx < spans[i].end_render_idx) {
			return 1;
		}
	}
	return 0;
}

static int screenBuildDiagnosticRenderSpansForRow(int row_idx, const struct editorRow *row,
                                                  struct editorRowSyntaxSpan *spans, int max_spans,
                                                  int *span_count_out) {
	if (span_count_out != NULL) {
		*span_count_out = 0;
	}
	if (row == NULL || spans == NULL || span_count_out == NULL || max_spans <= 0 ||
	    E.lsp_diagnostics == NULL || E.lsp_diagnostic_count <= 0) {
		return 1;
	}

	struct editorLineView line = {0};
	if (!editorDocumentLineView(E.document, row_idx, &line)) {
		return 1;
	}
	int count = 0;
	for (int i = 0; i < E.lsp_diagnostic_count && count < max_spans; i++) {
		if (!screenDiagnosticSeverityIsError(E.lsp_diagnostics[i].severity)) {
			continue;
		}
		int start = 0;
		int end = 0;
		if (!screenDiagnosticRangeForRow(&E.lsp_diagnostics[i], row_idx, line.size, &start,
		                                 &end)) {
			continue;
		}
		int render_start =
		        editorBytesCxToRenderIdx(line.data, line.size, row->rsize, start);
		int render_end = editorBytesCxToRenderIdx(line.data, line.size, row->rsize, end);
		if (render_end <= render_start) {
			continue;
		}
		spans[count].start_render_idx = render_start;
		spans[count].end_render_idx = render_end;
		spans[count].highlight_class = EDITOR_SYNTAX_HL_NONE;
		count++;
	}
	editorLineViewRelease(&line);
	*span_count_out = count;
	return 1;
}

static int screenAppendUnderlineToggle(struct writeBuf *wb, int on) {
	if (on) {
		return wbAppend(wb, VT100_UNDERLINE_ON_4, 4);
	}
	return wbAppend(wb, VT100_UNDERLINE_OFF_5, 5);
}

static int screenAppendUnderlineColor(struct writeBuf *wb, int red) {
	if (red) {
		return wbAppend(wb, VT100_UNDERLINE_COLOR_RED_9, 9);
	}
	return wbAppend(wb, VT100_UNDERLINE_COLOR_DEFAULT_5, 5);
}

static int screenBracketBoldAtRenderIdx(const struct screenRenderSliceArgs *args, int pos) {
	for (int i = 0; i < 2; i++) {
		int b = args->bracket_render_idx[i];
		if (b >= 0 && pos == b) {
			return 1;
		}
	}
	return 0;
}

static int screenSliceForegroundForPos(const struct screenRenderSliceArgs *args, int pos,
                                       struct editorThemeColor base_color,
                                       struct editorThemeColor *out_color) {
	enum editorSyntaxHighlightClass highlight_class =
	        screenSyntaxClassAtRenderIdx(args->syntax_spans, args->syntax_span_count, pos);
	*out_color = base_color;
	if (highlight_class > EDITOR_SYNTAX_HL_NONE &&
	    highlight_class < EDITOR_SYNTAX_HL_CLASS_COUNT) {
		*out_color = E.theme.syntax[highlight_class];
	}
	return 1;
}

static int screenSliceApplyForeground(struct writeBuf *wb, struct editorThemeColor next_color,
                                      struct editorThemeColor *active_color,
                                      int *active_color_emitted) {
	if (editorThemeColorEquals(next_color, *active_color)) {
		return 1;
	}
	if (!editorAppendThemeForeground(wb, next_color)) {
		return 0;
	}
	*active_color = next_color;
	*active_color_emitted = 1;
	return 1;
}

static int screenSliceApplyUnderline(struct writeBuf *wb, int next_on, int next_red,
                                     int *underline_on, int *underline_red) {
	if (next_on != *underline_on) {
		if (!screenAppendUnderlineToggle(wb, next_on)) {
			return 0;
		}
		*underline_on = next_on;
	}
	if (next_red != *underline_red) {
		if (!screenAppendUnderlineColor(wb, next_red)) {
			return 0;
		}
		*underline_red = next_red;
	}
	return 1;
}

static int screenSliceApplyBold(struct writeBuf *wb, int next_bold, int *bold_on) {
	if (next_bold == *bold_on) {
		return 1;
	}
	if (!wbAppend(wb, next_bold ? VT100_BOLD_ON_4 : VT100_BOLD_OFF_5,
	              next_bold ? sizeof(VT100_BOLD_ON_4) - 1 : sizeof(VT100_BOLD_OFF_5) - 1)) {
		return 0;
	}
	*bold_on = next_bold;
	return 1;
}

static void screenSliceConsiderBoundary(int candidate, int pos, int *next) {
	if (candidate > pos && candidate < *next) {
		*next = candidate;
	}
}

static int screenSliceNextSpanBoundary(const struct screenRenderSliceArgs *args, int pos,
                                       int segment_end, int has_hover) {
	int next = segment_end;
	for (int i = 0; i < args->syntax_span_count; i++) {
		int span_start = args->syntax_spans[i].start_render_idx;
		int span_end = args->syntax_spans[i].end_render_idx;
		if (span_end <= span_start) {
			continue;
		}
		screenSliceConsiderBoundary(span_start, pos, &next);
		screenSliceConsiderBoundary(span_end, pos, &next);
	}
	for (int i = 0; i < args->diagnostic_span_count; i++) {
		int span_start = args->diagnostic_spans[i].start_render_idx;
		int span_end = args->diagnostic_spans[i].end_render_idx;
		if (span_end <= span_start) {
			continue;
		}
		screenSliceConsiderBoundary(span_start, pos, &next);
		screenSliceConsiderBoundary(span_end, pos, &next);
	}
	if (has_hover) {
		screenSliceConsiderBoundary(args->hover_render_start, pos, &next);
		screenSliceConsiderBoundary(args->hover_render_end, pos, &next);
	}
	for (int i = 0; i < 2; i++) {
		int b = args->bracket_render_idx[i];
		if (b < 0) {
			continue;
		}
		screenSliceConsiderBoundary(b, pos, &next);
		screenSliceConsiderBoundary(b + 1, pos, &next);
	}
	return next;
}

static int screenSliceNextCodepointBoundary(const struct editorRow *row, int pos, int segment_end) {
	unsigned int cp = 0;
	int step = editorUtf8DecodeCodepoint(&row->render[pos], segment_end - pos, &cp);
	(void)cp;
	if (step <= 0) {
		step = 1;
	}
	if (step > segment_end - pos) {
		step = segment_end - pos;
	}
	return pos + step;
}

static int screenSliceResetStyles(struct writeBuf *wb, int active_color_emitted, int underline_on,
                                  int underline_red, int bold_on) {
	if (underline_on && !screenAppendUnderlineToggle(wb, 0)) {
		return 0;
	}
	if (underline_red && !screenAppendUnderlineColor(wb, 0)) {
		return 0;
	}
	if (bold_on && !wbAppend(wb, VT100_BOLD_OFF_5, sizeof(VT100_BOLD_OFF_5) - 1)) {
		return 0;
	}
	if (active_color_emitted && !editorAppendThemeBaseForeground(wb)) {
		return 0;
	}
	return 1;
}

static int screenSliceSyntaxFastPathApplies(const struct screenRenderSliceArgs *args) {
	int has_syntax = args->syntax_spans != NULL && args->syntax_span_count > 0 &&
	                 E.syntax_state != NULL && E.syntax_language != EDITOR_SYNTAX_NONE;
	int has_diag = args->diagnostic_spans != NULL && args->diagnostic_span_count > 0;
	int has_bracket = args->bracket_render_idx[0] >= 0 || args->bracket_render_idx[1] >= 0;
	return !has_syntax && !has_diag && !has_bracket;
}

static int screenDrawRenderSliceWithSyntax(const struct screenRenderSliceArgs *args,
                                           int segment_start, int segment_end) {
	if (segment_end <= segment_start) {
		return 1;
	}

	struct writeBuf *wb = args->wb;
	const struct editorRow *row = args->row;

	if (screenSliceSyntaxFastPathApplies(args)) {
		return wbAppend(wb, &row->render[segment_start],
		                (size_t)(segment_end - segment_start));
	}

	struct editorThemeColor base_color = E.theme.ui[EDITOR_THEME_UI_FOREGROUND];
	struct editorThemeColor active_color = base_color;
	int active_color_emitted = 0;
	int underline_on = 0;
	int underline_red = 0;
	int bold_on = 0;
	int has_hover = args->hover_render_end > args->hover_render_start;
	int pos = segment_start;
	while (pos < segment_end) {
		struct editorThemeColor next_color;
		(void)screenSliceForegroundForPos(args, pos, base_color, &next_color);
		if (!screenSliceApplyForeground(wb, next_color, &active_color,
		                                &active_color_emitted)) {
			return 0;
		}

		int diag_here = screenDiagnosticAtRenderIdx(args->diagnostic_spans,
		                                            args->diagnostic_span_count, pos);
		int hover_here = has_hover && pos >= args->hover_render_start &&
		                 pos < args->hover_render_end;
		if (!screenSliceApplyUnderline(wb, diag_here || hover_here, diag_here,
		                               &underline_on, &underline_red)) {
			return 0;
		}

		if (!screenSliceApplyBold(wb, screenBracketBoldAtRenderIdx(args, pos), &bold_on)) {
			return 0;
		}

		int next = screenSliceNextSpanBoundary(args, pos, segment_end, has_hover);
		if (next <= pos) {
			next = screenSliceNextCodepointBoundary(row, pos, segment_end);
		}

		if (!wbAppend(wb, &row->render[pos], (size_t)(next - pos))) {
			return 0;
		}
		pos = next;
	}

	return screenSliceResetStyles(wb, active_color_emitted, underline_on, underline_red,
	                              bold_on);
}

static void screenResolveHighlightSpan(int row_idx, int *highlight_start_chars_out,
                                       int *highlight_len_chars_out) {
	int highlight_start_chars = -1;
	int highlight_len_chars = 0;
	int selection_start = 0;
	int selection_end = 0;

	if (screenSelectionSpanForRow(row_idx, &selection_start, &selection_end)) {
		highlight_start_chars = selection_start;
		highlight_len_chars = selection_end - selection_start;
	} else if (screenSearchSpanForRow(row_idx, &selection_start, &selection_end)) {
		highlight_start_chars = selection_start;
		highlight_len_chars = selection_end - selection_start;
	}

	*highlight_start_chars_out = highlight_start_chars;
	*highlight_len_chars_out = highlight_len_chars;
}

static void screenGatherHoverRange(int row_idx, const struct editorRow *row,
                                   const struct editorLineView *row_line, int row_size,
                                   int have_row_line, int *hover_render_start_out,
                                   int *hover_render_end_out) {
	int hover_render_start = -1;
	int hover_render_end = -1;
	if (have_row_line && E.hover_link_active && E.hover_link_row == row_idx &&
	    E.hover_link_cx_end > E.hover_link_cx_start) {
		int hov_start_clamped = E.hover_link_cx_start;
		int hov_end_clamped = E.hover_link_cx_end;
		if (hov_start_clamped < 0) {
			hov_start_clamped = 0;
		}
		if (hov_end_clamped > row_size) {
			hov_end_clamped = row_size;
		}
		if (hov_end_clamped > hov_start_clamped) {
			hover_render_start = editorBytesCxToRenderIdx(
			        row_line->data, row_size, row->rsize, hov_start_clamped);
			hover_render_end = editorBytesCxToRenderIdx(row_line->data, row_size,
			                                            row->rsize, hov_end_clamped);
		}
	}

	*hover_render_start_out = hover_render_start;
	*hover_render_end_out = hover_render_end;
}

static void screenGatherBracketIndices(int row_idx, const struct editorRow *row,
                                       const struct editorLineView *row_line, int row_size,
                                       int have_row_line, int bracket_render_idx[2]) {
	bracket_render_idx[0] = -1;
	bracket_render_idx[1] = -1;

	int bracket_cols[2] = {0, 0};
	int bracket_count = 0;
	if (have_row_line &&
	    editorPaneActiveBracketColsForRow(row_idx, bracket_cols, &bracket_count)) {
		for (int i = 0; i < bracket_count; i++) {
			int cx = bracket_cols[i];
			if (cx >= 0 && cx < row_size) {
				bracket_render_idx[i] = editorBytesCxToRenderIdx(
				        row_line->data, row_size, row->rsize, cx);
			}
		}
	}
}

static void screenResolveHighlightRenderRange(const struct editorRow *row,
                                              const struct editorLineView *row_line, int row_size,
                                              int have_row_line, int highlight_start_chars,
                                              int highlight_len_chars, int *match_render_start_out,
                                              int *match_render_end_out) {
	int match_start_chars = highlight_start_chars;
	if (match_start_chars < 0) {
		match_start_chars = 0;
	}
	if (match_start_chars > row_size) {
		match_start_chars = row_size;
	}
	long long match_end_ll = (long long)match_start_chars + (long long)highlight_len_chars;
	if (match_end_ll < match_start_chars) {
		match_end_ll = match_start_chars;
	}
	if (match_end_ll > row_size) {
		match_end_ll = row_size;
	}
	int match_end_chars = (int)match_end_ll;

	*match_render_start_out = have_row_line
	                                  ? editorBytesCxToRenderIdx(row_line->data, row_size,
	                                                             row->rsize, match_start_chars)
	                                  : 0;
	*match_render_end_out = have_row_line
	                                ? editorBytesCxToRenderIdx(row_line->data, row_size,
	                                                           row->rsize, match_end_chars)
	                                : 0;
}

static int screenDrawRenderSlice(struct writeBuf *wb, struct editorRow *row, int row_idx,
                                 int coloff, int cols) {
	if (cols <= 0 || coloff < 0 || row->rsize <= 0) {
		return 1;
	}

	int start = -1;
	int end = row->rsize;
	screenRenderSliceBounds(row, coloff, cols, &start, &end);
	if (start == -1 || end <= start) {
		return 1;
	}

	int highlight_start_chars = -1;
	int highlight_len_chars = 0;
	screenResolveHighlightSpan(row_idx, &highlight_start_chars, &highlight_len_chars);

	struct editorRowSyntaxSpan syntax_spans[ROTIDE_MAX_SYNTAX_SPANS_PER_ROW];
	int syntax_span_count = 0;
	if (!editorPaneSyntaxRowOverrideCopy(row_idx, syntax_spans, ROTIDE_MAX_SYNTAX_SPANS_PER_ROW,
	                                     &syntax_span_count) &&
	    !editorSyntaxRowRenderSpans(row_idx, syntax_spans, ROTIDE_MAX_SYNTAX_SPANS_PER_ROW,
	                                &syntax_span_count)) {
		syntax_span_count = 0;
	}
	struct editorRowSyntaxSpan diagnostic_spans[EDITOR_DIAGNOSTIC_MAX_RENDER_SPANS_PER_ROW];
	int diagnostic_span_count = 0;
	if (!screenBuildDiagnosticRenderSpansForRow(row_idx, row, diagnostic_spans,
	                                            EDITOR_DIAGNOSTIC_MAX_RENDER_SPANS_PER_ROW,
	                                            &diagnostic_span_count)) {
		diagnostic_span_count = 0;
	}

	struct editorLineView row_line = {0};
	int have_row_line = editorDocumentLineView(E.document, row_idx, &row_line);
	int row_size = have_row_line ? row_line.size : 0;

	int hover_render_start = -1;
	int hover_render_end = -1;
	screenGatherHoverRange(row_idx, row, &row_line, row_size, have_row_line,
	                       &hover_render_start, &hover_render_end);

	int bracket_render_idx[2] = {-1, -1};
	screenGatherBracketIndices(row_idx, row, &row_line, row_size, have_row_line,
	                           bracket_render_idx);

	struct screenRenderSliceArgs slice = {
	        .wb = wb,
	        .row = row,
	        .syntax_spans = syntax_spans,
	        .syntax_span_count = syntax_span_count,
	        .diagnostic_spans = diagnostic_spans,
	        .diagnostic_span_count = diagnostic_span_count,
	        .hover_render_start = hover_render_start,
	        .hover_render_end = hover_render_end,
	        .bracket_render_idx = {bracket_render_idx[0], bracket_render_idx[1]},
	};

	if (highlight_len_chars <= 0) {
		editorLineViewRelease(&row_line);
		return screenDrawRenderSliceWithSyntax(&slice, start, end);
	}

	int match_render_start = 0;
	int match_render_end = 0;
	screenResolveHighlightRenderRange(row, &row_line, row_size, have_row_line,
	                                  highlight_start_chars, highlight_len_chars,
	                                  &match_render_start, &match_render_end);
	editorLineViewRelease(&row_line);
	if (match_render_end <= match_render_start) {
		return screenDrawRenderSliceWithSyntax(&slice, start, end);
	}

	int highlight_start = start > match_render_start ? start : match_render_start;
	int highlight_end = end < match_render_end ? end : match_render_end;
	if (highlight_end <= highlight_start) {
		return screenDrawRenderSliceWithSyntax(&slice, start, end);
	}

	if (highlight_start > start &&
	    !screenDrawRenderSliceWithSyntax(&slice, start, highlight_start)) {
		return 0;
	}
	if (!editorAppendThemeStyle(wb, EDITOR_THEME_STYLE_SELECTION)) {
		return 0;
	}
	if (!wbAppend(wb, &row->render[highlight_start], highlight_end - highlight_start)) {
		return 0;
	}
	if (!screenAppendTextRowReset(wb)) {
		return 0;
	}
	if (highlight_end < end && !screenDrawRenderSliceWithSyntax(&slice, highlight_end, end)) {
		return 0;
	}

	return 1;
}

static int screenRenderSliceDisplayCols(const struct editorRow *row, int coloff, int cols,
                                        int *has_right_overflow_out) {
	if (has_right_overflow_out != NULL) {
		*has_right_overflow_out = 0;
	}
	if (row == NULL || cols <= 0 || coloff < 0 || row->rsize <= 0) {
		return 0;
	}

	int start = -1;
	int end = row->rsize;
	screenRenderSliceBounds(row, coloff, cols, &start, &end);
	if (start == -1 || end <= start) {
		return 0;
	}

	int drawn_cols = 0;
	for (int i = start; i < end;) {
		unsigned int cp = 0;
		int src_len = editorUtf8DecodeCodepoint(&row->render[i], end - i, &cp);
		if (src_len <= 0) {
			src_len = 1;
		}
		if (src_len > end - i) {
			src_len = end - i;
		}
		drawn_cols += editorCharDisplayWidth(&row->render[i], end - i);
		i += src_len;
	}

	if (has_right_overflow_out != NULL) {
		*has_right_overflow_out = end < row->rsize;
	}
	return drawn_cols;
}

static int screenAppendGrayGlyph(struct writeBuf *wb, const char *glyph, size_t glyph_len) {
	return editorAppendGrayBytes(wb, glyph, glyph_len);
}

static void screenGitBlameIndicatorHitClear(void) {
	memset(&g_screen_git_blame_indicator_hit, 0, sizeof(g_screen_git_blame_indicator_hit));
}

static void screenGitBlameIndicatorHitRecord(int row_idx, int start_col, int written_cols,
                                             int anchor_col) {
	if (written_cols <= 0 || g_screen_drawing_file_row_screen_y < 0 || start_col < 0) {
		return;
	}
	g_screen_git_blame_indicator_hit.active = 1;
	g_screen_git_blame_indicator_hit.screen_y = g_screen_drawing_file_row_screen_y;
	g_screen_git_blame_indicator_hit.start_col = start_col;
	g_screen_git_blame_indicator_hit.end_col = start_col + written_cols;
	g_screen_git_blame_indicator_hit.row_idx = row_idx;
	g_screen_git_blame_indicator_hit.anchor_col = anchor_col;
}

int editorGitBlameIndicatorHitTest(int screen_row, int screen_col, int *row_out,
                                   int *anchor_col_out) {
	if (!g_screen_git_blame_indicator_hit.active ||
	    screen_row != g_screen_git_blame_indicator_hit.screen_y ||
	    screen_col < g_screen_git_blame_indicator_hit.start_col ||
	    screen_col >= g_screen_git_blame_indicator_hit.end_col) {
		return 0;
	}
	if (row_out != NULL) {
		*row_out = g_screen_git_blame_indicator_hit.row_idx;
	}
	if (anchor_col_out != NULL) {
		*anchor_col_out = g_screen_git_blame_indicator_hit.anchor_col;
	}
	return 1;
}

int editorGitBlameIndicatorTestRange(int *screen_y_out, int *start_col_out, int *end_col_out,
                                     int *anchor_col_out) {
	if (!g_screen_git_blame_indicator_hit.active) {
		return 0;
	}
	if (screen_y_out != NULL) {
		*screen_y_out = g_screen_git_blame_indicator_hit.screen_y;
	}
	if (start_col_out != NULL) {
		*start_col_out = g_screen_git_blame_indicator_hit.start_col;
	}
	if (end_col_out != NULL) {
		*end_col_out = g_screen_git_blame_indicator_hit.end_col;
	}
	if (anchor_col_out != NULL) {
		*anchor_col_out = g_screen_git_blame_indicator_hit.anchor_col;
	}
	return 1;
}

static int screenGitBlameIndicatorApplies(int row_idx, int segment_coloff) {
	if (!g_screen_drawing_focused_editor_pane || E.primary_focus != EDITOR_PRIMARY_FOCUS_TEXT ||
	    row_idx != E.cy) {
		return 0;
	}
	if (!E.line_wrap_enabled) {
		return 1;
	}
	if (row_idx < 0 || row_idx >= E.numrows) {
		return 0;
	}
	int body_cols = editorWrapBodyCols();
	int cursor_segment = editorWrapCursorSegmentForRx(&E.rows[row_idx], E.rx, body_cols);
	int cursor_segment_coloff = 0;
	editorWrapSegmentInfo(&E.rows[row_idx], cursor_segment, body_cols, &cursor_segment_coloff,
	                      NULL, NULL);
	return segment_coloff == cursor_segment_coloff;
}

static int screenAppendGitBlameIndicator(struct writeBuf *wb, int row_idx, int segment_coloff,
                                         int indicator_col, int anchor_col, int available_cols,
                                         int *written_cols_out) {
	if (written_cols_out != NULL) {
		*written_cols_out = 0;
	}
	if (available_cols <= 2 || !screenGitBlameIndicatorApplies(row_idx, segment_coloff)) {
		return 1;
	}

	char label[256];
	if (!editorGitBlameActiveInlineLabel(row_idx + 1, time(NULL), label, sizeof(label))) {
		return 1;
	}

	/* Right-align the blame label to the right edge of the text body so it reads as part
	 * of the current-line indicator rather than as buffer content. When the line is long
	 * enough that a right-aligned label would overlap the text, fall back to placing it
	 * immediately after the line end (it may then run off the viewport). */
	int label_cols = 0;
	char *measured = editorSanitizeTextDup(label, &label_cols);
	if (measured == NULL) {
		return 0;
	}
	free(measured);

	int leading_pad = available_cols - label_cols;
	if (leading_pad < 0) {
		leading_pad = 0;
	}
	for (int pad = 0; pad < leading_pad; pad++) {
		if (!wbAppend(wb, " ", 1)) {
			return 0;
		}
	}

	int written_cols = 0;
	if (!editorAppendThemeForegroundRole(wb, EDITOR_THEME_UI_PLACEHOLDER) ||
	    !editorAppendSanitizedText(wb, label, available_cols - leading_pad, &written_cols) ||
	    !editorAppendThemeBaseForeground(wb)) {
		return 0;
	}
	screenGitBlameIndicatorHitRecord(row_idx, indicator_col + leading_pad, written_cols,
	                                 anchor_col + leading_pad);
	if (written_cols_out != NULL) {
		*written_cols_out = leading_pad + written_cols;
	}
	return 1;
}

int editorCurrentLineHighlightApplies(int row_idx, int segment_coloff) {
	if (!E.current_line_highlight_enabled || row_idx != E.cy) {
		return 0;
	}
	if (E.primary_focus != EDITOR_PRIMARY_FOCUS_TEXT && !E.is_preview) {
		const char *lsp_path = NULL;
		int lsp_line = -1;
		int lsp_character = -1;
		if (E.primary_focus != EDITOR_PRIMARY_FOCUS_DRAWER ||
		    E.drawer_mode != EDITOR_DRAWER_MODE_LSP ||
		    !editorDrawerSelectedLspLocation(&lsp_path, &lsp_line, &lsp_character) ||
		    lsp_path == NULL || E.filename == NULL || strcmp(lsp_path, E.filename) != 0 ||
		    lsp_line != E.cy) {
			return 0;
		}
	}
	if (!E.line_wrap_enabled) {
		return 1;
	}
	if (row_idx < 0 || row_idx >= E.numrows) {
		return 0;
	}

	int body_cols = editorWrapBodyCols();
	int cursor_segment = editorWrapCursorSegmentForRx(&E.rows[row_idx], E.rx, body_cols);
	int cursor_segment_coloff = 0;
	editorWrapSegmentInfo(&E.rows[row_idx], cursor_segment, body_cols, &cursor_segment_coloff,
	                      NULL, NULL);
	return segment_coloff == cursor_segment_coloff;
}

int editorDebugStoppedLineHighlightApplies(int row_idx) {
	return row_idx >= 0 && row_idx < E.numrows && editorDapIsStoppedLine(E.filename, row_idx);
}

int editorDrawLineNumberGutter(struct writeBuf *wb, int row_idx, int segment_coloff,
                               int gutter_cols) {
	if (gutter_cols <= 0) {
		return 1;
	}

	if (row_idx >= 0 && row_idx < E.numrows && segment_coloff == 0) {
		char number[32];
		int len = snprintf(number, sizeof(number), "%d", row_idx + 1);
		if (len < 0) {
			return 0;
		}
		if (len >= (int)sizeof(number)) {
			len = (int)sizeof(number) - 1;
		}

		int number_cols = gutter_cols > 1 ? gutter_cols - 1 : gutter_cols;
		int pad_cols = number_cols - len;
		if (pad_cols < 0) {
			pad_cols = 0;
		}

		if (!editorAppendThemeForegroundRole(wb, EDITOR_THEME_UI_LINE_NUMBER)) {
			return 0;
		}
		for (int pad = 0; pad < pad_cols; pad++) {
			if (!wbAppend(wb, " ", 1)) {
				return 0;
			}
		}
		const char *visible_number = number;
		if (len > number_cols) {
			visible_number = number + (len - number_cols);
			len = number_cols;
		}
		if (len > 0 && !wbAppend(wb, visible_number, (size_t)len)) {
			return 0;
		}
		/* The trailing separator column doubles as the debug marker slot, so a
		 * breakpoint/stopped indicator never widens the gutter. Stopped line
		 * takes precedence over a breakpoint on the same row. */
		if (gutter_cols > 1) {
			if (editorDapIsStoppedLine(E.filename, row_idx)) {
				if (!editorAppendThemeForegroundRole(
				            wb, EDITOR_THEME_UI_DEBUG_STOPPED_LINE) ||
				    !wbAppend(wb, TEXT_DAP_STOPPED_LINE_UTF8,
				              sizeof(TEXT_DAP_STOPPED_LINE_UTF8) - 1)) {
					return 0;
				}
			} else if (editorDapHasBreakpoint(E.filename, row_idx) >= 0) {
				if (!editorAppendThemeForegroundRole(wb,
				                                     EDITOR_THEME_UI_BREAKPOINT) ||
				    !wbAppend(wb, TEXT_DAP_BREAKPOINT_UTF8,
				              sizeof(TEXT_DAP_BREAKPOINT_UTF8) - 1)) {
					return 0;
				}
			} else if (!wbAppend(wb, " ", 1)) {
				return 0;
			}
		}
		return editorAppendThemeBaseForeground(wb);
	}

	for (int col = 0; col < gutter_cols; col++) {
		if (!wbAppend(wb, " ", 1)) {
			return 0;
		}
	}
	return 1;
}

int editorDrawFileRowWrapped(struct writeBuf *wb, size_t i, int text_cols, int segment_coloff) {
	struct editorRow *row = &E.rows[i];
	if (text_cols >= 3) {
		int body_cols = text_cols - 2;
		if (body_cols < 1) {
			body_cols = 1;
		}
		int indent_cols =
		        segment_coloff > 0 ? editorWrapContinuationIndentCols(row, body_cols) : 0;
		int available_cols = body_cols - indent_cols;
		if (available_cols < 1) {
			available_cols = 1;
			indent_cols = body_cols > 1 ? body_cols - 1 : 0;
		}
		int total_cols = row->render_display_cols;
		int draw_cols = available_cols;
		if (segment_coloff < total_cols) {
			int next_start = editorWrapNextStartCol(row, segment_coloff, available_cols,
			                                        total_cols);
			if (next_start > segment_coloff &&
			    next_start - segment_coloff < draw_cols) {
				draw_cols = next_start - segment_coloff;
			}
		}
		int rendered_cols =
		        screenRenderSliceDisplayCols(row, segment_coloff, draw_cols, NULL);

		if (segment_coloff > 0) {
			if (!screenAppendGrayGlyph(wb, TEXT_WRAP_CONTINUATION_UTF8,
			                           sizeof(TEXT_WRAP_CONTINUATION_UTF8) - 1)) {
				return 0;
			}
		} else if (!wbAppend(wb, " ", 1)) {
			return 0;
		}

		for (int pad = 0; pad < indent_cols; pad++) {
			if (!wbAppend(wb, " ", 1)) {
				return 0;
			}
		}

		if (!screenDrawRenderSlice(wb, row, (int)i, segment_coloff, draw_cols)) {
			return 0;
		}

		int blame_cols = 0;
		int indicator_col =
		        g_screen_drawing_file_row_origin_col + 1 + indent_cols + rendered_cols;
		int anchor_col = segment_coloff + indent_cols + rendered_cols;
		if (!screenAppendGitBlameIndicator(
		            wb, (int)i, segment_coloff, indicator_col, anchor_col,
		            body_cols - indent_cols - rendered_cols, &blame_cols)) {
			return 0;
		}

		for (int pad = indent_cols + rendered_cols + blame_cols; pad < body_cols; pad++) {
			if (!wbAppend(wb, " ", 1)) {
				return 0;
			}
		}

		return wbAppend(wb, " ", 1);
	}

	return screenDrawRenderSlice(wb, row, (int)i, segment_coloff, text_cols);
}

int editorDrawFileRow(struct writeBuf *wb, size_t i, int text_cols) {
	struct editorRow *row = &E.rows[i];
	if (E.line_wrap_enabled) {
		return editorDrawFileRowWrapped(wb, i, text_cols, 0);
	}
	if (text_cols >= 3) {
		int body_cols = text_cols - 2;
		if (body_cols < 1) {
			body_cols = 1;
		}
		int has_right_overflow = 0;
		int rendered_cols =
		        screenRenderSliceDisplayCols(row, E.coloff, body_cols, &has_right_overflow);
		int has_left_overflow = E.coloff > 0 && row->rsize > 0;

		if (has_left_overflow) {
			if (!screenAppendGrayGlyph(wb, TEXT_OVERFLOW_LEFT_UTF8,
			                           sizeof(TEXT_OVERFLOW_LEFT_UTF8) - 1)) {
				return 0;
			}
		} else if (!wbAppend(wb, " ", 1)) {
			return 0;
		}

		if (!screenDrawRenderSlice(wb, row, (int)i, E.coloff, body_cols)) {
			return 0;
		}

		int blame_cols = 0;
		int indicator_col = g_screen_drawing_file_row_origin_col + 1 + rendered_cols;
		int anchor_col = E.coloff + rendered_cols;
		if (!screenAppendGitBlameIndicator(wb, (int)i, E.coloff, indicator_col, anchor_col,
		                                   body_cols - rendered_cols, &blame_cols)) {
			return 0;
		}

		for (int pad = rendered_cols + blame_cols; pad < body_cols; pad++) {
			if (!wbAppend(wb, " ", 1)) {
				return 0;
			}
		}

		if (has_right_overflow) {
			if (!screenAppendGrayGlyph(wb, TEXT_OVERFLOW_RIGHT_UTF8,
			                           sizeof(TEXT_OVERFLOW_RIGHT_UTF8) - 1)) {
				return 0;
			}
		} else if (!wbAppend(wb, " ", 1)) {
			return 0;
		}
		return 1;
	}

	return screenDrawRenderSlice(wb, row, (int)i, E.coloff, text_cols);
}

/* Draws every screen row of the single-pane layout, repainting only rows whose
 * bytes changed since the last frame. Returns the number of rows actually
 * redrawn, or -1 on allocation/write failure. */
static int screenDrawSinglePaneFileRows(struct writeBuf *wb, int drawer_cols, int separator_cols,
                                        int text_cols, int force_full) {
	int file_row_draw_count = 0;
	for (int y = 0; y < E.window_rows; y++) {
		struct writeBuf row_buf = WRITEBUF_INIT;
		if (!editorBuildSinglePaneRowLine(&row_buf, y, drawer_cols, separator_cols,
		                                  text_cols)) {
			wbFree(&row_buf);
			return -1;
		}

		int changed = force_full || y >= g_screen_file_row_frame_cache.row_count ||
		              g_screen_file_row_frame_cache.rows[y] == NULL ||
		              g_screen_file_row_frame_cache.row_lens[y] != row_buf.len ||
		              (row_buf.len != 0 && memcmp(g_screen_file_row_frame_cache.rows[y],
		                                          row_buf.b, row_buf.len) != 0);

		if (changed) {
			if (!editorAppendCursorMove(wb, y + 2, 1) ||
			    !wbAppend(wb, row_buf.b, row_buf.len) ||
			    !screenFileRowFrameCacheStoreRow(y, row_buf.b, row_buf.len)) {
				wbFree(&row_buf);
				return -1;
			}
			file_row_draw_count++;
		}

		wbFree(&row_buf);
	}
	return file_row_draw_count;
}

static int screenDrawRows(struct writeBuf *wb) {
	screenGitBlameIndicatorHitClear();
	editorDrawerClampViewport(E.window_rows);
	(void)editorSyntaxPrepareVisibleRowSpans(E.rowoff, E.window_rows);

	int had_terminal = editorTerminalPaneTreeHasTerminal(E.layout_root);
	if (had_terminal) {
		struct editorPaneNode *prev_focus = E.focused_leaf;
		(void)editorTerminalPanePumpAll(E.layout_root);
		int closed = editorTerminalPaneCloseExitedTabs();
		if (closed > 0 && E.focused_leaf != NULL && E.focused_leaf != prev_focus) {
			(void)editorPaneViewLoadIntoState(&E.focused_leaf->as.leaf.view);
		}
	}

	int drawer_cols = editorDrawerWidthForCols(E.window_cols);
	int separator_cols = editorDrawerSeparatorWidthForCols(E.window_cols);
	struct editorRect leaf_rect = {0};
	int text_cols;
	int has_focused_rect = editorLayoutFocusedLeafRect(&leaf_rect);
	if (has_focused_rect) {
		text_cols = leaf_rect.w;
	} else {
		text_cols = editorDrawerTextViewportCols(E.window_cols);
	}

	int leaf_count = editorPaneTreeLeafCount(E.layout_root);
	int has_terminal = editorTerminalPaneTreeHasTerminal(E.layout_root);
	if ((leaf_count > 1 || has_terminal) && has_focused_rect) {
		struct editorRect viewport;
		if (!editorLayoutEditorViewport(&viewport)) {
			return 0;
		}
		struct editorLeafLayout layout = {0};
		struct editorBorderList borders = {0};
		if (!editorLayoutComputeBorderedInto(E.layout_root, viewport,
		                                     ROTIDE_PANE_BORDER_SIZE, &layout) ||
		    !editorLayoutCollectBorders(E.layout_root, viewport, ROTIDE_PANE_BORDER_SIZE,
		                                &borders)) {
			editorBorderListFree(&borders);
			editorLeafLayoutFree(&layout);
			return 0;
		}
		int ok = editorDrawMultiPaneRows(wb, &layout, &borders, leaf_rect);
		editorBorderListFree(&borders);
		editorLeafLayoutFree(&layout);
		if (ok) {
			screenFileRowFrameCacheClearRowsFrom(0);
			g_screen_file_row_frame_cache.valid = 0;
			g_screen_last_refresh_file_row_draw_count = E.window_rows;
		}
		return ok;
	}
	int force_full = 0;
	if (!g_screen_file_row_frame_cache.valid ||
	    g_screen_file_row_frame_cache.window_rows != E.window_rows ||
	    g_screen_file_row_frame_cache.window_cols != E.window_cols) {
		force_full = 1;
	}

	if (!screenFileRowFrameCacheEnsureCapacity(E.window_rows)) {
		return 0;
	}

	/*
	 * Cells that were covered by the popup overlay during the previous frame are not tracked
	 * in the row cache. Drop those entries so the rows beneath them get repainted now that
	 * the popup may have moved or closed.
	 */
	if (g_screen_popup_prev_row_count > 0) {
		int top_idx = g_screen_popup_prev_screen_top - 2;
		int end_idx = top_idx + g_screen_popup_prev_row_count;
		if (top_idx < 0) {
			top_idx = 0;
		}
		if (end_idx > g_screen_file_row_frame_cache.row_capacity) {
			end_idx = g_screen_file_row_frame_cache.row_capacity;
		}
		for (int i = top_idx; i < end_idx; i++) {
			free(g_screen_file_row_frame_cache.rows[i]);
			g_screen_file_row_frame_cache.rows[i] = NULL;
			g_screen_file_row_frame_cache.row_lens[i] = 0;
		}
	}

	editorPaneSingleBracketMatchUpdate();
	int file_row_draw_count = screenDrawSinglePaneFileRows(wb, drawer_cols, separator_cols,
	                                                       text_cols, force_full);
	editorPaneSingleBracketMatchClear();
	if (file_row_draw_count < 0) {
		return 0;
	}

	if (g_screen_file_row_frame_cache.row_count > E.window_rows) {
		screenFileRowFrameCacheClearRowsFrom(E.window_rows);
	}
	g_screen_file_row_frame_cache.row_count = E.window_rows;
	g_screen_file_row_frame_cache.window_rows = E.window_rows;
	g_screen_file_row_frame_cache.window_cols = E.window_cols;
	g_screen_file_row_frame_cache.valid = 1;
	g_screen_last_refresh_file_row_draw_count = file_row_draw_count;

	return 1;
}

static int screenScrollProgressPercent(void) {
	int visible_rows = E.window_rows > 0 ? E.window_rows : 1;
	long long top = 0;
	long long total = 0;

	if (E.line_wrap_enabled) {
		int body_cols = editorWrapBodyCols();
		for (int i = 0; i < E.numrows; i++) {
			int segment_count = editorWrapSegmentCountForRowIndex(i, body_cols);
			if (i < E.rowoff) {
				top += segment_count;
			}
			total += segment_count;
		}
		if (E.rowoff >= 0 && E.rowoff < E.numrows) {
			int top_segment_count =
			        editorWrapSegmentCountForRowIndex(E.rowoff, body_cols);
			int top_segment = E.wrapoff;
			if (top_segment < 0) {
				top_segment = 0;
			}
			if (top_segment >= top_segment_count) {
				top_segment = top_segment_count - 1;
			}
			top += top_segment;
		}
	} else {
		top = E.rowoff;
		total = E.numrows;
	}

	if (total <= 0) {
		return 0;
	}
	if (total <= visible_rows || top + visible_rows >= total) {
		return 100;
	}

	long long max_top = total - visible_rows;
	if (max_top <= 0) {
		return 100;
	}
	int progress = (int)((top * 100) / max_top);
	if (progress < 0) {
		return 0;
	}
	if (progress > 100) {
		return 100;
	}
	return progress;
}

static const char *screenCursorStyleSequence(enum editorCursorStyle style, int blink_enabled,
                                             size_t *len_out) {
	const char *sequence =
	        blink_enabled ? VT100_CURSOR_BLINKING_BAR_5 : VT100_CURSOR_STEADY_BAR_5;
	switch (style) {
		case EDITOR_CURSOR_STYLE_BLOCK:
			sequence = blink_enabled ? VT100_CURSOR_BLINKING_BLOCK_5
			                         : VT100_CURSOR_STEADY_BLOCK_5;
			break;
		case EDITOR_CURSOR_STYLE_UNDERLINE:
			sequence = blink_enabled ? VT100_CURSOR_BLINKING_UNDERLINE_5
			                         : VT100_CURSOR_STEADY_UNDERLINE_5;
			break;
		case EDITOR_CURSOR_STYLE_BAR:
		default:
			sequence = blink_enabled ? VT100_CURSOR_BLINKING_BAR_5
			                         : VT100_CURSOR_STEADY_BAR_5;
			break;
	}
	if (len_out != NULL) {
		*len_out = strlen(sequence);
	}
	return sequence;
}

static enum editorCursorStyle screenCursorStyleFromVtermShape(int vterm_shape) {
	switch (vterm_shape) {
		case VTERM_PROP_CURSORSHAPE_UNDERLINE:
			return EDITOR_CURSOR_STYLE_UNDERLINE;
		case VTERM_PROP_CURSORSHAPE_BAR_LEFT:
			return EDITOR_CURSOR_STYLE_BAR;
		case VTERM_PROP_CURSORSHAPE_BLOCK:
		default:
			return EDITOR_CURSOR_STYLE_BLOCK;
	}
}

static struct editorTerminalPane *screenFocusedTerminalPane(void) {
	return editorTerminalPaneForPane(E.focused_leaf);
}

static const struct editorLspDiagnostic *screenDiagnosticAtCursor(void) {
	if (E.primary_focus != EDITOR_PRIMARY_FOCUS_TEXT || E.cy < 0 || E.cy >= E.numrows ||
	    E.lsp_diagnostics == NULL || E.lsp_diagnostic_count <= 0) {
		return NULL;
	}
	int row_size = (int)editorDocumentLineLength(E.document, E.cy);
	for (int i = 0; i < E.lsp_diagnostic_count; i++) {
		int start = 0;
		int end = 0;
		if (!screenDiagnosticRangeForRow(&E.lsp_diagnostics[i], E.cy, row_size, &start,
		                                 &end)) {
			continue;
		}
		if (E.cx >= start && E.cx < end) {
			return &E.lsp_diagnostics[i];
		}
	}
	return NULL;
}

static int screenCursorScreenPosition(int *screen_row_out, int *screen_col_out) {
	if (screen_row_out == NULL || screen_col_out == NULL || E.cy < 0 || E.cy >= E.numrows) {
		return 0;
	}
	if (E.line_wrap_enabled) {
		int body_cols = editorWrapBodyCols();
		int cursor_segment = editorWrapCursorSegmentForRx(&E.rows[E.cy], E.rx, body_cols);
		int cursor_distance = 0;
		if (!editorWrappedDistanceForward(E.rowoff, E.wrapoff, E.cy, cursor_segment,
		                                  editorViewportFocusedPaneBodyRows() > 0
		                                          ? editorViewportFocusedPaneBodyRows() - 1
		                                          : 0,
		                                  body_cols, &cursor_distance)) {
			return 0;
		}
		int segment_start_col = 0;
		int segment_indent_cols = 0;
		editorWrapSegmentInfo(&E.rows[E.cy], cursor_segment, body_cols, &segment_start_col,
		                      NULL, &segment_indent_cols);
		int cursor_segment_col = segment_indent_cols + E.rx - segment_start_col;
		if (cursor_segment_col < 0) {
			cursor_segment_col = 0;
		}
		if (cursor_segment_col >= body_cols) {
			cursor_segment_col = body_cols - 1;
		}
		*screen_row_out = cursor_distance;
		*screen_col_out = cursor_segment_col;
		return 1;
	}

	int screen_row = E.cy - E.rowoff;
	int screen_col = E.rx - E.coloff;
	if (screen_row < 0 || screen_row >= editorViewportFocusedPaneBodyRows() || screen_col < 0) {
		return 0;
	}
	*screen_row_out = screen_row;
	*screen_col_out = screen_col;
	return 1;
}

static int screenDrawDiagnosticPopdown(struct writeBuf *wb) {
	if (editorPopupIsVisible()) {
		return 1;
	}
	const struct editorLspDiagnostic *diagnostic = screenDiagnosticAtCursor();
	const char *message =
	        diagnostic != NULL && diagnostic->message != NULL ? diagnostic->message : NULL;

	int screen_row = 0;
	int screen_col = 0;
	if (!screenCursorScreenPosition(&screen_row, &screen_col)) {
		return 1;
	}

	if (!editorDrawDiagnosticPopdownMessage(wb, message, screen_row, screen_col,
	                                        &g_screen_popup_prev_screen_top,
	                                        &g_screen_popup_prev_row_count)) {
		return 0;
	}
	return 1;
}

static int screenDrawPopupOverlay(struct writeBuf *wb) {
	if (!editorPopupIsVisible()) {
		g_screen_popup_prev_screen_top = 0;
		g_screen_popup_prev_row_count = 0;
		return 1;
	}
	int terminal_row = 0;
	int terminal_col = 0;
	int visible_rows = 0;
	int cols = 0;
	int place_above = 0;
	editorPopupComputePlacement(&terminal_row, &terminal_col, &visible_rows, &cols,
	                            &place_above);
	if (visible_rows <= 0 || cols <= 0) {
		g_screen_popup_prev_screen_top = 0;
		g_screen_popup_prev_row_count = 0;
		return 1;
	}
	g_screen_popup_prev_screen_top = terminal_row;
	g_screen_popup_prev_row_count = visible_rows;

	int item_count = editorPopupItemCount();
	int row_offset = E.popup.row_offset;
	int selected_idx = editorPopupSelectedIndex();

	for (int row = 0; row < visible_rows; row++) {
		int item_idx = row_offset + row;
		if (item_idx >= item_count) {
			break;
		}
		char move_buf[32];
		int move_len = snprintf(move_buf, sizeof(move_buf), "\x1b[%d;%dH",
		                        terminal_row + row, terminal_col);
		if (move_len <= 0 || move_len >= (int)sizeof(move_buf)) {
			return 0;
		}
		if (!wbAppend(wb, move_buf, (size_t)move_len)) {
			return 0;
		}
		int is_selected = E.popup.kind != EDITOR_POPUP_KIND_LSP_HOVER &&
		                  E.popup.kind != EDITOR_POPUP_KIND_GIT_BLAME &&
		                  item_idx == selected_idx;
		if (is_selected) {
			if (!editorAppendThemeStyle(wb, EDITOR_THEME_STYLE_SELECTION)) {
				return 0;
			}
		} else {
			if (!editorAppendThemeBackgroundRole(wb, EDITOR_THEME_UI_CURRENT_LINE_BG)) {
				return 0;
			}
		}
		if (!wbAppend(wb, " ", 1)) {
			return 0;
		}
		const char *label =
		        E.popup.items[item_idx].label != NULL ? E.popup.items[item_idx].label : "";
		int wrote = 0;
		if (!editorAppendSanitizedText(wb, label, cols - 2, &wrote)) {
			return 0;
		}
		int padding = cols - 1 - wrote;
		while (padding > 0) {
			if (!wbAppend(wb, " ", 1)) {
				return 0;
			}
			padding--;
		}
		if (!editorAppendThemeReset(wb)) {
			return 0;
		}
	}
	return 1;
}

static int screenAppendFramePreamble(struct writeBuf *wb) {
	struct editorTerminalPane *focused_terminal = screenFocusedTerminalPane();
	enum editorCursorStyle frame_cursor_style = E.cursor_style;
	int frame_cursor_blink = E.cursor_blink_enabled;
	if (focused_terminal != NULL) {
		frame_cursor_style =
		        screenCursorStyleFromVtermShape(focused_terminal->cursor_shape);
		frame_cursor_blink = focused_terminal->cursor_blink != 0;
	} else {
		const struct editorInputSystem *system = editorInputSystemActive();
		if (system != NULL && system->cursor_style != NULL) {
			int style_override = system->cursor_style();
			if (style_override >= 0) {
				frame_cursor_style = (enum editorCursorStyle)style_override;
			}
		}
	}
	size_t cursor_style_len = 0;
	const char *cursor_style_sequence = screenCursorStyleSequence(
	        frame_cursor_style, frame_cursor_blink, &cursor_style_len);

	/* Block cursors need the terminal default color so the cell stays readable. */
	return wbAppend(wb, VT100_HIDE_CURSOR_6, 6) &&
	       (frame_cursor_style == EDITOR_CURSOR_STYLE_BLOCK
	                ? wbAppend(wb, VT100_CURSOR_COLOR_DEFAULT,
	                           strlen(VT100_CURSOR_COLOR_DEFAULT))
	                : editorAppendThemeCursorColor(wb)) &&
	       wbAppend(wb, cursor_style_sequence, cursor_style_len) &&
	       wbAppend(wb, VT100_RESET_CURSOR_POS_3, 3) && editorAppendThemeBaseStyle(wb);
}

static int screenDrawFrameContent(struct writeBuf *wb) {
	int status_row = E.window_rows + 2;
	int message_row = E.window_rows + 3;
	int scroll_progress_percent = screenScrollProgressPercent();
	return editorDrawTabBar(wb) && screenDrawRows(wb) &&
	       editorAppendCursorMove(wb, status_row, 1) &&
	       editorDrawStatusBar(wb, scroll_progress_percent) &&
	       editorAppendCursorMove(wb, message_row, 1) && editorDrawMessageBar(wb) &&
	       screenDrawPopupOverlay(wb) && screenDrawDiagnosticPopdown(wb);
}

static int screenCursorPaneTextStartCol(const struct editorRect *cursor_focused_rect,
                                        int has_focus_rect,
                                        const struct editorTerminalPane *focused_terminal) {
	if (has_focus_rect) {
		if (focused_terminal != NULL) {
			return cursor_focused_rect->x;
		}
		int gutter_cols = editorLineNumberGutterColsForCols(E.window_cols);
		if (gutter_cols > cursor_focused_rect->w) {
			gutter_cols = cursor_focused_rect->w;
		}
		int text_body_cols = cursor_focused_rect->w - gutter_cols;
		int cursor_pane_text_start_col = cursor_focused_rect->x + gutter_cols;
		if (text_body_cols >= 3) {
			cursor_pane_text_start_col += 1;
		}
		return cursor_pane_text_start_col;
	}

	return editorTextBodyStartColForCols(E.window_cols);
}

static void screenApplyTerminalCursor(const struct editorTerminalPane *focused_terminal,
                                      const struct editorRect *cursor_focused_rect, int *cursor_row,
                                      int *cursor_col, int *cursor_visible) {
	VTermPos cursor_pos = {0};
	if (focused_terminal->vt != NULL) {
		vterm_state_get_cursorpos(vterm_obtain_state(focused_terminal->vt), &cursor_pos);
	}
	*cursor_row = cursor_focused_rect->y + cursor_pos.row + 1;
	*cursor_col = cursor_focused_rect->x + cursor_pos.col + 1;
	*cursor_visible = focused_terminal->cursor_visible != 0;
}

static void screenApplyWrappedCursor(int cursor_pane_y, int cursor_pane_text_start_col,
                                     int *cursor_row, int *cursor_col, int *cursor_visible) {
	int body_cols = editorWrapBodyCols();
	int cursor_segment =
	        E.cy < E.numrows ? editorWrapCursorSegmentForRx(&E.rows[E.cy], E.rx, body_cols) : 0;
	int segment_start_col = 0;
	int segment_indent_cols = 0;
	if (E.cy < E.numrows) {
		editorWrapSegmentInfo(&E.rows[E.cy], cursor_segment, body_cols, &segment_start_col,
		                      NULL, &segment_indent_cols);
	}
	int cursor_segment_col = segment_indent_cols + E.rx - segment_start_col;
	if (cursor_segment_col >= body_cols) {
		cursor_segment_col = body_cols - 1;
	}
	if (cursor_segment_col < 0) {
		cursor_segment_col = segment_indent_cols;
		if (cursor_segment_col >= body_cols) {
			cursor_segment_col = body_cols - 1;
		}
	}
	int cursor_distance = 0;
	if (editorWrappedDistanceForward(E.rowoff, E.wrapoff, E.cy, cursor_segment,
	                                 editorViewportFocusedPaneBodyRows() > 0
	                                         ? editorViewportFocusedPaneBodyRows() - 1
	                                         : 0,
	                                 body_cols, &cursor_distance)) {
		*cursor_row = cursor_pane_y + cursor_distance + 1;
	} else {
		*cursor_visible = 0;
	}
	*cursor_col = cursor_pane_text_start_col + cursor_segment_col + 1;
}

static int screenApplyDrawerCursor(int *cursor_row, int *cursor_col, int *cursor_visible) {
	int drawer_width = editorDrawerWidthForCols(E.window_cols);
	if (E.primary_focus != EDITOR_PRIMARY_FOCUS_DRAWER || drawer_width <= 0) {
		return 0;
	}

	if (editorFileSearchIsActive()) {
		*cursor_row = 2;
		*cursor_col = editorFileSearchHeaderCursorCol(drawer_width);
	} else if (editorProjectSearchIsActive()) {
		*cursor_row = 2;
		*cursor_col = editorProjectSearchHeaderCursorCol(drawer_width);
	} else {
		*cursor_visible = 0;
	}
	return 1;
}

static void screenClampTerminalCursor(const struct editorRect *cursor_focused_rect, int *cursor_row,
                                      int *cursor_col) {
	int pane_row_min = cursor_focused_rect->y + 1;
	int pane_row_max = cursor_focused_rect->y + cursor_focused_rect->h;
	int pane_col_min = cursor_focused_rect->x + 1;
	int pane_col_max = cursor_focused_rect->x + cursor_focused_rect->w;
	if (pane_row_max < pane_row_min) {
		pane_row_max = pane_row_min;
	}
	if (pane_col_max < pane_col_min) {
		pane_col_max = pane_col_min;
	}
	if (*cursor_row < pane_row_min) {
		*cursor_row = pane_row_min;
	}
	if (*cursor_row > pane_row_max) {
		*cursor_row = pane_row_max;
	}
	if (*cursor_col < pane_col_min) {
		*cursor_col = pane_col_min;
	}
	if (*cursor_col > pane_col_max) {
		*cursor_col = pane_col_max;
	}
}

static void screenClampTextCursor(const struct editorRect *cursor_focused_rect, int has_focus_rect,
                                  int cursor_pane_text_start_col, int *cursor_row, int *cursor_col,
                                  int *cursor_visible) {
	int text_row_min = has_focus_rect ? cursor_focused_rect->y + 1 : 2;
	int text_row_max = has_focus_rect ? cursor_focused_rect->y + cursor_focused_rect->h
	                                  : E.window_rows + 1;
	if (text_row_max < text_row_min) {
		text_row_max = text_row_min;
	}

	int text_col_min = cursor_pane_text_start_col + 1;
	int text_col_max = text_col_min + editorViewportFocusedPaneTextBodyCols() - 1;
	if (text_col_max < text_col_min) {
		text_col_max = text_col_min;
	}
	if (E.viewport_mode == EDITOR_VIEWPORT_FREE_SCROLL &&
	    (*cursor_row < text_row_min || *cursor_row > text_row_max ||
	     *cursor_col < text_col_min || *cursor_col > text_col_max)) {
		*cursor_visible = 0;
		return;
	}

	if (*cursor_row < text_row_min) {
		*cursor_row = text_row_min;
	}
	if (*cursor_row > text_row_max) {
		*cursor_row = text_row_max;
	}
	if (*cursor_col < text_col_min) {
		*cursor_col = text_col_min;
	}
	if (*cursor_col > text_col_max) {
		*cursor_col = text_col_max;
	}
}

static void screenClampCursor(const struct editorTerminalPane *focused_terminal,
                              const struct editorRect *cursor_focused_rect, int has_focus_rect,
                              int cursor_pane_text_start_col, int *cursor_row, int *cursor_col,
                              int *cursor_visible) {
	if (focused_terminal != NULL && has_focus_rect) {
		screenClampTerminalCursor(cursor_focused_rect, cursor_row, cursor_col);
		return;
	}
	screenClampTextCursor(cursor_focused_rect, has_focus_rect, cursor_pane_text_start_col,
	                      cursor_row, cursor_col, cursor_visible);
}

static int screenAppendCursorMoveAndShow(struct writeBuf *wb, int cursor_row, int cursor_col) {
	if (cursor_row < 1) {
		cursor_row = 1;
	}
	if (cursor_col < 1) {
		cursor_col = 1;
	}

	char buf[32];
	int buflen = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", cursor_row, cursor_col);
	if (buflen > 0 && buflen < (int)sizeof(buf) && !wbAppend(wb, buf, (size_t)buflen)) {
		return 0;
	}

	return wbAppend(wb, VT100_SHOW_CURSOR_6, 6);
}

int editorCursorTerminalPosition(int *terminal_row_out, int *terminal_col_out) {
	struct editorTerminalPane *focused_terminal = screenFocusedTerminalPane();
	if (focused_terminal != NULL) {
		return 0;
	}

	struct editorRect cursor_focused_rect = {0};
	int has_focus_rect = editorLayoutFocusedLeafRect(&cursor_focused_rect);
	int cursor_pane_y = has_focus_rect ? cursor_focused_rect.y : 1;
	int cursor_pane_text_start_col =
	        screenCursorPaneTextStartCol(&cursor_focused_rect, has_focus_rect, NULL);
	int screen_row = 0;
	int screen_col = 0;
	if (!screenCursorScreenPosition(&screen_row, &screen_col)) {
		return 0;
	}
	if (terminal_row_out != NULL) {
		*terminal_row_out = cursor_pane_y + screen_row + 1;
	}
	if (terminal_col_out != NULL) {
		*terminal_col_out = cursor_pane_text_start_col + screen_col + 1;
	}
	return 1;
}

static int screenAppendFrameCursor(struct writeBuf *wb) {
	struct editorTerminalPane *focused_terminal = screenFocusedTerminalPane();

	struct editorRect cursor_focused_rect = {0};
	int has_focus_rect = editorLayoutFocusedLeafRect(&cursor_focused_rect);

	/* Debug Console REPL: render a single cursor at the inline input line ("> "
	 * plus the typed text), like a terminal, rather than the editor cursor at an
	 * empty buffer position (which would double up with the input row). */
	if (focused_terminal == NULL && has_focus_rect &&
	    E.primary_focus != EDITOR_PRIMARY_FOCUS_DRAWER) {
		struct editorDapConsolePane *console = editorDapConsoleForPane(E.focused_leaf);
		if (console != NULL) {
			int row = cursor_focused_rect.y + cursor_focused_rect.h;
			int col = cursor_focused_rect.x + 2 + console->input_len + 1;
			int max_col = cursor_focused_rect.x + cursor_focused_rect.w;
			if (col > max_col) {
				col = max_col;
			}
			return screenAppendCursorMoveAndShow(wb, row, col);
		}
	}

	int cursor_pane_y = has_focus_rect ? cursor_focused_rect.y : 1;
	int cursor_pane_text_start_col = screenCursorPaneTextStartCol(
	        &cursor_focused_rect, has_focus_rect, focused_terminal);

	int cursor_row = cursor_pane_y + (E.cy - E.rowoff) + 1;
	int cursor_col = cursor_pane_text_start_col + (E.rx - E.coloff) + 1;
	int cursor_visible = 1;
	if (has_focus_rect && focused_terminal != NULL) {
		screenApplyTerminalCursor(focused_terminal, &cursor_focused_rect, &cursor_row,
		                          &cursor_col, &cursor_visible);
	}
	if (focused_terminal == NULL && E.line_wrap_enabled) {
		screenApplyWrappedCursor(cursor_pane_y, cursor_pane_text_start_col, &cursor_row,
		                         &cursor_col, &cursor_visible);
	}
	if (!screenApplyDrawerCursor(&cursor_row, &cursor_col, &cursor_visible)) {
		screenClampCursor(focused_terminal, &cursor_focused_rect, has_focus_rect,
		                  cursor_pane_text_start_col, &cursor_row, &cursor_col,
		                  &cursor_visible);
	}
	if (cursor_visible) {
		return screenAppendCursorMoveAndShow(wb, cursor_row, cursor_col);
	}
	return 1;
}

static void screenAbortRefreshOom(struct writeBuf *wb) {
	wbFree(wb);
	editorSetStatusMsg("Out of memory");
}

static void screenAbortRefreshWriteFailure(struct writeBuf *wb) {
	int saved_errno = errno;
	wbFree(wb);
	if (saved_errno != 0) {
		editorSetStatusMsg("Output write failed: %s", strerror(saved_errno));
	} else {
		editorSetStatusMsg("Output write failed");
	}
}

void editorRefreshScreen(void) {
	g_screen_last_refresh_file_row_draw_count = 0;

	struct writeBuf wb = WRITEBUF_INIT;
	if (!screenAppendFramePreamble(&wb) || !screenDrawFrameContent(&wb) ||
	    !screenAppendFrameCursor(&wb)) {
		screenAbortRefreshOom(&wb);
		return;
	}

	if (!wbWriteAllToStdout(&wb)) {
		screenAbortRefreshWriteFailure(&wb);
		return;
	}

	wbFree(&wb);
}

void editorOutputTestResetFrameCache(void) {
	screenFileRowFrameCacheReset();
	g_screen_last_refresh_file_row_draw_count = 0;
}

int editorOutputTestLastRefreshFileRowDrawCount(void) {
	return g_screen_last_refresh_file_row_draw_count;
}
