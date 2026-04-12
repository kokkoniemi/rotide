#include "editing/edit.h"

#include "editing/buffer_core.h"
#include "input/dispatch.h"
#include "language/lsp.h"
#include "support/alloc.h"
#include "support/file_io.h"
#include "support/save_syscalls.h"
#include "support/size_utils.h"
#include "support/terminal.h"
#include "workspace/tabs.h"
#include "text/document.h"
#include "text/row.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define NEWLINE_CHAR_WIDTH 1

static int editorReadNormalizedFileToText(FILE *fp, char **text_out, size_t *len_out) {
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

int editorInsertText(const char *text, size_t len) {
	int insert_cx = 0;
	size_t start_offset = 0;
	size_t after_offset = 0;
	int dirty_delta = 0;
	int sim_cy = 0;
	int sim_cx = 0;
	int sim_numrows = 0;

	if (len == 0) {
		return 0;
	}
	if (text == NULL || len > ROTIDE_MAX_TEXT_BYTES || E.cy < 0 || E.cy > E.numrows) {
		editorSetOperationTooLargeStatus();
		return 0;
	}
	if (E.cy < E.numrows) {
		insert_cx = editorRowClampCxToClusterBoundary(&E.rows[E.cy], E.cx);
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
	sim_cx = insert_cx;
	sim_numrows = E.numrows;
	for (size_t i = 0; i < len; i++) {
		if (text[i] == '\n') {
			dirty_delta++;
			sim_numrows++;
			sim_cy++;
			sim_cx = 0;
			continue;
		}
		dirty_delta++;
		if (sim_cy == sim_numrows) {
			dirty_delta++;
			sim_numrows++;
		}
		sim_cx++;
	}
	if (dirty_delta <= 0 || E.dirty > INT_MAX - dirty_delta) {
		editorSetOperationTooLargeStatus();
		return 0;
	}

	struct editorDocumentEdit edit = {
		.kind = EDITOR_EDIT_INSERT_TEXT,
		.start_offset = start_offset,
		.old_len = 0,
		.new_text = text,
		.new_len = len,
		.before_cursor_offset = start_offset,
		.after_cursor_offset = after_offset,
		.before_dirty = E.dirty,
		.after_dirty = E.dirty + dirty_delta
	};
	return editorApplyDocumentEdit(&edit);
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
		insert_cx = editorRowClampCxToCharBoundary(&E.rows[E.cy], E.cx);
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

	struct editorDocumentEdit edit = {
		.kind = EDITOR_EDIT_INSERT_TEXT,
		.start_offset = start_offset,
		.old_len = 0,
		.new_text = inserted_text,
		.new_len = inserted_len,
		.before_cursor_offset = start_offset,
		.after_cursor_offset = start_offset + 1,
		.before_dirty = E.dirty,
		.after_dirty = E.dirty + dirty_delta
	};
	if (editorApplyDocumentEdit(&edit)) {
		(void)editorSyncCursorFromOffsetByteBoundary(start_offset + 1);
	}
}

void editorInsertNewline(void) {
	int split_idx = 0;
	if (E.cy < E.numrows) {
		split_idx = editorRowClampCxToClusterBoundary(&E.rows[E.cy], E.cx);
	}

	size_t start_offset = 0;
	if (!editorBufferPosToOffset(E.cy, split_idx, &start_offset)) {
		return;
	}

	struct editorDocumentEdit edit = {
		.kind = EDITOR_EDIT_NEWLINE,
		.start_offset = start_offset,
		.old_len = 0,
		.new_text = "\n",
		.new_len = 1,
		.before_cursor_offset = start_offset,
		.after_cursor_offset = start_offset + 1,
		.before_dirty = E.dirty,
		.after_dirty = E.dirty + 1
	};
	(void)editorApplyDocumentEdit(&edit);
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
		struct erow *row = &E.rows[E.cy];
		int cur_cx = editorRowClampCxToClusterBoundary(row, E.cx);
		int prev_cx = editorRowPrevClusterIdx(row, cur_cx);
		if (!editorBufferPosToOffset(E.cy, prev_cx, &start_offset) ||
				!editorBufferPosToOffset(E.cy, cur_cx, &end_offset) ||
				end_offset <= start_offset) {
			return;
		}
	} else {
		int merge_col = E.rows[E.cy - 1].size;
		if (!editorBufferPosToOffset(E.cy - 1, merge_col, &start_offset) ||
				!editorBufferPosToOffset(E.cy, 0, &end_offset) ||
				end_offset <= start_offset) {
			return;
		}
		dirty_delta = 2;
	}

	old_len = end_offset - start_offset;
	struct editorDocumentEdit edit = {
		.kind = EDITOR_EDIT_DELETE_TEXT,
		.start_offset = start_offset,
		.old_len = old_len,
		.new_text = "",
		.new_len = 0,
		.before_cursor_offset = before_cursor_offset,
		.after_cursor_offset = start_offset,
		.before_dirty = E.dirty,
		.after_dirty = E.dirty + dirty_delta
	};
	(void)editorApplyDocumentEdit(&edit);
}

void editorOpen(const char *filename) {
	int was_preview = E.is_preview;
	FILE *fp = NULL;
	char *text = NULL;
	size_t text_len = 0;
	struct editorDocument document;
	int document_inited = 0;

	editorLspNotifyDidClose(E.filename, E.syntax_language, &E.lsp_doc_open, &E.lsp_doc_version);
	editorFreeActiveBufferState();
	E.tab_kind = EDITOR_TAB_FILE;
	E.is_preview = was_preview;
	E.filename = strdup(filename);
	if (E.filename == NULL) {
		editorSetAllocFailureStatus();
		return;
	}

	fp = fopen(filename, "r");
	if (!fp) {
		panic("fopen");
	}
	if (!editorReadNormalizedFileToText(fp, &text, &text_len)) {
		fclose(fp);
		return;
	}
	fclose(fp);

	editorDocumentInit(&document);
	document_inited = 1;
	if (!editorDocumentResetFromString(&document, text, text_len)) {
		editorSetAllocFailureStatus();
		goto cleanup;
	}

	if (!editorRestoreActiveFromDocument(&document, 0, 0, 0, 1)) {
		goto cleanup;
	}

cleanup:
	if (document_inited) {
		editorDocumentFree(&document);
	}
	free(text);
}

void editorSetStatusMsg(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
	va_end(ap);
	E.statusmsg_time = time(NULL);
}

static int editorWriteAll(int fd, const char *buf, size_t len) {
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

static int editorSaveCleanupOnError(int *fd, int *dir_fd, const char *tmp_path,
		int tmp_created, int tmp_renamed, int *cleanup_errno) {
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

static const char *editorSaveFailureClass(int errnum) {
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

static void editorSetSaveFailureStatus(int saved_errno, int cleanup_errno) {
	const char *error_class = editorSaveFailureClass(saved_errno);
	const char *error_text = strerror(saved_errno);

	if (cleanup_errno != 0) {
		editorSetStatusMsg("Save failed: %s (%s); cleanup failed (%s)", error_class,
				error_text, strerror(cleanup_errno));
		return;
	}

	editorSetStatusMsg("Save failed: %s (%s)", error_class, error_text);
}

static mode_t editorDefaultCreateMode(void) {
	mode_t mask = umask(0);
	umask(mask);
	return 0644 & ~mask;
}

void editorSave(void) {
	if (editorActiveTabIsTaskLog()) {
		editorSetStatusMsg("Task logs cannot be saved");
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

	size_t len = 0;
	errno = 0;
	char *buf = editorDupActiveTextSource(&len);
	char *tmp_path = editorTempPathForTarget(E.filename);
	int fd = -1;
	int dir_fd = -1;
	int tmp_created = 0;
	int tmp_renamed = 0;
	mode_t mode = editorDefaultCreateMode();
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

	// Write to a sibling temp file and atomically replace the target on success.
	// This avoids leaving a partially written file if the save path fails midway.
	fd = mkstemp(tmp_path);
	if (fd == -1) {
		goto err;
	}
	tmp_created = 1;
	if (fchmod(fd, mode) == -1) {
		goto err;
	}
	if (editorWriteAll(fd, buf, len) == -1) {
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
	if (editorSaveRename(tmp_path, E.filename) == -1) {
		goto err;
	}
	tmp_renamed = 1;

	dir_fd = editorOpenParentDirForTarget(E.filename);
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
	dir_fd = -1;

	E.dirty = 0;
	free(tmp_path);
	free(buf);
	editorLspNotifyDidSaveActive();
	editorSetStatusMsg("%zu bytes written to disk", len);
	return;

err: {
	int saved_errno = errno;
	int cleanup_errno = 0;
	(void)editorSaveCleanupOnError(&fd, &dir_fd, tmp_path, tmp_created, tmp_renamed,
			&cleanup_errno);
	free(tmp_path);
	free(buf);
	editorSetSaveFailureStatus(saved_errno, cleanup_errno);
}
}
