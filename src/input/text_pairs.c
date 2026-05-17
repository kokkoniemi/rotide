#include "input/text_pairs.h"

#include "editing/buffer_core.h"
#include "editing/edit.h"
#include "editing/history.h"
#include "editing/selection.h"
#include "language/autocomplete.h"
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

static int editorPairClosingForOpening(int c, char *closing_out) {
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

static int editorPairIsClosing(int c) {
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

static int editorPairNextCharAllowsAutoClose(void) {
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

static int editorTextPairsSetCursorFromOffset(size_t offset) {
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

static int editorTextPairsSetCursorFromPosition(int cy, int cx) {
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
	return editorTextPairsSetCursorFromOffset(offset);
}

static void editorTextPairsPinActivePreviewForEdit(void) {
	if (editorTabIsPreviewAt(E.active_tab)) {
		editorTabPinActivePreview();
	}
}

int editorTrySkipOverClosingPair(int c) {
	if (!editorPairIsClosing(c) || E.cy < 0 || E.cy >= E.numrows) {
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
	(void)editorTextPairsSetCursorFromPosition(E.cy, E.cx + 1);
	return 1;
}

int editorTryAutoClosePair(int c) {
	char closing = '\0';
	if (!editorPairClosingForOpening(c, &closing) || !editorPairNextCharAllowsAutoClose()) {
		return 0;
	}

	size_t start_offset = 0;
	if (!editorBufferPosToOffset(E.cy, E.cx, &start_offset)) {
		return 0;
	}

	char pair[2] = {(char)c, closing};
	editorClearSelectionState();
	editorTextPairsPinActivePreviewForEdit();
	editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
	int dirty_before = E.dirty;
	(void)editorInsertText(pair, sizeof(pair));
	editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, E.dirty != dirty_before);
	if (E.dirty != dirty_before) {
		(void)editorTextPairsSetCursorFromOffset(start_offset + 1);
		editorAutocompleteOnCharInserted(c);
	}
	return 1;
}

static int editorBracketPairForByte(char c, char *open_out, char *close_out, int *forward_out) {
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

static int editorFindMatchingBracketOffset(const char *text, size_t len, size_t bracket_offset,
		char open, char close, int forward, size_t *match_out) {
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
			editorBracketPairForByte(text[cursor_offset], &open, &close, &forward)) {
		bracket_offset = cursor_offset;
	} else if (cursor_offset > 0 &&
			editorBracketPairForByte(text[cursor_offset - 1], &open, &close, &forward)) {
		bracket_offset = cursor_offset - 1;
	} else {
		free(text);
		editorSetStatusMsg("No bracket near cursor");
		return 0;
	}

	if (!editorFindMatchingBracketOffset(text, text_len, bracket_offset, open, close, forward,
				&match_offset)) {
		free(text);
		editorSetStatusMsg("No matching bracket");
		return 0;
	}

	free(text);
	return editorTextPairsSetCursorFromOffset(match_offset);
}
