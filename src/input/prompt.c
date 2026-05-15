#include "input/prompt.h"

#include "debug/dap.h"
#include "editing/edit.h"
#include "language/lsp.h"
#include "render/screen.h"
#include "support/alloc.h"
#include "support/terminal.h"
#include "text/utf8.h"
#include "workspace/task.h"

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <strings.h>

static size_t editorPromptPrevDeleteIdx(const char *buf, size_t buflen) {
	if (buflen == 0) {
		return 0;
	}

	size_t seq_start = buflen - 1;
	while (seq_start > 0 &&
			editorIsUtf8ContinuationByte((unsigned char)buf[seq_start])) {
		seq_start--;
	}

	unsigned int cp = 0;
	int seq_len = editorUtf8DecodeCodepoint(&buf[seq_start], (int)(buflen - seq_start), &cp);
	if (seq_len > 1 && seq_start + (size_t)seq_len == buflen) {
		return seq_start;
	}

	return buflen - 1;
}

void editorExitOnInputShutdown(void) {
	if (editorTaskIsRunning()) {
		(void)editorTaskTerminate();
	}
	editorDapShutdown();
	editorLspShutdown();
	editorRestoreTerminal();
	editorClearScreen();
	editorResetCursorPos();

	exit(EXIT_FAILURE);
}

char *editorPromptWithCallback(const char *prompt, int allow_empty,
		editorPromptCallback callback) {
	size_t bufmax = 128;
	char *buf = editorMalloc(bufmax);
	if (buf == NULL) {
		editorSetStatusMsg("Out of memory");
		return NULL;
	}

	size_t buflen = 0;
	buf[0] = '\0';

	while (1) {
		editorSetStatusMsg(prompt, buf);
		editorRefreshScreen();

		int c = editorReadKey();
		if (c == INPUT_EOF_EVENT) {
			free(buf);
			editorExitOnInputShutdown();
			return NULL;
		}
		if (c == RESIZE_EVENT) {
			(void)editorRefreshWindowSize();
			continue;
		}
		if (c == SYNTAX_EVENT || c == TASK_EVENT || c == WATCH_EVENT) {
			continue;
		}
		/* Prompt editing is keyboard-only; ignore mouse packets. */
		if (c == MOUSE_EVENT) {
			continue;
		}
		if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
			if (buflen != 0) {
				buflen = editorPromptPrevDeleteIdx(buf, buflen);
				buf[buflen] = '\0';
			}
		} else if (c == '\x1b') {
			if (callback != NULL) {
				callback(buf, c);
			}
			editorSetStatusMsg("");
			free(buf);
			return NULL;
		} else if (c == '\r' && (allow_empty || buflen != 0)) {
			if (callback != NULL) {
				callback(buf, c);
			}
			editorSetStatusMsg("");
			return buf;
		} else if (c >= CHAR_MIN && c <= CHAR_MAX) {
			unsigned char byte = (unsigned char)c;
			/* Keep non-ASCII bytes verbatim; only filter ASCII controls. */
			if (byte >= 0x80 || !iscntrl(byte)) {
				if (buflen == bufmax - 1) {
					size_t new_bufmax = bufmax * 2;
					char *new_buf = editorRealloc(buf, new_bufmax);
					if (new_buf == NULL) {
						free(buf);
						editorSetStatusMsg("Out of memory");
						return NULL;
					}
					buf = new_buf;
					bufmax = new_bufmax;
				}
				buf[buflen] = (char)byte;
				buflen++;
				buf[buflen] = '\0';
			}
		}

		if (callback != NULL) {
			callback(buf, c);
		}
	}
}

char *editorPrompt(const char *prompt) {
	return editorPromptWithCallback(prompt, 0, NULL);
}

int editorPromptYesNo(const char *prompt) {
	char *response = editorPromptWithCallback(prompt, 1, NULL);
	int accepted = 0;
	if (response == NULL) {
		return 0;
	}
	if (strcasecmp(response, "y") == 0 || strcasecmp(response, "yes") == 0) {
		accepted = 1;
	}
	free(response);
	return accepted;
}
