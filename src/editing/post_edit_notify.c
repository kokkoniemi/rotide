#include "editing/post_edit_notify.h"

#include "editing/syntax_runtime.h"
#include "editing/text_source.h"
#include "language/lsp.h"
#include "rotide.h"
#include "language/syntax.h"

#include <stdlib.h>

static int postEditNotifyLspTracked(void) {
	return editorLspFileEnabled(E.filename, E.syntax_language);
}

static int postEditNotifyEslintTracked(void) {
	return editorLspEslintEnabledForFile(E.filename, E.syntax_language);
}

static void postEditNotifyLspDidChangeActive(const struct editorSyntaxEdit *edit,
                                             const char *inserted_text, size_t inserted_len) {
	if (!postEditNotifyLspTracked() && !postEditNotifyEslintTracked()) {
		return;
	}

	char *full_text = NULL;
	size_t full_text_len = 0;
	if ((postEditNotifyLspTracked() && !E.lsp_doc_open) ||
	    (postEditNotifyEslintTracked() && !E.lsp_eslint_doc_open)) {
		full_text = editorDupActiveTextSource(&full_text_len);
		if (full_text == NULL && full_text_len > 0) {
			free(full_text);
			return;
		}
	}

	if (postEditNotifyLspTracked()) {
		(void)editorLspNotifyDidChange(E.filename, E.syntax_language, &E.lsp_doc_open,
		                               &E.lsp_doc_version, edit, inserted_text,
		                               inserted_len, full_text, full_text_len);
	}
	if (postEditNotifyEslintTracked()) {
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
		postEditNotifyLspDidChangeActive(syntax_edit, inserted_text, inserted_len);
		return;
	}

	(void)editorSyntaxApplyIncrementalEditActive(NULL, inserted_text, inserted_len);
	postEditNotifyLspDidChangeActive(NULL, inserted_text, inserted_len);
}
