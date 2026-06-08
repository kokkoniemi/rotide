#include "config/keymap.h"
#include "editing/buffer_core.h"
#include "editing/document_position.h"
#include "editing/edit.h"
#include "editing/history.h"
#include "editing/selection.h"
#include "input/dispatch.h"
#include "input/input_system.h"
#include "input/text_pairs.h"
#include "rotide.h"
#include "support/alloc.h"
#include "text/document.h"
#include "text/row.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum vimSystemMode {
	VIM_SYSTEM_MODE_NORMAL = 0,
	VIM_SYSTEM_MODE_INSERT,
	VIM_SYSTEM_MODE_VISUAL,
	VIM_SYSTEM_MODE_VISUAL_LINE
};

enum vimSystemMotion {
	VIM_SYSTEM_MOTION_LEFT = 0,
	VIM_SYSTEM_MOTION_DOWN,
	VIM_SYSTEM_MOTION_UP,
	VIM_SYSTEM_MOTION_RIGHT,
	VIM_SYSTEM_MOTION_WORD_FORWARD,
	VIM_SYSTEM_MOTION_WORD_BACKWARD,
	VIM_SYSTEM_MOTION_WORD_END,
	VIM_SYSTEM_MOTION_LINE_START,
	VIM_SYSTEM_MOTION_LINE_END,
	VIM_SYSTEM_MOTION_FIRST_NONBLANK,
	VIM_SYSTEM_MOTION_FIRST_LINE,
	VIM_SYSTEM_MOTION_LAST_LINE
};

enum vimSystemMotionParse {
	VIM_SYSTEM_MOTION_PARSE_NONE = 0,
	VIM_SYSTEM_MOTION_PARSE_PENDING,
	VIM_SYSTEM_MOTION_PARSE_FOUND
};

enum vimSystemOperator {
	VIM_SYSTEM_OPERATOR_NONE = 0,
	VIM_SYSTEM_OPERATOR_DELETE = 'd',
	VIM_SYSTEM_OPERATOR_CHANGE = 'c',
	VIM_SYSTEM_OPERATOR_YANK = 'y'
};

enum vimSystemCharClass { VIM_SYSTEM_CHAR_SPACE = 0, VIM_SYSTEM_CHAR_WORD, VIM_SYSTEM_CHAR_PUNCT };

static enum vimSystemMode vimSystemMode(void) {
	switch (E.input_vim_mode) {
		case VIM_SYSTEM_MODE_INSERT:
			return VIM_SYSTEM_MODE_INSERT;
		case VIM_SYSTEM_MODE_VISUAL:
			return VIM_SYSTEM_MODE_VISUAL;
		case VIM_SYSTEM_MODE_VISUAL_LINE:
			return VIM_SYSTEM_MODE_VISUAL_LINE;
		default:
			return VIM_SYSTEM_MODE_NORMAL;
	}
}

static void vimSystemSetMode(enum vimSystemMode mode) {
	E.input_vim_mode = mode;
	E.input_vim_pending_g = 0;
	E.input_vim_pending_operator = VIM_SYSTEM_OPERATOR_NONE;
	E.input_vim_pending_operator_g = 0;
}

const char *editorVimModeLabel(void) {
	switch (vimSystemMode()) {
		case VIM_SYSTEM_MODE_INSERT:
			return "-- INSERT --";
		case VIM_SYSTEM_MODE_VISUAL:
			return "-- VISUAL --";
		case VIM_SYSTEM_MODE_VISUAL_LINE:
			return "-- VISUAL LINE --";
		default:
			return "-- NORMAL --";
	}
}

static void vimSystemBeginVisual(enum vimSystemMode mode) {
	size_t cursor_offset = E.cursor_offset;
	(void)editorBufferPosToOffset(E.cy, E.cx, &cursor_offset);
	editorColumnSelectionClear();
	E.selection_mode_active = 1;
	E.selection_anchor_offset = cursor_offset;
	vimSystemSetMode(mode);
}

static enum vimSystemCharClass vimSystemClassAt(const struct editorLineView *line, int cx) {
	unsigned char byte = (unsigned char)line->data[cx];

	if (isspace(byte)) {
		return VIM_SYSTEM_CHAR_SPACE;
	}
	if (isalnum(byte) || byte == '_' || byte >= 0x80) {
		return VIM_SYSTEM_CHAR_WORD;
	}
	return VIM_SYSTEM_CHAR_PUNCT;
}

static int vimSystemLineFirstNonblank(int cy) {
	struct editorLineView line = {0};
	int cx = 0;

	if (!editorDocumentLineView(E.document, cy, &line)) {
		return 0;
	}
	while (cx < line.size && isspace((unsigned char)line.data[cx])) {
		int next = editorBytesNextClusterIdx(line.data, line.size, cx);
		if (next <= cx) {
			break;
		}
		cx = next;
	}
	if (cx == line.size) {
		cx = 0;
	}
	editorLineViewRelease(&line);
	return cx;
}

static int vimSystemLineLastCluster(int cy) {
	struct editorLineView line = {0};
	int cx = 0;

	if (!editorDocumentLineView(E.document, cy, &line)) {
		return 0;
	}
	if (line.size > 0) {
		cx = editorBytesPrevClusterIdx(line.data, line.size, line.size);
	}
	editorLineViewRelease(&line);
	return cx;
}

static int vimSystemLineEndCx(int cy) {
	return (int)editorDocumentLineLength(E.document, cy);
}

static void vimSystemClearPendingOperator(void) {
	E.input_vim_pending_operator = VIM_SYSTEM_OPERATOR_NONE;
	E.input_vim_pending_operator_g = 0;
}

static void vimSystemAddEditEffect(int *effects_out) {
	if (effects_out != NULL) {
		*effects_out |= EDITOR_INPUT_KEY_EFFECT_CURSOR_OR_EDIT;
	}
}

static int vimSystemSetCursor(int cy, int cx, int *effects_out) {
	size_t old_offset = E.cursor_offset;
	size_t offset = 0;

	if (!editorBufferPosToOffset(cy, cx, &offset) || !editorSyncCursorFromOffset(offset)) {
		return 0;
	}
	editorHistoryBreakGroup();
	if (effects_out != NULL && E.cursor_offset != old_offset) {
		*effects_out |= EDITOR_INPUT_KEY_EFFECT_CURSOR_OR_EDIT;
	}
	return 1;
}

static int vimSystemSyncCursor(void) {
	size_t offset = 0;

	if (!editorBufferPosToOffset(E.cy, E.cx, &offset)) {
		return 0;
	}
	return editorSyncCursorFromOffset(offset);
}

static int vimSystemWordForwardTarget(int *cy_out, int *cx_out) {
	int original_cy = E.cy;
	int original_cx = E.cx;
	int cy = E.cy;
	int cx = E.cx;

	while (cy < E.numrows) {
		struct editorLineView line = {0};
		if (!editorDocumentLineView(E.document, cy, &line)) {
			return 0;
		}
		if (cx < line.size && vimSystemClassAt(&line, cx) != VIM_SYSTEM_CHAR_SPACE) {
			enum vimSystemCharClass char_class = vimSystemClassAt(&line, cx);
			do {
				int next = editorBytesNextClusterIdx(line.data, line.size, cx);
				if (next <= cx) {
					break;
				}
				cx = next;
			} while (cx < line.size && vimSystemClassAt(&line, cx) == char_class);
		}
		while (cx < line.size && vimSystemClassAt(&line, cx) == VIM_SYSTEM_CHAR_SPACE) {
			int next = editorBytesNextClusterIdx(line.data, line.size, cx);
			if (next <= cx) {
				break;
			}
			cx = next;
		}
		if (cx < line.size) {
			editorLineViewRelease(&line);
			*cy_out = cy;
			*cx_out = cx;
			return 1;
		}
		editorLineViewRelease(&line);
		cy++;
		cx = 0;
	}

	*cy_out = original_cy;
	*cx_out = original_cx;
	return 1;
}

static int vimSystemWordBackwardTarget(int *cy_out, int *cx_out) {
	int cy = E.cy;
	int cx = E.cx;

	while (cy >= 0) {
		struct editorLineView line = {0};
		if (!editorDocumentLineView(E.document, cy, &line)) {
			return 0;
		}
		if (cx > 0) {
			cx = editorBytesPrevClusterIdx(line.data, line.size, cx);
			if (vimSystemClassAt(&line, cx) != VIM_SYSTEM_CHAR_SPACE) {
				enum vimSystemCharClass char_class = vimSystemClassAt(&line, cx);
				while (cx > 0) {
					int prev =
					        editorBytesPrevClusterIdx(line.data, line.size, cx);
					if (prev >= cx ||
					    vimSystemClassAt(&line, prev) != char_class) {
						break;
					}
					cx = prev;
				}
				editorLineViewRelease(&line);
				*cy_out = cy;
				*cx_out = cx;
				return 1;
			}
			editorLineViewRelease(&line);
			continue;
		}
		editorLineViewRelease(&line);
		if (cy == 0) {
			break;
		}
		cy--;
		cx = (int)editorDocumentLineLength(E.document, cy);
	}

	*cy_out = 0;
	*cx_out = 0;
	return 1;
}

static int vimSystemWordEndTarget(int *cy_out, int *cx_out) {
	int original_cy = E.cy;
	int original_cx = E.cx;
	int cy = E.cy;
	int cx = E.cx;
	int find_next_run = 0;

	while (cy < E.numrows) {
		struct editorLineView line = {0};
		if (!editorDocumentLineView(E.document, cy, &line)) {
			return 0;
		}
		if (!find_next_run && cx < line.size &&
		    vimSystemClassAt(&line, cx) != VIM_SYSTEM_CHAR_SPACE) {
			enum vimSystemCharClass char_class = vimSystemClassAt(&line, cx);
			int end = cx;
			int next = editorBytesNextClusterIdx(line.data, line.size, cx);
			while (next < line.size && vimSystemClassAt(&line, next) == char_class) {
				end = next;
				next = editorBytesNextClusterIdx(line.data, line.size, next);
			}
			if (end != cx) {
				editorLineViewRelease(&line);
				*cy_out = cy;
				*cx_out = end;
				return 1;
			}
			cx = next;
		}
		find_next_run = 1;
		while (cx < line.size && vimSystemClassAt(&line, cx) == VIM_SYSTEM_CHAR_SPACE) {
			int next = editorBytesNextClusterIdx(line.data, line.size, cx);
			if (next <= cx) {
				break;
			}
			cx = next;
		}
		if (cx < line.size) {
			enum vimSystemCharClass char_class = vimSystemClassAt(&line, cx);
			int end = cx;
			int next = editorBytesNextClusterIdx(line.data, line.size, cx);
			while (next < line.size && vimSystemClassAt(&line, next) == char_class) {
				end = next;
				next = editorBytesNextClusterIdx(line.data, line.size, next);
			}
			editorLineViewRelease(&line);
			*cy_out = cy;
			*cx_out = end;
			return 1;
		}
		editorLineViewRelease(&line);
		cy++;
		cx = 0;
	}

	*cy_out = original_cy;
	*cx_out = original_cx;
	return 1;
}

static int vimSystemMotionTarget(enum vimSystemMotion motion, int *cy_out, int *cx_out) {
	int cy = E.cy;
	int cx = E.cx;

	if (!vimSystemSyncCursor() || E.numrows == 0) {
		return 0;
	}
	cy = E.cy;
	cx = E.cx;

	switch (motion) {
		case VIM_SYSTEM_MOTION_LEFT:
			if (cx > 0) {
				struct editorLineView line = {0};
				if (editorDocumentLineView(E.document, cy, &line)) {
					cx = editorBytesPrevClusterIdx(line.data, line.size, cx);
					editorLineViewRelease(&line);
				}
			}
			break;
		case VIM_SYSTEM_MOTION_RIGHT: {
			struct editorLineView line = {0};
			if (editorDocumentLineView(E.document, cy, &line)) {
				int next = editorBytesNextClusterIdx(line.data, line.size, cx);
				if (next < line.size) {
					cx = next;
				}
				editorLineViewRelease(&line);
			}
			break;
		}
		case VIM_SYSTEM_MOTION_UP:
		case VIM_SYSTEM_MOTION_DOWN: {
			int target_cy = cy + (motion == VIM_SYSTEM_MOTION_UP ? -1 : 1);
			if (target_cy >= 0 && target_cy < E.numrows) {
				struct editorLineView current = {0};
				struct editorLineView target = {0};
				int target_rx = 0;
				if (editorDocumentLineView(E.document, cy, &current)) {
					target_rx =
					        editorBytesCxToRx(current.data, current.size, cx);
					editorLineViewRelease(&current);
				}
				if (editorDocumentLineView(E.document, target_cy, &target)) {
					cx = editorBytesRxToCx(target.data, target.size, target_rx);
					if (cx == target.size && target.size > 0) {
						cx = editorBytesPrevClusterIdx(target.data,
						                               target.size, cx);
					}
					editorLineViewRelease(&target);
					cy = target_cy;
				}
			}
			break;
		}
		case VIM_SYSTEM_MOTION_WORD_FORWARD:
			return vimSystemWordForwardTarget(cy_out, cx_out);
		case VIM_SYSTEM_MOTION_WORD_BACKWARD:
			return vimSystemWordBackwardTarget(cy_out, cx_out);
		case VIM_SYSTEM_MOTION_WORD_END:
			return vimSystemWordEndTarget(cy_out, cx_out);
		case VIM_SYSTEM_MOTION_LINE_START:
			cx = 0;
			break;
		case VIM_SYSTEM_MOTION_LINE_END:
			cx = vimSystemLineLastCluster(cy);
			break;
		case VIM_SYSTEM_MOTION_FIRST_NONBLANK:
			cx = vimSystemLineFirstNonblank(cy);
			break;
		case VIM_SYSTEM_MOTION_FIRST_LINE:
			cy = 0;
			cx = vimSystemLineFirstNonblank(cy);
			break;
		case VIM_SYSTEM_MOTION_LAST_LINE:
			cy = E.numrows - 1;
			cx = vimSystemLineFirstNonblank(cy);
			break;
	}

	*cy_out = cy;
	*cx_out = cx;
	return 1;
}

static int vimSystemMotionIsLinewise(enum vimSystemMotion motion) {
	return motion == VIM_SYSTEM_MOTION_DOWN || motion == VIM_SYSTEM_MOTION_UP ||
	       motion == VIM_SYSTEM_MOTION_FIRST_LINE || motion == VIM_SYSTEM_MOTION_LAST_LINE;
}

static int vimSystemMotionIsInclusive(enum vimSystemMotion motion) {
	return motion == VIM_SYSTEM_MOTION_WORD_END || motion == VIM_SYSTEM_MOTION_LINE_END;
}

static int vimSystemPositionComesBefore(int left_cy, int left_cx, int right_cy, int right_cx) {
	if (left_cy != right_cy) {
		return left_cy < right_cy;
	}
	return left_cx < right_cx;
}

static int vimSystemPositionAfterCluster(int cy, int cx, int *cy_out, int *cx_out) {
	struct editorLineView line = {0};
	int next = cx;

	if (!editorDocumentLineView(E.document, cy, &line)) {
		return 0;
	}
	if (cx < 0 || cx >= line.size) {
		editorLineViewRelease(&line);
		return 0;
	}
	next = editorBytesNextClusterIdx(line.data, line.size, cx);
	if (next <= cx || next > line.size) {
		editorLineViewRelease(&line);
		return 0;
	}
	editorLineViewRelease(&line);
	*cy_out = cy;
	*cx_out = next;
	return 1;
}

static void vimSystemClampNormalCursor(void) {
	struct editorLineView line = {0};
	int cx = E.cx;

	if (!vimSystemSyncCursor() || E.cy < 0 || E.cy >= E.numrows ||
	    !editorDocumentLineView(E.document, E.cy, &line)) {
		return;
	}
	if (line.size > 0 && cx >= line.size) {
		cx = editorBytesPrevClusterIdx(line.data, line.size, line.size);
	}
	editorLineViewRelease(&line);
	(void)vimSystemSetCursor(E.cy, cx, NULL);
}

static int vimSystemMakeRange(int start_cy, int start_cx, int end_cy, int end_cx,
                              struct editorSelectionRange *range_out) {
	if (range_out == NULL) {
		return 0;
	}
	if (start_cy == end_cy && start_cx == end_cx) {
		return 0;
	}
	if (vimSystemPositionComesBefore(end_cy, end_cx, start_cy, start_cx)) {
		range_out->start_cy = end_cy;
		range_out->start_cx = end_cx;
		range_out->end_cy = start_cy;
		range_out->end_cx = start_cx;
		return 1;
	}
	range_out->start_cy = start_cy;
	range_out->start_cx = start_cx;
	range_out->end_cy = end_cy;
	range_out->end_cx = end_cx;
	return 1;
}

static int vimSystemLineRange(int start_cy, int end_cy, struct editorSelectionRange *range_out) {
	if (range_out == NULL || E.numrows <= 0) {
		return 0;
	}
	if (start_cy > end_cy) {
		int tmp = start_cy;
		start_cy = end_cy;
		end_cy = tmp;
	}
	if (start_cy < 0) {
		start_cy = 0;
	}
	if (end_cy >= E.numrows) {
		end_cy = E.numrows - 1;
	}
	range_out->start_cy = start_cy;
	range_out->start_cx = 0;
	if (end_cy + 1 < E.numrows) {
		range_out->end_cy = end_cy + 1;
		range_out->end_cx = 0;
	} else {
		range_out->end_cy = end_cy;
		range_out->end_cx = vimSystemLineEndCx(end_cy);
	}
	return range_out->start_cy != range_out->end_cy || range_out->start_cx != range_out->end_cx;
}

static int vimSystemLineRangeLastRow(const struct editorSelectionRange *range) {
	if (range == NULL) {
		return 0;
	}
	if (range->end_cx == 0 && range->end_cy > range->start_cy) {
		return range->end_cy - 1;
	}
	return range->end_cy;
}

static int vimSystemMotionRange(enum vimSystemMotion motion, struct editorSelectionRange *range_out,
                                int *linewise_out) {
	int start_cy = E.cy;
	int start_cx = E.cx;
	int target_cy = E.cy;
	int target_cx = E.cx;

	if (linewise_out != NULL) {
		*linewise_out = 0;
	}
	if (!vimSystemMotionTarget(motion, &target_cy, &target_cx)) {
		return 0;
	}
	if (vimSystemMotionIsLinewise(motion)) {
		if (linewise_out != NULL) {
			*linewise_out = 1;
		}
		return vimSystemLineRange(start_cy, target_cy, range_out);
	}
	if (vimSystemMotionIsInclusive(motion) &&
	    !vimSystemPositionComesBefore(target_cy, target_cx, start_cy, start_cx)) {
		if (!vimSystemPositionAfterCluster(target_cy, target_cx, &target_cy, &target_cx)) {
			return 0;
		}
	}
	return vimSystemMakeRange(start_cy, start_cx, target_cy, target_cx, range_out);
}

static int vimSystemApplyMotion(enum vimSystemMotion motion, int *effects_out) {
	int cy = E.cy;
	int cx = E.cx;

	if (!vimSystemMotionTarget(motion, &cy, &cx)) {
		return 0;
	}
	(void)vimSystemSetCursor(cy, cx, effects_out);
	return 1;
}

static enum vimSystemMotionParse vimSystemParseMotionKey(int c, int *pending_g,
                                                         enum vimSystemMotion *motion_out) {
	enum vimSystemMotion motion;

	if (pending_g != NULL && *pending_g) {
		*pending_g = 0;
		if (c == 'g') {
			*motion_out = VIM_SYSTEM_MOTION_FIRST_LINE;
			return VIM_SYSTEM_MOTION_PARSE_FOUND;
		}
		return VIM_SYSTEM_MOTION_PARSE_NONE;
	}

	switch (c) {
		case 'h':
			motion = VIM_SYSTEM_MOTION_LEFT;
			break;
		case 'j':
			motion = VIM_SYSTEM_MOTION_DOWN;
			break;
		case 'k':
			motion = VIM_SYSTEM_MOTION_UP;
			break;
		case 'l':
			motion = VIM_SYSTEM_MOTION_RIGHT;
			break;
		case 'w':
			motion = VIM_SYSTEM_MOTION_WORD_FORWARD;
			break;
		case 'b':
			motion = VIM_SYSTEM_MOTION_WORD_BACKWARD;
			break;
		case 'e':
			motion = VIM_SYSTEM_MOTION_WORD_END;
			break;
		case '0':
			motion = VIM_SYSTEM_MOTION_LINE_START;
			break;
		case '$':
			motion = VIM_SYSTEM_MOTION_LINE_END;
			break;
		case '^':
			motion = VIM_SYSTEM_MOTION_FIRST_NONBLANK;
			break;
		case 'G':
			motion = VIM_SYSTEM_MOTION_LAST_LINE;
			break;
		case 'g':
			if (pending_g != NULL) {
				*pending_g = 1;
			}
			return VIM_SYSTEM_MOTION_PARSE_PENDING;
		default:
			return VIM_SYSTEM_MOTION_PARSE_NONE;
	}
	*motion_out = motion;
	return VIM_SYSTEM_MOTION_PARSE_FOUND;
}

static int vimSystemTryMotionKey(int c, int *effects_out) {
	enum vimSystemMotion motion;
	enum vimSystemMotionParse parsed =
	        vimSystemParseMotionKey(c, &E.input_vim_pending_g, &motion);

	if (parsed == VIM_SYSTEM_MOTION_PARSE_PENDING) {
		return 1;
	}
	if (parsed != VIM_SYSTEM_MOTION_PARSE_FOUND) {
		return 0;
	}
	return vimSystemApplyMotion(motion, effects_out);
}

static int vimSystemYankRange(const struct editorSelectionRange *range, int linewise) {
	char *text = NULL;
	size_t len = 0;
	int extracted = editorExtractRangeText(range, &text, &len);

	if (extracted <= 0) {
		return extracted;
	}
	if (!editorClipboardSet(text, len)) {
		free(text);
		return -1;
	}
	free(text);
	E.input_vim_register_linewise = linewise ? 1 : 0;
	return 1;
}

static int vimSystemYankLines(int start_cy, int end_cy) {
	size_t total = 0;
	char *text = NULL;
	size_t pos = 0;

	if (E.numrows <= 0) {
		return 0;
	}
	if (start_cy > end_cy) {
		int tmp = start_cy;
		start_cy = end_cy;
		end_cy = tmp;
	}
	if (start_cy < 0) {
		start_cy = 0;
	}
	if (end_cy >= E.numrows) {
		end_cy = E.numrows - 1;
	}
	for (int cy = start_cy; cy <= end_cy; cy++) {
		size_t line_len = editorDocumentLineLength(E.document, cy);
		if (total > ROTIDE_MAX_TEXT_BYTES || line_len > ROTIDE_MAX_TEXT_BYTES - total - 1) {
			editorSetOperationTooLargeStatus();
			return -1;
		}
		total += line_len + 1;
	}
	text = editorMalloc(total + 1);
	if (text == NULL) {
		editorSetAllocFailureStatus();
		return -1;
	}
	for (int cy = start_cy; cy <= end_cy; cy++) {
		struct editorLineView line = {0};
		if (!editorDocumentLineView(E.document, cy, &line)) {
			free(text);
			return -1;
		}
		if (line.size > 0) {
			memcpy(text + pos, line.data, (size_t)line.size);
			pos += (size_t)line.size;
		}
		text[pos++] = '\n';
		editorLineViewRelease(&line);
	}
	text[pos] = '\0';
	if (!editorClipboardSet(text, pos)) {
		free(text);
		return -1;
	}
	free(text);
	E.input_vim_register_linewise = 1;
	return 1;
}

static int vimSystemChangeLineRange(int start_cy, int end_cy, int *effects_out) {
	struct editorSelectionRange range;
	int dirty_before = E.dirty;
	int changed = 0;

	if (start_cy > end_cy) {
		int tmp = start_cy;
		start_cy = end_cy;
		end_cy = tmp;
	}
	if (!vimSystemYankLines(start_cy, end_cy)) {
		return 0;
	}
	range.start_cy = start_cy;
	range.start_cx = 0;
	range.end_cy = end_cy;
	range.end_cx = vimSystemLineEndCx(end_cy);
	editorHistoryBeginEdit(EDITOR_EDIT_DELETE_TEXT);
	changed = editorDeleteRange(&range);
	editorHistoryCommitEdit(EDITOR_EDIT_DELETE_TEXT, E.dirty != dirty_before);
	editorClearSelectionState();
	if (changed > 0) {
		(void)vimSystemSetCursor(start_cy, 0, effects_out);
		vimSystemSetMode(VIM_SYSTEM_MODE_INSERT);
		vimSystemAddEditEffect(effects_out);
	}
	return changed > 0;
}

static int vimSystemApplyOperatorToRange(enum vimSystemOperator op,
                                         const struct editorSelectionRange *range, int linewise,
                                         int *effects_out) {
	int dirty_before = E.dirty;
	int changed = 0;

	if (op == VIM_SYSTEM_OPERATOR_YANK) {
		if (linewise) {
			return vimSystemYankLines(range->start_cy,
			                          vimSystemLineRangeLastRow(range)) > 0;
		}
		return vimSystemYankRange(range, 0) > 0;
	}
	if (linewise && !vimSystemYankLines(range->start_cy, vimSystemLineRangeLastRow(range))) {
		return 0;
	}
	if (!linewise && !vimSystemYankRange(range, 0)) {
		return 0;
	}

	editorHistoryBeginEdit(EDITOR_EDIT_DELETE_TEXT);
	changed = editorDeleteRange(range);
	editorHistoryCommitEdit(EDITOR_EDIT_DELETE_TEXT, E.dirty != dirty_before);
	editorClearSelectionState();
	if (changed > 0) {
		vimSystemAddEditEffect(effects_out);
		if (op == VIM_SYSTEM_OPERATOR_CHANGE) {
			vimSystemSetMode(VIM_SYSTEM_MODE_INSERT);
		} else {
			vimSystemSetMode(VIM_SYSTEM_MODE_NORMAL);
			vimSystemClampNormalCursor();
		}
	}
	return changed > 0;
}

static int vimSystemApplyOperatorMotion(enum vimSystemOperator op, enum vimSystemMotion motion,
                                        int *effects_out) {
	struct editorSelectionRange range;
	int linewise = 0;

	if (!vimSystemMotionRange(motion, &range, &linewise)) {
		return 0;
	}
	return vimSystemApplyOperatorToRange(op, &range, linewise, effects_out);
}

static int vimSystemApplyLineOperator(enum vimSystemOperator op, int *effects_out) {
	struct editorSelectionRange range;

	if (op == VIM_SYSTEM_OPERATOR_CHANGE) {
		return vimSystemChangeLineRange(E.cy, E.cy, effects_out);
	}
	if (op == VIM_SYSTEM_OPERATOR_YANK) {
		return vimSystemYankLines(E.cy, E.cy) > 0;
	}
	if (!vimSystemLineRange(E.cy, E.cy, &range)) {
		return 0;
	}
	return vimSystemApplyOperatorToRange(op, &range, 1, effects_out);
}

static int vimSystemHandlePendingOperatorKey(int c, int *effects_out) {
	enum vimSystemOperator op = (enum vimSystemOperator)E.input_vim_pending_operator;
	enum vimSystemMotion motion;
	enum vimSystemMotionParse parsed;

	if (c == '\x1b') {
		vimSystemClearPendingOperator();
		return 0;
	}
	if (c == E.input_vim_pending_operator) {
		vimSystemClearPendingOperator();
		(void)vimSystemApplyLineOperator(op, effects_out);
		return 0;
	}

	parsed = vimSystemParseMotionKey(c, &E.input_vim_pending_operator_g, &motion);
	if (parsed == VIM_SYSTEM_MOTION_PARSE_PENDING) {
		return 0;
	}
	vimSystemClearPendingOperator();
	if (parsed == VIM_SYSTEM_MOTION_PARSE_FOUND) {
		(void)vimSystemApplyOperatorMotion(op, motion, effects_out);
	}
	return 0;
}

static int vimSystemDeleteUnderCursor(int *effects_out) {
	struct editorSelectionRange range;
	int end_cy = E.cy;
	int end_cx = E.cx;

	if (E.numrows <= 0 || !vimSystemPositionAfterCluster(E.cy, E.cx, &end_cy, &end_cx)) {
		return 0;
	}
	if (!vimSystemMakeRange(E.cy, E.cx, end_cy, end_cx, &range)) {
		return 0;
	}
	return vimSystemApplyOperatorToRange(VIM_SYSTEM_OPERATOR_DELETE, &range, 0, effects_out);
}

static int vimSystemDeleteToLineEnd(enum vimSystemOperator op, int *effects_out) {
	struct editorSelectionRange range;
	int end_cx = vimSystemLineEndCx(E.cy);

	if (!vimSystemMakeRange(E.cy, E.cx, E.cy, end_cx, &range)) {
		return 0;
	}
	return vimSystemApplyOperatorToRange(op, &range, 0, effects_out);
}

static int vimSystemPasteDefaultRegister(int after, int *effects_out) {
	size_t clip_len = 0;
	const char *clip = editorClipboardGet(&clip_len);
	const char *paste_text = clip;
	size_t paste_len = clip_len;
	char *owned_text = NULL;
	int dirty_before = E.dirty;
	int pasted = 0;

	if (clip == NULL || clip_len == 0) {
		editorSetStatusMsg("Clipboard is empty");
		return 0;
	}

	if (E.input_vim_register_linewise) {
		int insert_cy = after ? E.cy + 1 : E.cy;
		if (insert_cy < E.numrows) {
			(void)vimSystemSetCursor(insert_cy, 0, NULL);
		} else if (E.numrows > 0) {
			size_t body_len = clip_len;
			if (body_len > 0 && clip[body_len - 1] == '\n') {
				body_len--;
			}
			owned_text = editorMalloc(body_len + 2);
			if (owned_text == NULL) {
				editorSetAllocFailureStatus();
				return 0;
			}
			owned_text[0] = '\n';
			if (body_len > 0) {
				memcpy(owned_text + 1, clip, body_len);
			}
			owned_text[body_len + 1] = '\0';
			paste_text = owned_text;
			paste_len = body_len + 1;
			(void)vimSystemSetCursor(E.numrows - 1, vimSystemLineEndCx(E.numrows - 1),
			                         NULL);
		}
	} else if (after) {
		int end_cy = E.cy;
		int end_cx = E.cx;
		if (vimSystemPositionAfterCluster(E.cy, E.cx, &end_cy, &end_cx)) {
			(void)vimSystemSetCursor(end_cy, end_cx, NULL);
		}
	}

	editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
	pasted = editorInsertText(paste_text, paste_len);
	editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, E.dirty != dirty_before);
	editorHistoryBreakGroup();
	free(owned_text);
	if (pasted) {
		vimSystemAddEditEffect(effects_out);
		vimSystemSetMode(VIM_SYSTEM_MODE_NORMAL);
		vimSystemClampNormalCursor();
	}
	return pasted;
}

static int vimSystemTryMappedActionKey(int c, int *effects_out, int *return_now_out) {
	enum editorAction action = EDITOR_ACTION_COUNT;

	if (return_now_out != NULL) {
		*return_now_out = 0;
	}
	if (editorKeymapLookupAction(&E.keymap, c, &action)) {
		int mapped_effects = EDITOR_INPUT_KEY_EFFECT_NONE;
		if (editorDispatchProcessMappedAction(action, &mapped_effects)) {
			if (return_now_out != NULL) {
				*return_now_out = 1;
			}
			return 1;
		}
		if (effects_out != NULL) {
			*effects_out |= mapped_effects;
		}
		return 1;
	}
	return 0;
}

static int vimSystemEnterInsertWithAction(enum editorAction action, int *effects_out) {
	int mapped_effects = EDITOR_INPUT_KEY_EFFECT_NONE;
	(void)editorDispatchProcessMappedAction(action, &mapped_effects);
	if (effects_out != NULL) {
		*effects_out |= mapped_effects;
	}
	vimSystemSetMode(VIM_SYSTEM_MODE_INSERT);
	return 0;
}

static int vimSystemHandleInsertKey(int c, int *effects_out) {
	if (c == '\x1b') {
		vimSystemSetMode(VIM_SYSTEM_MODE_NORMAL);
		return 0;
	}
	int return_now = 0;
	if (vimSystemTryMappedActionKey(c, effects_out, &return_now)) {
		return return_now;
	}
	if (editorByteShouldInsertAsText(c)) {
		editorDispatchHandleTextByte(c, effects_out);
	}
	return 0;
}

static int vimSystemHandleNormalKey(int c, int *effects_out) {
	if (E.input_vim_pending_operator != VIM_SYSTEM_OPERATOR_NONE) {
		return vimSystemHandlePendingOperatorKey(c, effects_out);
	}
	if (vimSystemTryMotionKey(c, effects_out)) {
		return 0;
	}
	switch (c) {
		case '\x1b':
			vimSystemSetMode(VIM_SYSTEM_MODE_NORMAL);
			return 0;
		case 'i':
			vimSystemSetMode(VIM_SYSTEM_MODE_INSERT);
			return 0;
		case 'a':
			return vimSystemEnterInsertWithAction(EDITOR_ACTION_MOVE_RIGHT,
			                                      effects_out);
		case 'I':
			return vimSystemEnterInsertWithAction(EDITOR_ACTION_MOVE_HOME, effects_out);
		case 'A':
			return vimSystemEnterInsertWithAction(EDITOR_ACTION_MOVE_END, effects_out);
		case 'o':
			(void)vimSystemEnterInsertWithAction(EDITOR_ACTION_MOVE_END, effects_out);
			return vimSystemEnterInsertWithAction(EDITOR_ACTION_NEWLINE, effects_out);
		case 'O':
			(void)vimSystemEnterInsertWithAction(EDITOR_ACTION_MOVE_HOME, effects_out);
			(void)vimSystemEnterInsertWithAction(EDITOR_ACTION_NEWLINE, effects_out);
			return vimSystemEnterInsertWithAction(EDITOR_ACTION_MOVE_UP, effects_out);
		case 'x':
			(void)vimSystemDeleteUnderCursor(effects_out);
			return 0;
		case 'd':
		case 'c':
		case 'y':
			E.input_vim_pending_operator = c;
			E.input_vim_pending_operator_g = 0;
			return 0;
		case 'D':
			(void)vimSystemDeleteToLineEnd(VIM_SYSTEM_OPERATOR_DELETE, effects_out);
			return 0;
		case 'C':
			(void)vimSystemDeleteToLineEnd(VIM_SYSTEM_OPERATOR_CHANGE, effects_out);
			return 0;
		case 'Y':
			(void)vimSystemYankLines(E.cy, E.cy);
			return 0;
		case 'p':
			(void)vimSystemPasteDefaultRegister(1, effects_out);
			return 0;
		case 'P':
			(void)vimSystemPasteDefaultRegister(0, effects_out);
			return 0;
		case 'v':
			vimSystemBeginVisual(VIM_SYSTEM_MODE_VISUAL);
			return 0;
		case 'V':
			vimSystemBeginVisual(VIM_SYSTEM_MODE_VISUAL_LINE);
			return 0;
		default:
			int return_now = 0;
			if (vimSystemTryMappedActionKey(c, effects_out, &return_now)) {
				return return_now;
			}
			return 0;
	}
}

static int vimSystemVisualRange(struct editorSelectionRange *range_out, int *linewise_out) {
	if (linewise_out != NULL) {
		*linewise_out = vimSystemMode() == VIM_SYSTEM_MODE_VISUAL_LINE;
	}
	if (vimSystemMode() == VIM_SYSTEM_MODE_VISUAL_LINE) {
		int anchor_cy = 0;
		int anchor_cx = 0;
		if (!editorBufferOffsetToPos(E.selection_anchor_offset, &anchor_cy, &anchor_cx)) {
			return 0;
		}
		(void)anchor_cx;
		return vimSystemLineRange(anchor_cy, E.cy, range_out);
	}
	return editorGetSelectionRange(range_out);
}

static int vimSystemHandleVisualKey(int c, int *effects_out) {
	if (c == '\x1b') {
		editorClearSelectionState();
		vimSystemSetMode(VIM_SYSTEM_MODE_NORMAL);
		return 0;
	}
	if (c == 'v') {
		vimSystemBeginVisual(VIM_SYSTEM_MODE_VISUAL);
		return 0;
	}
	if (c == 'V') {
		vimSystemBeginVisual(VIM_SYSTEM_MODE_VISUAL_LINE);
		return 0;
	}
	if (vimSystemTryMotionKey(c, effects_out)) {
		return 0;
	}
	if (c == 'd' || c == 'c' || c == 'y') {
		struct editorSelectionRange range;
		int linewise = 0;
		if (vimSystemVisualRange(&range, &linewise)) {
			(void)vimSystemApplyOperatorToRange((enum vimSystemOperator)c, &range,
			                                    linewise, effects_out);
		}
		editorClearSelectionState();
		if (c != 'c') {
			vimSystemSetMode(VIM_SYSTEM_MODE_NORMAL);
		}
		return 0;
	}
	if (c == 'p' || c == 'P') {
		struct editorSelectionRange range;
		if (vimSystemVisualRange(&range, NULL)) {
			editorHistoryBeginEdit(EDITOR_EDIT_INSERT_TEXT);
			int dirty_before = E.dirty;
			size_t clip_len = 0;
			const char *clip = editorClipboardGet(&clip_len);
			int pasted = 0;
			if (clip != NULL && clip_len > 0) {
				pasted = editorReplaceRange(&range, clip, clip_len) > 0;
			}
			editorHistoryCommitEdit(EDITOR_EDIT_INSERT_TEXT, E.dirty != dirty_before);
			editorHistoryBreakGroup();
			if (pasted) {
				vimSystemAddEditEffect(effects_out);
				vimSystemClampNormalCursor();
			}
		}
		editorClearSelectionState();
		vimSystemSetMode(VIM_SYSTEM_MODE_NORMAL);
		return 0;
	}
	int return_now = 0;
	if (vimSystemTryMappedActionKey(c, effects_out, &return_now)) {
		return return_now;
	}
	return 0;
}

static int vimSystemHandleKey(int c, int *effects_out) {
	switch (vimSystemMode()) {
		case VIM_SYSTEM_MODE_INSERT:
			return vimSystemHandleInsertKey(c, effects_out);
		case VIM_SYSTEM_MODE_VISUAL:
		case VIM_SYSTEM_MODE_VISUAL_LINE:
			return vimSystemHandleVisualKey(c, effects_out);
		default:
			return vimSystemHandleNormalKey(c, effects_out);
	}
}

static int vimSystemResolveCommand(const char *name, int *command_id_out) {
	enum editorAction action = EDITOR_ACTION_COUNT;
	if (!editorKeymapResolveActionName(name, &action)) {
		return 0;
	}
	if (command_id_out != NULL) {
		*command_id_out = action;
	}
	return 1;
}

static int vimSystemBindKey(const char *mode, const char *name, int key) {
	enum editorAction action = EDITOR_ACTION_COUNT;

	if (mode != NULL && mode[0] != '\0' && strcmp(mode, "default") != 0) {
		return 0;
	}
	if (!editorKeymapResolveActionName(name, &action)) {
		return 0;
	}
	return editorKeymapBindAction(&E.keymap, action, key);
}

static void vimSystemStatusSegment(char *buf, size_t bufsize) {
	if (bufsize != 0) {
		(void)snprintf(buf, bufsize, "%s", editorVimModeLabel());
	}
}

static int vimSystemOnActivate(void) {
	vimSystemSetMode(VIM_SYSTEM_MODE_NORMAL);
	return 1;
}

static void vimSystemReset(void) {
	vimSystemSetMode(VIM_SYSTEM_MODE_NORMAL);
}

const struct editorInputSystem editorVimInputSystem = {
        .id = "vim",
        .on_activate = vimSystemOnActivate,
        .on_deactivate = NULL,
        .handle_key = vimSystemHandleKey,
        .resolve_command = vimSystemResolveCommand,
        .bind_key = vimSystemBindKey,
        .status_segment = vimSystemStatusSegment,
        .reset = vimSystemReset,
};
