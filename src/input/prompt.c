#include "input/prompt.h"

#include "debug/dap.h"
#include "editing/edit.h"
#include "language/lsp.h"
#include "render/screen.h"
#include "rotide.h"
#include "support/alloc.h"
#include "support/terminal.h"
#include "text/utf8.h"
#include "workspace/task.h"

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static size_t promptPrevDeleteIdx(const char *buf, size_t buflen) {
	if (buflen == 0) {
		return 0;
	}

	size_t seq_start = buflen - 1;
	while (seq_start > 0 && editorIsUtf8ContinuationByte((unsigned char)buf[seq_start])) {
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

/* Exactly one of `prompt` and `literal_label` is non-NULL and selects how the
 * line is rendered with the live input `buf`. `prompt` is a printf format with one
 * `%s` for `buf` (callers pass a constant format). `literal_label` is plain text
 * rendered via a constant "%s%s" format, so callers may safely interpolate
 * untrusted data (e.g. a git remote name) into it — it is only ever a `%s`
 * argument, never a format string, so it cannot be interpreted as one. */
static char *promptRunLoop(const char *prompt, const char *literal_label, int allow_empty,
                           editorPromptCallback callback, editorPromptCompleteFn complete_fn,
                           void *complete_ctx) {
	size_t bufmax = 128;
	char *buf = editorMalloc(bufmax);
	if (buf == NULL) {
		editorSetStatusMsg("Out of memory");
		return NULL;
	}

	size_t buflen = 0;
	buf[0] = '\0';
	int tab_iteration = 0;
	char *tab_anchor = NULL;

	while (1) {
		if (literal_label != NULL) {
			editorSetStatusMsg("%s%s", literal_label, buf);
		} else {
			editorSetStatusMsg(prompt, buf);
		}
		editorRefreshScreen();

		int c = editorReadKey();
		if (c == INPUT_EOF_EVENT) {
			free(buf);
			free(tab_anchor);
			editorExitOnInputShutdown();
			return NULL;
		}
		if (c == RESIZE_EVENT) {
			(void)editorRefreshWindowSize();
			continue;
		}
		if (c == SYNTAX_EVENT || c == TASK_EVENT || c == WATCH_EVENT || c == DAP_EVENT) {
			continue;
		}
		/* Prompt editing is keyboard-only; ignore mouse packets. */
		if (c == MOUSE_EVENT) {
			continue;
		}
		if (c == '\t' && complete_fn != NULL) {
			if (tab_iteration == 0) {
				free(tab_anchor);
				tab_anchor = strdup(buf);
			}
			const char *anchor = tab_anchor != NULL ? tab_anchor : buf;
			char *replacement = complete_fn(buf, anchor, complete_ctx, tab_iteration);
			if (replacement != NULL) {
				size_t new_len = strlen(replacement);
				size_t needed = new_len + 1;
				if (needed > bufmax) {
					size_t new_max = bufmax;
					while (new_max < needed) {
						new_max *= 2;
					}
					char *new_buf = editorRealloc(buf, new_max);
					if (new_buf == NULL) {
						free(replacement);
						free(tab_anchor);
						free(buf);
						editorSetStatusMsg("Out of memory");
						return NULL;
					}
					buf = new_buf;
					bufmax = new_max;
				}
				memcpy(buf, replacement, new_len + 1);
				buflen = new_len;
				free(replacement);
			}
			tab_iteration++;
			if (callback != NULL) {
				callback(buf, c);
			}
			continue;
		}
		tab_iteration = 0;
		free(tab_anchor);
		tab_anchor = NULL;
		if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
			if (buflen != 0) {
				buflen = promptPrevDeleteIdx(buf, buflen);
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

char *editorPromptWithCallback(const char *prompt, int allow_empty, editorPromptCallback callback) {
	return promptRunLoop(prompt, NULL, allow_empty, callback, NULL, NULL);
}

char *editorPromptWithCompletion(const char *prompt, int allow_empty,
                                 editorPromptCompleteFn complete_fn, void *complete_ctx) {
	return promptRunLoop(prompt, NULL, allow_empty, NULL, complete_fn, complete_ctx);
}

char *editorPrompt(const char *prompt) {
	return promptRunLoop(prompt, NULL, 0, NULL, NULL, NULL);
}

static int promptYesNo(const char *prompt, const char *literal_label) {
	char *response = promptRunLoop(prompt, literal_label, 1, NULL, NULL, NULL);
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

int editorPromptYesNo(const char *prompt) {
	return promptYesNo(prompt, NULL);
}

/* Like editorPromptYesNo, but `label` is plain text rather than a printf format,
 * so untrusted data can be interpolated into it safely (no format-string risk):
 * it is passed as the literal-label argument, which only ever reaches a `%s`. */
int editorPromptYesNoLiteral(const char *label) {
	return promptYesNo(NULL, label);
}
