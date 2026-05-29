#include "input/text_pairs.h"

#include "editing/document_position.h"
#include "editing/edit.h"
#include "editing/history.h"
#include "editing/selection.h"
#include "editing/text_source.h"
#include "language/autocomplete.h"
#include "language/syntax.h"
#include "language/syntax_visible_cache.h"
#include "rotide.h"
#include "text/document.h"
#include "text/row.h"
#include "workspace/tabs.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

int editorByteShouldInsertAsText(int c) {
	if (c < CHAR_MIN || c > CHAR_MAX) {
		return 0;
	}

	unsigned char byte = (unsigned char)c;
	/* Keep non-ASCII bytes verbatim and allow literal Tab; filter other ASCII controls. */
	return byte == '\t' || byte >= 0x80 || !iscntrl(byte);
}

static int textPairsClosingForOpening(int c, char *closing_out) {
	char closing = '\0';

	switch (c) {
		case '(':
			closing = ')';
			break;
		case '[':
			closing = ']';
			break;
		case '{':
			closing = '}';
			break;
		case '"':
		case '\'':
		case '`':
			closing = (char)c;
			break;
		default:
			return 0;
	}

	if (closing_out != NULL) {
		*closing_out = closing;
	}
	return 1;
}

static int textPairsIsClosing(int c) {
	switch (c) {
		case ')':
		case ']':
		case '}':
		case '"':
		case '\'':
		case '`':
			return 1;
		default:
			return 0;
	}
}

static int textPairsNextCharAllowsAutoClose(void) {
	if (E.cy < 0 || E.cy >= E.numrows) {
		return 1;
	}

	struct editorLineView line = {0};
	if (!editorDocumentLineView(E.document, E.cy, &line)) {
		return 1;
	}
	int allow = 1;
	if (E.cx >= 0 && E.cx < line.size) {
		unsigned char next = (unsigned char)line.data[E.cx];
		allow = isspace(next) || strchr(")]}'\"`.,;:", next) != NULL;
	}
	editorLineViewRelease(&line);
	return allow;
}

static int textPairsSetCursorFromOffset(size_t offset) {
	int cy = 0;
	int cx = 0;
	size_t normalized_offset = 0;

	if (!editorBufferOffsetToPos(offset, &cy, &cx)) {
		return 0;
	}
	if (cy < E.numrows) {
		struct editorLineView line = {0};
		if (editorDocumentLineView(E.document, cy, &line)) {
			cx = editorBytesClampCxToCharBoundary(line.data, line.size, cx);
			cx = editorBytesClampCxToClusterBoundary(line.data, line.size, cx);
			editorLineViewRelease(&line);
		}
	} else {
		cx = 0;
	}
	if (!editorBufferPosToOffset(cy, cx, &normalized_offset)) {
		return 0;
	}
	E.cursor_offset = normalized_offset;
	E.cy = cy;
	E.cx = cx;
	return 1;
}

static int textPairsSetCursorFromPosition(int cy, int cx) {
	size_t offset = 0;

	if (cy < 0) {
		cy = 0;
	}
	if (cy > E.numrows) {
		cy = E.numrows;
	}
	if (cy < E.numrows) {
		struct editorLineView line = {0};
		if (editorDocumentLineView(E.document, cy, &line)) {
			cx = editorBytesClampCxToCharBoundary(line.data, line.size, cx);
			cx = editorBytesClampCxToClusterBoundary(line.data, line.size, cx);
			editorLineViewRelease(&line);
		}
	} else {
		cx = 0;
	}
	if (!editorBufferPosToOffset(cy, cx, &offset)) {
		return 0;
	}
	return textPairsSetCursorFromOffset(offset);
}

static void textPairsPinActivePreviewForEdit(void) {
	if (editorTabIsPreviewAt(E.active_tab)) {
		editorTabPinActivePreview();
	}
}

int editorTrySkipOverClosingPair(int c) {
	if (!textPairsIsClosing(c) || E.cy < 0 || E.cy >= E.numrows) {
		return 0;
	}

	struct editorLineView line = {0};
	if (!editorDocumentLineView(E.document, E.cy, &line)) {
		return 0;
	}
	int match = E.cx >= 0 && E.cx < line.size && line.data[E.cx] == (char)c;
	editorLineViewRelease(&line);
	if (!match) {
		return 0;
	}

	editorHistoryBreakGroup();
	editorClearSelectionState();
	(void)textPairsSetCursorFromPosition(E.cy, E.cx + 1);
	return 1;
}

int editorTryAutoClosePair(int c) {
	char closing = '\0';
	if (!textPairsClosingForOpening(c, &closing) || !textPairsNextCharAllowsAutoClose()) {
		return 0;
	}

	size_t start_offset = 0;
	if (!editorBufferPosToOffset(E.cy, E.cx, &start_offset)) {
		return 0;
	}

	char pair[2] = {(char)c, closing};
	editorClearSelectionState();
	textPairsPinActivePreviewForEdit();
	editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
	int dirty_before = E.dirty;
	(void)editorInsertText(pair, sizeof(pair));
	editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, E.dirty != dirty_before);
	if (E.dirty != dirty_before) {
		(void)textPairsSetCursorFromOffset(start_offset + 1);
		editorAutocompleteOnCharInserted(c);
	}
	return 1;
}

static int textPairsBracketPairForByte(char c, char *open_out, char *close_out, int *forward_out) {
	char open = '\0';
	char close = '\0';
	int forward = 1;

	switch (c) {
		case '(':
			open = '(';
			close = ')';
			break;
		case '[':
			open = '[';
			close = ']';
			break;
		case '{':
			open = '{';
			close = '}';
			break;
		case ')':
			open = '(';
			close = ')';
			forward = 0;
			break;
		case ']':
			open = '[';
			close = ']';
			forward = 0;
			break;
		case '}':
			open = '{';
			close = '}';
			forward = 0;
			break;
		default:
			return 0;
	}

	if (open_out != NULL) {
		*open_out = open;
	}
	if (close_out != NULL) {
		*close_out = close;
	}
	if (forward_out != NULL) {
		*forward_out = forward;
	}
	return 1;
}

static int textPairsFindMatchingBracketOffset(const char *text, size_t len, size_t bracket_offset,
                                              char open, char close, int forward,
                                              size_t *match_out) {
	int depth = 0;

	if (text == NULL || bracket_offset >= len || match_out == NULL) {
		return 0;
	}

	if (forward) {
		for (size_t i = bracket_offset; i < len; i++) {
			if (text[i] == open) {
				depth++;
			} else if (text[i] == close) {
				depth--;
				if (depth == 0) {
					*match_out = i;
					return 1;
				}
			}
		}
		return 0;
	}

	size_t i = bracket_offset;
	while (1) {
		if (text[i] == close) {
			depth++;
		} else if (text[i] == open) {
			depth--;
			if (depth == 0) {
				*match_out = i;
				return 1;
			}
		}
		if (i == 0) {
			break;
		}
		i--;
	}
	return 0;
}

enum { TEXT_PAIRS_MATCH_HIGHLIGHT_MAX_SCAN_BYTES = 200000 };

/*
 * Brackets that tree-sitter places inside comments or string literals are not
 * structural, so highlighting their "match" would be misleading (e.g. the `(`
 * in an OCaml `(* comment *)`). When syntax info is available, suppress the
 * highlight for such positions. Languages without tree-sitter fall through and
 * still get plain depth-matched highlighting.
 */
static int textPairsCursorBracketIsInCommentOrString(const struct editorLineView *cursor_line) {
	if (E.syntax_state == NULL || E.syntax_language == EDITOR_SYNTAX_NONE || E.cy < 0 ||
	    E.cy >= E.numrows) {
		return 0;
	}

	struct editorRowSyntaxSpan spans[ROTIDE_MAX_SYNTAX_SPANS_PER_ROW];
	int span_count = 0;
	if (!editorSyntaxRowRenderSpans(E.cy, spans, ROTIDE_MAX_SYNTAX_SPANS_PER_ROW,
	                                &span_count)) {
		return 0;
	}

	int render_idx = editorBytesCxToRenderIdx(cursor_line->data, cursor_line->size,
	                                          E.rows[E.cy].rsize, E.cx);
	enum editorSyntaxHighlightClass cls = EDITOR_SYNTAX_HL_NONE;
	for (int i = 0; i < span_count; i++) {
		if (render_idx >= spans[i].start_render_idx &&
		    render_idx < spans[i].end_render_idx) {
			cls = spans[i].highlight_class;
		}
	}
	return cls == EDITOR_SYNTAX_HL_COMMENT || cls == EDITOR_SYNTAX_HL_STRING;
}

int editorBracketMatchComputeForCursor(int out_rows[2], int out_cols[2]) {
	if (out_rows == NULL || out_cols == NULL || E.document == NULL || E.cy < 0 ||
	    E.cy >= E.numrows || E.cx < 0) {
		return 0;
	}

	struct editorLineView cursor_line = {0};
	if (!editorDocumentLineView(E.document, E.cy, &cursor_line)) {
		return 0;
	}
	char open = '\0';
	char close = '\0';
	int forward = 1;
	int on_bracket =
	        E.cx < cursor_line.size &&
	        textPairsBracketPairForByte(cursor_line.data[E.cx], &open, &close, &forward);
	int suppressed = on_bracket && textPairsCursorBracketIsInCommentOrString(&cursor_line);
	editorLineViewRelease(&cursor_line);
	if (!on_bracket || suppressed) {
		return 0;
	}

	int depth = 0;
	size_t scanned = 0;
	int row = E.cy;
	while (row >= 0 && row < E.numrows) {
		struct editorLineView line = {0};
		if (!editorDocumentLineView(E.document, row, &line)) {
			return 0;
		}
		int col = forward ? (row == E.cy ? E.cx : 0) : (row == E.cy ? E.cx : line.size - 1);
		int matched = 0;
		while (col >= 0 && col < line.size) {
			char c = line.data[col];
			if (forward) {
				if (c == open) {
					depth++;
				} else if (c == close && --depth == 0) {
					matched = 1;
					break;
				}
			} else {
				if (c == close) {
					depth++;
				} else if (c == open && --depth == 0) {
					matched = 1;
					break;
				}
			}
			if (++scanned > TEXT_PAIRS_MATCH_HIGHLIGHT_MAX_SCAN_BYTES) {
				editorLineViewRelease(&line);
				return 0;
			}
			col += forward ? 1 : -1;
		}
		editorLineViewRelease(&line);
		if (matched) {
			out_rows[0] = E.cy;
			out_cols[0] = E.cx;
			out_rows[1] = row;
			out_cols[1] = col;
			return 1;
		}
		row += forward ? 1 : -1;
	}
	return 0;
}

int editorJumpToMatchingBracket(void) {
	struct editorTextSource source = {0};
	size_t cursor_offset = 0;
	size_t text_len = 0;
	size_t bracket_offset = 0;
	size_t match_offset = 0;
	char *text = NULL;
	char open = '\0';
	char close = '\0';
	int forward = 1;

	if (!editorBufferPosToOffset(E.cy, E.cx, &cursor_offset)) {
		editorSetStatusMsg("No bracket near cursor");
		return 0;
	}

	if (!editorBuildActiveTextSource(&source)) {
		editorSetStatusMsg("No bracket near cursor");
		return 0;
	}

	text = editorTextSourceDupRange(&source, 0, source.length, &text_len);
	if (text == NULL && text_len > 0) {
		editorSetStatusMsg("Out of memory");
		return 0;
	}

	if (cursor_offset < text_len &&
	    textPairsBracketPairForByte(text[cursor_offset], &open, &close, &forward)) {
		bracket_offset = cursor_offset;
	} else if (cursor_offset > 0 &&
	           textPairsBracketPairForByte(text[cursor_offset - 1], &open, &close, &forward)) {
		bracket_offset = cursor_offset - 1;
	} else {
		free(text);
		editorSetStatusMsg("No bracket near cursor");
		return 0;
	}

	if (!textPairsFindMatchingBracketOffset(text, text_len, bracket_offset, open, close,
	                                        forward, &match_offset)) {
		free(text);
		editorSetStatusMsg("No matching bracket");
		return 0;
	}

	free(text);
	return textPairsSetCursorFromOffset(match_offset);
}
