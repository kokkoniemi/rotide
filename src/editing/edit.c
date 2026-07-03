#include "editing/edit.h"

#include "config/common.h"
#include "config/runtime_config.h"
#include "editing/buffer_core.h"
#include "editing/document_position.h"
#include "editing/edit_pipeline.h"
#include "editing/text_source.h"
#include "input/prompt.h"
#include "language/lsp.h"
#include "language/syntax.h"
#include "rotide.h"
#include "support/alloc.h"
#include "support/file_io.h"
#include "support/save_syscalls.h"
#include "support/size_utils.h"
#include "text/document.h"
#include "text/row.h"
#include "workspace/git.h"
#include "workspace/git_view.h"
#include "workspace/tabs.h"
#include "workspace/watch.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define NEWLINE_CHAR_WIDTH 1
#define BINARY_DETECT_SAMPLE_BYTES 8192

static int editFileStreamLooksBinary(FILE *fp, int *binary_out) {
	unsigned char buf[BINARY_DETECT_SAMPLE_BYTES];

	if (fp == NULL || binary_out == NULL) {
		return 0;
	}
	*binary_out = 0;

	while (1) {
		size_t bytes_read = fread(buf, 1, sizeof(buf), fp);
		if (ferror(fp)) {
			return 0;
		}
		for (size_t i = 0; i < bytes_read; i++) {
			if (buf[i] == '\0') {
				*binary_out = 1;
				break;
			}
		}
		if (*binary_out || bytes_read < sizeof(buf)) {
			break;
		}
	}
	if (fseek(fp, 0, SEEK_SET) != 0) {
		return 0;
	}
	return 1;
}

static int editCheckOpenFileStream(const char *filename, FILE **fp_out) {
	FILE *fp = NULL;
	int is_binary = 0;

	if (fp_out == NULL) {
		return 0;
	}
	*fp_out = NULL;
	if (filename == NULL || filename[0] == '\0') {
		editorSetStatusMsg("No file selected");
		return 0;
	}

	fp = fopen(filename, "rb");
	if (fp == NULL) {
		editorSetStatusMsg("Unable to open file: %s", strerror(errno));
		return 0;
	}
	if (!editFileStreamLooksBinary(fp, &is_binary)) {
		(void)fclose(fp);
		editorSetStatusMsg("Unable to inspect file: %s", strerror(errno));
		return 0;
	}
	if (is_binary) {
		(void)fclose(fp);
		editorSetStatusMsg("Binary files are not supported");
		return 0;
	}

	*fp_out = fp;
	return 1;
}

int editorFilePathLooksBinary(const char *filename, int *binary_out) {
	FILE *fp = fopen(filename, "rb");
	if (fp == NULL || binary_out == NULL) {
		if (binary_out != NULL) {
			*binary_out = 0;
		}
		return 0;
	}
	if (!editFileStreamLooksBinary(fp, binary_out)) {
		(void)fclose(fp);
		return 0;
	}
	(void)fclose(fp);
	return 1;
}

int editorFileCanOpen(const char *filename) {
	FILE *fp = NULL;
	if (!editCheckOpenFileStream(filename, &fp)) {
		return 0;
	}
	(void)fclose(fp);
	return 1;
}

static int editReadNormalizedFileToText(FILE *fp, char **text_out, size_t *len_out) {
	char *line = NULL;
	size_t line_cap = 0;
	ssize_t line_len = 0;
	char *text = NULL;
	size_t text_len = 0;

	if (text_out == NULL || len_out == NULL || fp == NULL) {
		return 0;
	}
	*text_out = NULL;
	*len_out = 0;

	while ((line_len = getline(&line, &line_cap, fp)) != -1) {
		size_t normalized_len = 0;
		size_t row_total = 0;
		size_t next_total = 0;
		size_t next_cap = 0;
		char *grown = NULL;

		if (!editorSsizeToSize(line_len, &normalized_len)) {
			editorSetFileTooLargeStatus();
			break;
		}
		while (normalized_len > 0 &&
		       (line[normalized_len - 1] == '\n' || line[normalized_len - 1] == '\r')) {
			normalized_len--;
		}

		if (!editorSizeAdd(normalized_len, NEWLINE_CHAR_WIDTH, &row_total) ||
		    !editorSizeAdd(text_len, row_total, &next_total) ||
		    next_total > ROTIDE_MAX_TEXT_BYTES ||
		    !editorSizeAdd(next_total, 1, &next_cap)) {
			editorSetFileTooLargeStatus();
			break;
		}

		grown = editorRealloc(text, next_cap);
		if (grown == NULL) {
			free(text);
			free(line);
			editorSetAllocFailureStatus();
			return 0;
		}
		text = grown;
		if (normalized_len > 0) {
			memcpy(text + text_len, line, normalized_len);
		}
		text[text_len + normalized_len] = '\n';
		text[next_total] = '\0';
		text_len = next_total;
	}

	free(line);
	if (text == NULL) {
		text = editorMalloc(1);
		if (text == NULL) {
			editorSetAllocFailureStatus();
			return 0;
		}
		text[0] = '\0';
	}

	*text_out = text;
	*len_out = text_len;
	return 1;
}

int editorReadFileToText(const char *filename, char **text_out, size_t *len_out) {
	FILE *fp = NULL;
	int ok = 0;

	if (text_out == NULL || len_out == NULL) {
		return 0;
	}
	*text_out = NULL;
	*len_out = 0;

	if (!editCheckOpenFileStream(filename, &fp)) {
		return 0;
	}
	ok = editReadNormalizedFileToText(fp, text_out, len_out);
	(void)fclose(fp);
	return ok;
}

int editorInsertText(const char *text, size_t len) {
	int insert_cx = 0;
	size_t start_offset = 0;
	size_t after_offset = 0;
	int dirty_delta = 0;
	int sim_cy = 0;
	int sim_numrows = 0;

	if (len == 0) {
		return 0;
	}
	if (text == NULL || len > ROTIDE_MAX_TEXT_BYTES || E.cy < 0 || E.cy > E.numrows) {
		editorSetOperationTooLargeStatus();
		return 0;
	}
	if (E.cy < E.numrows) {
		struct editorLineView line = {0};
		if (editorDocumentLineView(E.document, E.cy, &line)) {
			insert_cx = editorBytesClampCxToClusterBoundary(line.data, line.size, E.cx);
			editorLineViewRelease(&line);
		} else {
			insert_cx = E.cx;
		}
	} else {
		insert_cx = 0;
	}
	if (!editorBufferPosToOffset(E.cy, insert_cx, &start_offset) ||
	    !editorSizeAdd(start_offset, len, &after_offset) ||
	    after_offset > ROTIDE_MAX_TEXT_BYTES) {
		editorSetOperationTooLargeStatus();
		return 0;
	}

	sim_cy = E.cy;
	sim_numrows = E.numrows;
	for (size_t i = 0; i < len; i++) {
		if (text[i] == '\n') {
			dirty_delta++;
			sim_numrows++;
			sim_cy++;
			continue;
		}
		dirty_delta++;
		if (sim_cy == sim_numrows) {
			dirty_delta++;
			sim_numrows++;
		}
	}
	if (dirty_delta <= 0 || E.dirty > INT_MAX - dirty_delta) {
		editorSetOperationTooLargeStatus();
		return 0;
	}

	struct editorDocumentEdit edit = {.kind = EDITOR_EDIT_INSERT_TEXT,
	                                  .start_offset = start_offset,
	                                  .old_len = 0,
	                                  .new_text = text,
	                                  .new_len = len,
	                                  .before_cursor_offset = start_offset,
	                                  .after_cursor_offset = after_offset,
	                                  .before_dirty = E.dirty,
	                                  .after_dirty = E.dirty + dirty_delta};
	return editorApplyDocumentEdit(&edit);
}

static int editEffectiveIndentWidth(void) {
	if (E.indent_width >= 1 && E.indent_width <= ROTIDE_INDENT_WIDTH_MAX) {
		return E.indent_width;
	}
	return ROTIDE_INDENT_WIDTH_DEFAULT;
}

static size_t editIndentPrefixColumnsBytes(const char *bytes, int size, int limit_cx) {
	if (bytes == NULL || limit_cx <= 0) {
		return 0;
	}
	if (limit_cx > size) {
		limit_cx = size;
	}

	size_t cols = 0;
	size_t width = (size_t)editEffectiveIndentWidth();
	for (int i = 0; i < limit_cx; i++) {
		if (bytes[i] == ' ') {
			cols++;
			continue;
		}
		if (bytes[i] == '\t') {
			size_t remainder = cols % width;
			cols += remainder == 0 ? width : width - remainder;
			continue;
		}
		break;
	}
	return cols;
}

static int editBuildIndentString(size_t cols, char **indent_out, size_t *indent_len_out) {
	if (indent_out == NULL || indent_len_out == NULL) {
		return 0;
	}
	*indent_out = NULL;
	*indent_len_out = 0;
	if (cols == 0) {
		return 1;
	}

	size_t width = (size_t)editEffectiveIndentWidth();
	size_t tabs = E.indent_use_tabs ? cols / width : 0;
	size_t spaces = E.indent_use_tabs ? cols % width : cols;
	size_t len = 0;
	if (!editorSizeAdd(tabs, spaces, &len) || len > ROTIDE_MAX_TEXT_BYTES) {
		editorSetOperationTooLargeStatus();
		return 0;
	}

	char *indent = editorMalloc(len + 1);
	if (indent == NULL) {
		editorSetAllocFailureStatus();
		return 0;
	}
	memset(indent, '\t', tabs);
	memset(indent + tabs, ' ', spaces);
	indent[len] = '\0';
	*indent_out = indent;
	*indent_len_out = len;
	return 1;
}

static int editBuildIndentForLine(int row_idx, int limit_cx, char **indent_out,
                                  size_t *indent_len_out) {
	if (indent_out == NULL || indent_len_out == NULL) {
		return 0;
	}
	*indent_out = NULL;
	*indent_len_out = 0;
	if (!E.auto_indent_enabled || row_idx < 0 || row_idx >= E.numrows) {
		return 1;
	}
	struct editorLineView row_view = {0};
	if (!editorDocumentLineView(E.document, row_idx, &row_view)) {
		return 0;
	}
	int clamped_cx =
	        editorBytesClampCxToClusterBoundary(row_view.data, row_view.size, limit_cx);
	size_t cols = editIndentPrefixColumnsBytes(row_view.data, row_view.size, clamped_cx);
	editorLineViewRelease(&row_view);
	int anchor_row = 0;
	int extra_levels = 0;
	if (editorSyntaxStateSuggestIndentAnchor(E.syntax_state, row_idx, clamped_cx, &anchor_row,
	                                         &extra_levels) &&
	    anchor_row >= 0 && anchor_row < E.numrows && extra_levels > 0) {
		struct editorLineView anchor_view = {0};
		if (!editorDocumentLineView(E.document, anchor_row, &anchor_view)) {
			return 0;
		}
		size_t syntax_cols = editIndentPrefixColumnsBytes(
		        anchor_view.data, anchor_view.size, anchor_view.size);
		editorLineViewRelease(&anchor_view);
		size_t extra_cols = 0;
		size_t total_cols = 0;
		if (!editorSizeMul((size_t)extra_levels, (size_t)editEffectiveIndentWidth(),
		                   &extra_cols) ||
		    !editorSizeAdd(syntax_cols, extra_cols, &total_cols)) {
			editorSetOperationTooLargeStatus();
			return 0;
		}
		if (total_cols > cols) {
			cols = total_cols;
		}
	}
	return editBuildIndentString(cols, indent_out, indent_len_out);
}

int editorBuildAutoIndentedText(const char *text, size_t len, int indent_cy, int indent_cx,
                                char **text_out, size_t *len_out) {
	if (text_out == NULL || len_out == NULL) {
		return -1;
	}
	*text_out = NULL;
	*len_out = 0;
	if (text == NULL || len == 0) {
		return 0;
	}
	if (!E.auto_indent_enabled) {
		return 0;
	}

	int has_newline = 0;
	size_t extra_lines = 0;
	for (size_t i = 0; i < len; i++) {
		if (text[i] == '\n') {
			has_newline = 1;
			if (i + 1 < len && text[i + 1] != '\n' && text[i + 1] != '\r') {
				extra_lines++;
			}
		}
	}
	if (!has_newline || extra_lines == 0) {
		return 0;
	}

	char *indent = NULL;
	size_t indent_len = 0;
	if (!editBuildIndentForLine(indent_cy, indent_cx, &indent, &indent_len)) {
		return -1;
	}
	if (indent_len == 0) {
		free(indent);
		return 0;
	}

	size_t extra_len = 0;
	size_t total_len = 0;
	if (!editorSizeMul(indent_len, extra_lines, &extra_len) ||
	    !editorSizeAdd(len, extra_len, &total_len) || total_len > ROTIDE_MAX_TEXT_BYTES) {
		free(indent);
		editorSetOperationTooLargeStatus();
		return -1;
	}

	char *out = editorMalloc(total_len + 1);
	if (out == NULL) {
		free(indent);
		editorSetAllocFailureStatus();
		return -1;
	}

	size_t out_idx = 0;
	for (size_t i = 0; i < len; i++) {
		out[out_idx++] = text[i];
		if (text[i] == '\n' && i + 1 < len && text[i + 1] != '\n' && text[i + 1] != '\r') {
			memcpy(out + out_idx, indent, indent_len);
			out_idx += indent_len;
		}
	}
	out[out_idx] = '\0';
	*text_out = out;
	*len_out = out_idx;
	free(indent);
	return 1;
}

void editorInsertChar(int c) {
	int insert_cx = 0;
	size_t start_offset = 0;
	char inserted_text[2] = {(char)c, '\n'};
	size_t inserted_len = 1;
	int dirty_delta = 1;

	if (E.cy < 0 || E.cy > E.numrows) {
		return;
	}
	if (E.cy < E.numrows) {
		/*
		 * Terminal UTF-8 input arrives byte-by-byte, so insertion needs to preserve
		 * in-progress multibyte sequences instead of snapping back to a cluster boundary.
		 */
		struct editorLineView line = {0};
		if (editorDocumentLineView(E.document, E.cy, &line)) {
			insert_cx = editorBytesClampCxToCharBoundary(line.data, line.size, E.cx);
			editorLineViewRelease(&line);
		} else {
			insert_cx = E.cx;
		}
	} else {
		insert_cx = 0;
	}
	if (!editorBufferPosToOffset(E.cy, insert_cx, &start_offset)) {
		return;
	}
	if (E.cy == E.numrows) {
		inserted_len = 2;
		dirty_delta = 2;
	}

	struct editorDocumentEdit edit = {.kind = EDITOR_EDIT_INSERT_TEXT,
	                                  .start_offset = start_offset,
	                                  .old_len = 0,
	                                  .new_text = inserted_text,
	                                  .new_len = inserted_len,
	                                  .before_cursor_offset = start_offset,
	                                  .after_cursor_offset = start_offset + 1,
	                                  .before_dirty = E.dirty,
	                                  .after_dirty = E.dirty + dirty_delta};
	if (editorApplyDocumentEdit(&edit)) {
		(void)editorSyncCursorFromOffsetByteBoundary(start_offset + 1);
	}
}

void editorInsertNewline(void) {
	int split_idx = 0;
	if (E.cy < E.numrows) {
		struct editorLineView line = {0};
		if (editorDocumentLineView(E.document, E.cy, &line)) {
			split_idx = editorBytesClampCxToClusterBoundary(line.data, line.size, E.cx);
			editorLineViewRelease(&line);
		} else {
			split_idx = E.cx;
		}
	}

	size_t start_offset = 0;
	if (!editorBufferPosToOffset(E.cy, split_idx, &start_offset)) {
		return;
	}

	char *indent = NULL;
	size_t indent_len = 0;
	if (!editBuildIndentForLine(E.cy, split_idx, &indent, &indent_len)) {
		return;
	}
	size_t inserted_len = 0;
	size_t after_offset = 0;
	if (!editorSizeAdd(1, indent_len, &inserted_len) ||
	    !editorSizeAdd(start_offset, inserted_len, &after_offset) ||
	    after_offset > ROTIDE_MAX_TEXT_BYTES) {
		free(indent);
		editorSetOperationTooLargeStatus();
		return;
	}
	const char *inserted_text = "\n";
	char *allocated_text = NULL;
	if (indent_len > 0) {
		allocated_text = editorMalloc(inserted_len + 1);
		if (allocated_text == NULL) {
			free(indent);
			editorSetAllocFailureStatus();
			return;
		}
		allocated_text[0] = '\n';
		memcpy(allocated_text + 1, indent, indent_len);
		allocated_text[inserted_len] = '\0';
		inserted_text = allocated_text;
	}
	if (inserted_len > (size_t)INT_MAX || E.dirty > INT_MAX - (int)inserted_len) {
		free(allocated_text);
		free(indent);
		editorSetOperationTooLargeStatus();
		return;
	}
	int dirty_delta = (int)inserted_len;

	struct editorDocumentEdit edit = {.kind = EDITOR_EDIT_NEWLINE,
	                                  .start_offset = start_offset,
	                                  .old_len = 0,
	                                  .new_text = inserted_text,
	                                  .new_len = inserted_len,
	                                  .before_cursor_offset = start_offset,
	                                  .after_cursor_offset = after_offset,
	                                  .before_dirty = E.dirty,
	                                  .after_dirty = E.dirty + dirty_delta};
	(void)editorApplyDocumentEdit(&edit);
	free(allocated_text);
	free(indent);
}

void editorDelChar(void) {
	if (E.cy == E.numrows || (E.cx == 0 && E.cy == 0)) {
		return;
	}
	size_t before_cursor_offset = 0;
	size_t start_offset = 0;
	size_t end_offset = 0;
	size_t old_len = 0;
	int dirty_delta = 1;

	if (!editorBufferPosToOffset(E.cy, E.cx, &before_cursor_offset)) {
		before_cursor_offset = 0;
	}

	if (E.cx > 0) {
		struct editorLineView line = {0};
		if (!editorDocumentLineView(E.document, E.cy, &line)) {
			return;
		}
		int cur_cx = editorBytesClampCxToClusterBoundary(line.data, line.size, E.cx);
		int prev_cx = editorBytesPrevClusterIdx(line.data, line.size, cur_cx);
		editorLineViewRelease(&line);
		if (!editorBufferPosToOffset(E.cy, prev_cx, &start_offset) ||
		    !editorBufferPosToOffset(E.cy, cur_cx, &end_offset) ||
		    end_offset <= start_offset) {
			return;
		}
	} else {
		int merge_col = (int)editorDocumentLineLength(E.document, E.cy - 1);
		if (!editorBufferPosToOffset(E.cy - 1, merge_col, &start_offset) ||
		    !editorBufferPosToOffset(E.cy, 0, &end_offset) || end_offset <= start_offset) {
			return;
		}
		dirty_delta = 2;
	}

	old_len = end_offset - start_offset;
	struct editorDocumentEdit edit = {.kind = EDITOR_EDIT_DELETE_TEXT,
	                                  .start_offset = start_offset,
	                                  .old_len = old_len,
	                                  .new_text = "",
	                                  .new_len = 0,
	                                  .before_cursor_offset = before_cursor_offset,
	                                  .after_cursor_offset = start_offset,
	                                  .before_dirty = E.dirty,
	                                  .after_dirty = E.dirty + dirty_delta};
	(void)editorApplyDocumentEdit(&edit);
}

static int g_edit_open_defer_lsp = 0;

void editorOpenSetDeferLsp(int defer) {
	g_edit_open_defer_lsp = defer ? 1 : 0;
}

int editorOpen(const char *filename) {
	int was_preview = E.is_preview;
	FILE *fp = NULL;
	char *filename_copy = NULL;
	char *text = NULL;
	size_t text_len = 0;
	struct editorDocument document;
	int document_inited = 0;
	int ok = 0;

	filename_copy = strdup(filename != NULL ? filename : "");
	if (filename_copy == NULL) {
		editorSetAllocFailureStatus();
		return 0;
	}

	if (!editCheckOpenFileStream(filename, &fp)) {
		goto cleanup;
	}
	if (!editReadNormalizedFileToText(fp, &text, &text_len)) {
		goto cleanup;
	}
	(void)fclose(fp);
	fp = NULL;

	editorDocumentInit(&document);
	document_inited = 1;
	if (!editorDocumentResetFromString(&document, text, text_len)) {
		editorSetAllocFailureStatus();
		goto cleanup;
	}

	editorLspNotifyDidClose(E.filename, E.syntax_language, &E.lsp_doc_open, &E.lsp_doc_version);
	editorLspNotifyEslintDidClose(E.filename, E.syntax_language, &E.lsp_eslint_doc_open,
	                              &E.lsp_eslint_doc_version);
	editorFreeActiveBufferState();
	E.tab_kind = EDITOR_TAB_FILE;
	E.is_preview = was_preview;
	E.filename = filename_copy;
	filename_copy = NULL;
	if (!editorRestoreActiveFromDocument(&document, 0, 0, 0, 1)) {
		goto cleanup;
	}
	editorWatchRefreshActiveBaseline();
	if (!g_edit_open_defer_lsp) {
		(void)editorLspEnsureDocumentOpen(E.filename, E.syntax_language, &E.lsp_doc_open,
		                                  &E.lsp_doc_version, text != NULL ? text : "",
		                                  text_len);
		(void)editorLspEnsureEslintDocumentOpen(
		        E.filename, E.syntax_language, &E.lsp_eslint_doc_open,
		        &E.lsp_eslint_doc_version, text != NULL ? text : "", text_len);
	}
	ok = 1;

cleanup:
	if (fp != NULL) {
		(void)fclose(fp);
	}
	if (document_inited) {
		editorDocumentFree(&document);
	}
	free(filename_copy);
	free(text);
	return ok;
}

void editorSetStatusMsg(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	(void)vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
	va_end(ap);
	E.statusmsg_time = time(NULL);
}

static int editWriteAll(int fd, const char *buf, size_t len) {
	size_t total = 0;
	while (total < len) {
		ssize_t written = write(fd, buf + total, len - total);
		if (written == -1) {
			if (errno == EINTR) {
				continue;
			}
			return -1;
		}
		if (written == 0) {
			errno = EIO;
			return -1;
		}
		total += (size_t)written;
	}
	return 0;
}

static int editSaveCleanupOnError(int *fd, int *dir_fd, const char *tmp_path, int tmp_created,
                                  int tmp_renamed, int *cleanup_errno) {
	int first_cleanup_errno = 0;

	if (*fd != -1) {
		if (editorSaveClose(*fd) == -1 && first_cleanup_errno == 0) {
			first_cleanup_errno = errno;
		}
		*fd = -1;
	}
	if (*dir_fd != -1) {
		if (editorSaveClose(*dir_fd) == -1 && first_cleanup_errno == 0) {
			first_cleanup_errno = errno;
		}
		*dir_fd = -1;
	}

	if (tmp_path != NULL && tmp_created && !tmp_renamed) {
		if (editorSaveUnlink(tmp_path) == -1 && errno != ENOENT &&
		    first_cleanup_errno == 0) {
			first_cleanup_errno = errno;
		}
	}

	if (cleanup_errno != NULL) {
		*cleanup_errno = first_cleanup_errno;
	}
	return first_cleanup_errno == 0 ? 0 : -1;
}

static const char *editSaveFailureClass(int errnum) {
	switch (errnum) {
		case EACCES:
		case EPERM:
			return "permission denied";
		case ENOENT:
		case ENOTDIR:
			return "missing path";
		case EROFS:
			return "read-only filesystem";
		case ENOSPC:
#ifdef EDQUOT
		case EDQUOT:
#endif
			return "no space left";
		default:
			return "system error";
	}
}

static void editSetSaveFailureStatus(int saved_errno, int cleanup_errno) {
	const char *error_class = editSaveFailureClass(saved_errno);
	const char *error_text = strerror(saved_errno);

	if (cleanup_errno != 0) {
		editorSetStatusMsg("Save failed: %s (%s); cleanup failed (%s)", error_class,
		                   error_text, strerror(cleanup_errno));
		return;
	}

	editorSetStatusMsg("Save failed: %s (%s)", error_class, error_text);
}

static mode_t editDefaultCreateMode(void) {
	mode_t mask = umask(0);
	umask(mask);
	return 0644 & ~mask;
}

static void editMaybePromptReloadSettingsAfterSave(const char *filename) {
	if (!editorConfigPathIsGlobalConfig(filename)) {
		return;
	}
	if (editorPromptYesNo("Reload settings now? [y/N] %s")) {
		editorConfigReloadConfiguredSettings();
		return;
	}
	editorSetStatusMsg("Settings reload skipped");
}

static int editSaveWriteToTempPath(const char *filename, char *tmp_path, const char *buf,
                                   size_t len, mode_t mode, int *saved_errno_out,
                                   int *cleanup_errno_out) {
	int fd = -1;
	int dir_fd = -1;
	int tmp_created = 0;
	int tmp_renamed = 0;

	fd = mkstemp(tmp_path);
	if (fd == -1) {
		goto err;
	}
	tmp_created = 1;
	if (fchmod(fd, mode) == -1) {
		goto err;
	}
	if (editWriteAll(fd, buf, len) == -1) {
		goto err;
	}
	if (editorSaveFsync(fd) == -1) {
		goto err;
	}
	if (editorSaveClose(fd) == -1) {
		fd = -1;
		goto err;
	}
	fd = -1;
	if (editorSaveRename(tmp_path, filename) == -1) {
		goto err;
	}
	tmp_renamed = 1;

	dir_fd = editorOpenParentDirForTarget(filename);
	if (dir_fd == -1) {
		goto err;
	}
	if (editorSaveFsync(dir_fd) == -1) {
		goto err;
	}
	if (editorSaveClose(dir_fd) == -1) {
		dir_fd = -1;
		goto err;
	}
	return 1;

err: {
	int saved_errno = errno;
	int cleanup_errno = 0;
	(void)editSaveCleanupOnError(&fd, &dir_fd, tmp_path, tmp_created, tmp_renamed,
	                             &cleanup_errno);
	if (saved_errno_out != NULL) {
		*saved_errno_out = saved_errno;
	}
	if (cleanup_errno_out != NULL) {
		*cleanup_errno_out = cleanup_errno;
	}
	return 0;
}
}

static void editSaveAfterCommit(const char *filename, size_t len) {
	editorLspNotifyDidSaveActive();
	editorGitRefresh();
	if (editorConfigPathIsGlobalConfig(filename)) {
		editMaybePromptReloadSettingsAfterSave(filename);
	} else {
		editorSetStatusMsg("%zu bytes written to disk", len);
	}
}

void editorSave(void) {
	if (editorActiveTabIsUnsupportedFile()) {
		editorSetStatusMsg("Unsupported files cannot be saved");
		return;
	}
	if (editorActiveTabIsTaskLog()) {
		editorSetStatusMsg("Task logs cannot be saved");
		return;
	}
	if (E.tab_kind == EDITOR_TAB_GIT_COMMIT) {
		editorGitViewCommitFromActiveTab();
		return;
	}
	if (editorActiveTabIsReadOnly()) {
		editorSetStatusMsg("This view is read-only");
		return;
	}

	if (E.filename == NULL) {
		if ((E.filename = editorPrompt("Save as: %s")) == NULL) {
			if (E.statusmsg[0] == '\0') {
				editorSetStatusMsg("Save aborted");
			}
			return;
		}
		(void)editorSyntaxParseFullActive();
	}
	if (editorWatchActiveHasDiskConflict() &&
	    !editorPromptYesNo("File changed on disk. Overwrite? [y/N] %s")) {
		editorSetStatusMsg("Save aborted; file changed on disk");
		return;
	}

	size_t len = 0;
	errno = 0;
	char *buf = editorDupActiveTextSource(&len);
	char *tmp_path = editorTempPathForTarget(E.filename);
	int saved_errno = 0;
	int cleanup_errno = 0;
	mode_t mode = editDefaultCreateMode();
	struct stat st;

	if (buf == NULL && (len > 0 || errno != 0)) {
		free(tmp_path);
		if (errno == EOVERFLOW) {
			editorSetFileTooLargeStatus();
		} else {
			editorSetAllocFailureStatus();
		}
		return;
	}

	if (stat(E.filename, &st) == 0) {
		mode = st.st_mode & 0777;
	}

	if (tmp_path == NULL) {
		free(buf);
		editorSetAllocFailureStatus();
		return;
	}

	if (!editSaveWriteToTempPath(E.filename, tmp_path, buf, len, mode, &saved_errno,
	                             &cleanup_errno)) {
		free(tmp_path);
		free(buf);
		editSetSaveFailureStatus(saved_errno, cleanup_errno);
		return;
	}

	E.dirty = 0;
	editorGitBlameCacheClear(&E.active_buffer);
	editorWatchRefreshActiveBaseline();
	free(tmp_path);
	free(buf);
	editSaveAfterCommit(E.filename, len);
	return;
}
