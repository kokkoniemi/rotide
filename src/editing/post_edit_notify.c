#include "editing/post_edit_notify.h"

#include "editing/syntax_runtime.h"
#include "editing/text_source.h"
#include "language/lsp.h"
#include "rotide.h"

#include <stdlib.h>

static int editorLspActiveBufferTracked(void) {
	return editorLspFileEnabled(E.filename, E.syntax_language);
}

static int editorLspActiveBufferTrackedForEslint(void) {
	return editorLspEslintEnabledForFile(E.filename, E.syntax_language);
}

static void editorLspNotifyDidChangeActive(const struct editorSyntaxEdit *edit,
                                           const char *inserted_text, size_t inserted_len) {
	if (!editorLspActiveBufferTracked() && !editorLspActiveBufferTrackedForEslint()) {
		return;
	}

	char *full_text = NULL;
	size_t full_text_len = 0;
	if ((editorLspActiveBufferTracked() && !E.lsp_doc_open) ||
	    (editorLspActiveBufferTrackedForEslint() && !E.lsp_eslint_doc_open)) {
		full_text = editorDupActiveTextSource(&full_text_len);
		if (full_text == NULL && full_text_len > 0) {
			free(full_text);
			return;
		}
	}

	if (editorLspActiveBufferTracked()) {
		(void)editorLspNotifyDidChange(E.filename, E.syntax_language, &E.lsp_doc_open,
		                               &E.lsp_doc_version, edit, inserted_text,
		                               inserted_len, full_text, full_text_len);
	}
	if (editorLspActiveBufferTrackedForEslint()) {
		(void)editorLspNotifyEslintDidChange(E.filename, E.syntax_language,
		                                     &E.lsp_eslint_doc_open,
		                                     &E.lsp_eslint_doc_version, edit, inserted_text,
		                                     inserted_len, full_text, full_text_len);
	}
	free(full_text);
}

void editorNotifyPostEditLanguage(int syntax_track, const struct editorSyntaxEdit *syntax_edit,
                                  const char *inserted_text, size_t inserted_len) {
	if (syntax_track) {
		(void)editorSyntaxApplyIncrementalEditActive(syntax_edit, inserted_text,
		                                             inserted_len);
		editorLspNotifyDidChangeActive(syntax_edit, inserted_text, inserted_len);
		return;
	}

	(void)editorSyntaxApplyIncrementalEditActive(NULL, inserted_text, inserted_len);
	editorLspNotifyDidChangeActive(NULL, inserted_text, inserted_len);
}
