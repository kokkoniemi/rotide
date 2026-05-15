#include "workspace/drawer.h"

#include "language/lsp.h"
#include "language/syntax.h"
#include "workspace/drawer_internal.h"
#include "workspace/file_search.h"
#include "workspace/project_search.h"
#include "workspace/tabs.h"

#include <stdio.h>
#include <string.h>

enum editorDrawerLspGroup {
	EDITOR_DRAWER_LSP_GROUP_PROBLEMS = 0,
	EDITOR_DRAWER_LSP_GROUP_SYMBOLS,
	EDITOR_DRAWER_LSP_GROUP_COUNT
};

#define EDITOR_DRAWER_LSP_VISIBLE_GROUP_COUNT 2

enum editorDrawerLspEntryKind {
	EDITOR_DRAWER_LSP_ENTRY_ROOT = 0,
	EDITOR_DRAWER_LSP_ENTRY_GROUP,
	EDITOR_DRAWER_LSP_ENTRY_PROBLEM,
	EDITOR_DRAWER_LSP_ENTRY_SYMBOL,
	EDITOR_DRAWER_LSP_ENTRY_PLACEHOLDER
};

enum editorDrawerLspProblemSource {
	EDITOR_DRAWER_LSP_PROBLEM_LSP = 0,
	EDITOR_DRAWER_LSP_PROBLEM_SYNTAX
};

struct editorDrawerLspProblem {
	enum editorDrawerLspProblemSource source;
	const char *path;
	const char *message;
	int line;
	int character;
	int severity;
};

struct editorDrawerLspSymbolEntry {
	const char *name;
	const char *path;
	int kind;
	int line;
	int character;
	int depth;
	int parent_index;
	int is_last_sibling;
};

struct editorDrawerLspLookup {
	enum editorDrawerLspEntryKind kind;
	int group_idx;
	int item_idx;
	int item_count;
	int visible_idx;
	int parent_visible_idx;
	int group_visible_idx;
	struct editorDrawerLspProblem problem;
	struct editorDrawerLspSymbolEntry symbol;
};

static const char *editor_drawer_lsp_group_names[EDITOR_DRAWER_LSP_GROUP_COUNT] = {
	"Problems",
	"Symbols"
};

static unsigned int editorDrawerLspAllGroupsMask(void) {
	unsigned int mask = 0;
	for (int i = 0; i < EDITOR_DRAWER_LSP_VISIBLE_GROUP_COUNT; i++) {
		mask |= 1u << (unsigned int)i;
	}
	return mask;
}

static int editorDrawerLspGroupExpanded(int group_idx) {
	if (group_idx < 0 || group_idx >= EDITOR_DRAWER_LSP_VISIBLE_GROUP_COUNT) {
		return 0;
	}
	return (E.drawer_lsp_expanded & (1u << (unsigned int)group_idx)) != 0;
}

static void editorDrawerLspEnsureDefaultExpanded(void) {
	E.drawer_lsp_expanded = editorDrawerLspAllGroupsMask();
}

static int editorDrawerTabHasSyntaxProblem(const struct editorBuffer *tab, int *line_out,
		int *character_out) {
	struct editorSyntaxState *state = tab != NULL ? tab->syntax_state : NULL;
	return editorSyntaxStateFirstErrorPosition(state, line_out, character_out);
}

static int editorDrawerLspTabProblemCount(int tab_idx) {
	const struct editorBuffer *tab = editorTabBufferHandleAt(tab_idx);
	if (tab == NULL) {
		return 0;
	}
	int count = tab->lsp_diagnostic_count;
	if (editorDrawerTabHasSyntaxProblem(tab, NULL, NULL)) {
		count++;
	}
	return count;
}

static int editorDrawerLspProblemCount(void) {
	int count = 0;
	for (int tab_idx = 0; tab_idx < E.tab_count; tab_idx++) {
		count += editorDrawerLspTabProblemCount(tab_idx);
	}
	return count;
}

static int editorDrawerLspProblemAt(int problem_idx, struct editorDrawerLspProblem *problem_out) {
	if (problem_out == NULL || problem_idx < 0) {
		return 0;
	}

	for (int tab_idx = 0; tab_idx < E.tab_count; tab_idx++) {
		const struct editorBuffer *tab = editorTabBufferHandleAt(tab_idx);
		if (tab == NULL) {
			continue;
		}
		const char *filename = tab->filename;

		int line = 0;
		int character = 0;
		if (editorDrawerTabHasSyntaxProblem(tab, &line, &character)) {
			if (problem_idx == 0) {
				memset(problem_out, 0, sizeof(*problem_out));
				problem_out->source = EDITOR_DRAWER_LSP_PROBLEM_SYNTAX;
				problem_out->path = filename;
				problem_out->message = "Syntax parse error";
				problem_out->line = line;
				problem_out->character = character;
				problem_out->severity = 1;
				return 1;
			}
			problem_idx--;
		}

		const struct editorLspDiagnostic *diagnostics = tab->lsp_diagnostics;
		int diagnostic_count = tab->lsp_diagnostic_count;
		for (int i = 0; i < diagnostic_count; i++) {
			if (problem_idx == 0) {
				memset(problem_out, 0, sizeof(*problem_out));
				problem_out->source = EDITOR_DRAWER_LSP_PROBLEM_LSP;
				problem_out->path = filename;
				problem_out->message = diagnostics[i].message;
				problem_out->line = diagnostics[i].start_line;
				problem_out->character = diagnostics[i].start_character;
				problem_out->severity = diagnostics[i].severity;
				return 1;
			}
			problem_idx--;
		}
	}

	return 0;
}

static int editorDrawerLspSymbolCount(void) {
	return E.lsp_symbol_count > 0 ? E.lsp_symbol_count : 0;
}

static int editorDrawerLspSymbolAt(int symbol_idx, struct editorDrawerLspSymbolEntry *symbol_out) {
	if (symbol_out == NULL || symbol_idx < 0 || symbol_idx >= E.lsp_symbol_count ||
			E.lsp_symbols == NULL) {
		return 0;
	}
	const struct editorLspSymbol *src = &E.lsp_symbols[symbol_idx];
	memset(symbol_out, 0, sizeof(*symbol_out));
	symbol_out->name = src->name != NULL ? src->name : "";
	symbol_out->path = E.filename;
	symbol_out->kind = src->kind;
	symbol_out->line = src->line;
	symbol_out->character = src->character;
	symbol_out->depth = src->depth;
	symbol_out->parent_index = src->parent_index;
	symbol_out->is_last_sibling = src->is_last_sibling;
	return 1;
}

static int editorDrawerLspGroupItemCount(int group_idx) {
	if (group_idx == EDITOR_DRAWER_LSP_GROUP_PROBLEMS) {
		return editorDrawerLspProblemCount();
	}
	if (group_idx == EDITOR_DRAWER_LSP_GROUP_SYMBOLS) {
		return editorDrawerLspSymbolCount();
	}
	return 0;
}

int editorDrawerLspVisibleCount(void) {
	int count = 1;
	for (int group_idx = 0; group_idx < EDITOR_DRAWER_LSP_VISIBLE_GROUP_COUNT; group_idx++) {
		count++;
		if (!editorDrawerLspGroupExpanded(group_idx)) {
			continue;
		}
		int item_count = editorDrawerLspGroupItemCount(group_idx);
		count += item_count > 0 ? item_count : 1;
	}
	return count;
}

static int editorDrawerLspLookupByVisibleIndex(int visible_idx,
		struct editorDrawerLspLookup *lookup_out) {
	if (lookup_out == NULL || visible_idx < 0) {
		return 0;
	}

	memset(lookup_out, 0, sizeof(*lookup_out));
	lookup_out->visible_idx = visible_idx;
	lookup_out->group_idx = -1;
	lookup_out->item_idx = -1;
	lookup_out->parent_visible_idx = -1;
	lookup_out->group_visible_idx = -1;

	if (visible_idx == 0) {
		lookup_out->kind = EDITOR_DRAWER_LSP_ENTRY_ROOT;
		return 1;
	}

	int cursor = 1;
	for (int group_idx = 0; group_idx < EDITOR_DRAWER_LSP_VISIBLE_GROUP_COUNT; group_idx++) {
		int group_visible_idx = cursor;
		int item_count = editorDrawerLspGroupItemCount(group_idx);
		if (visible_idx == group_visible_idx) {
			lookup_out->kind = EDITOR_DRAWER_LSP_ENTRY_GROUP;
			lookup_out->group_idx = group_idx;
			lookup_out->parent_visible_idx = 0;
			lookup_out->group_visible_idx = group_visible_idx;
			lookup_out->item_count = item_count;
			return 1;
		}
		cursor++;

		if (!editorDrawerLspGroupExpanded(group_idx)) {
			continue;
		}

		if (item_count == 0) {
			if (visible_idx == cursor) {
				lookup_out->kind = EDITOR_DRAWER_LSP_ENTRY_PLACEHOLDER;
				lookup_out->group_idx = group_idx;
				lookup_out->parent_visible_idx = group_visible_idx;
				lookup_out->group_visible_idx = group_visible_idx;
				return 1;
			}
			cursor++;
			continue;
		}

		for (int item_idx = 0; item_idx < item_count; item_idx++) {
			if (visible_idx == cursor) {
				lookup_out->group_idx = group_idx;
				lookup_out->item_idx = item_idx;
				lookup_out->item_count = item_count;
				lookup_out->group_visible_idx = group_visible_idx;
				if (group_idx == EDITOR_DRAWER_LSP_GROUP_SYMBOLS) {
					lookup_out->kind = EDITOR_DRAWER_LSP_ENTRY_SYMBOL;
					if (!editorDrawerLspSymbolAt(item_idx, &lookup_out->symbol)) {
						return 0;
					}
					lookup_out->parent_visible_idx = lookup_out->symbol.parent_index >= 0 ?
							group_visible_idx + 1 + lookup_out->symbol.parent_index :
							group_visible_idx;
					return 1;
				}
				lookup_out->parent_visible_idx = group_visible_idx;
				lookup_out->kind = EDITOR_DRAWER_LSP_ENTRY_PROBLEM;
				return editorDrawerLspProblemAt(item_idx, &lookup_out->problem);
			}
			cursor++;
		}
	}

	return 0;
}

int editorDrawerLspToggle(void) {
	if (E.drawer_mode == EDITOR_DRAWER_MODE_LSP) {
		E.drawer_mode = EDITOR_DRAWER_MODE_TREE;
		E.drawer_selected_index = -1;
		E.drawer_rowoff = 0;
		E.drawer_resize_active = 0;
		E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
		return 1;
	}

	if (editorFileSearchIsActive()) {
		editorFileSearchExit(1);
	}
	if (editorProjectSearchIsActive()) {
		editorProjectSearchExit(1);
	}
	editorLspRefreshActiveDocumentSymbols();
	editorDrawerLspEnsureDefaultExpanded();
	E.drawer_mode = EDITOR_DRAWER_MODE_LSP;
	E.drawer_selected_index = -1;
	E.drawer_rowoff = 0;
	E.drawer_resize_active = 0;
	(void)editorDrawerSetCollapsed(0);
	E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
	return 1;
}

int editorDrawerLspGetVisibleEntry(int visible_idx, struct editorDrawerEntryView *view_out) {
	if (view_out == NULL) {
		return 0;
	}

	static char lsp_name_buf[PATH_MAX + 128];
	struct editorDrawerLspLookup lookup;
	if (!editorDrawerLspLookupByVisibleIndex(visible_idx, &lookup)) {
		return 0;
	}

	memset(view_out, 0, sizeof(*view_out));
	view_out->is_selected = visible_idx == E.drawer_selected_index;
	view_out->parent_visible_idx = lookup.parent_visible_idx;
	view_out->line = lookup.problem.line;
	view_out->character = lookup.problem.character;
	switch (lookup.kind) {
	case EDITOR_DRAWER_LSP_ENTRY_ROOT:
		view_out->name = "LSP";
		view_out->depth = 0;
		view_out->is_dir = 1;
		view_out->is_expanded = 1;
		view_out->is_root = 1;
		view_out->is_last_sibling = 1;
		return 1;
	case EDITOR_DRAWER_LSP_ENTRY_GROUP:
		if (lookup.group_idx == EDITOR_DRAWER_LSP_GROUP_PROBLEMS) {
			snprintf(lsp_name_buf, sizeof(lsp_name_buf), "Problems (%d)", lookup.item_count);
			view_out->name = lsp_name_buf;
		} else {
			view_out->name = editor_drawer_lsp_group_names[lookup.group_idx];
		}
		view_out->depth = 1;
		view_out->is_dir = 1;
		view_out->is_expanded = editorDrawerLspGroupExpanded(lookup.group_idx);
		view_out->is_last_sibling = lookup.group_idx == EDITOR_DRAWER_LSP_VISIBLE_GROUP_COUNT - 1;
		return 1;
	case EDITOR_DRAWER_LSP_ENTRY_PROBLEM: {
		const char *path = lookup.problem.path != NULL ? lookup.problem.path : "";
		const char *slash = strrchr(path, '/');
		const char *base = slash != NULL ? slash + 1 : path;
		const char *message = lookup.problem.message != NULL ? lookup.problem.message : "";
		const char *kind = lookup.problem.source == EDITOR_DRAWER_LSP_PROBLEM_SYNTAX ? "Syntax" :
				"Info";
		if (lookup.problem.source == EDITOR_DRAWER_LSP_PROBLEM_LSP) {
			if (lookup.problem.severity == 1) {
				kind = "Error";
			} else if (lookup.problem.severity == 2) {
				kind = "Warning";
			}
		}
		snprintf(lsp_name_buf, sizeof(lsp_name_buf), "%s %s:%d:%d %s", kind,
				base != NULL && base[0] != '\0' ? base : "(untitled)", lookup.problem.line + 1,
				lookup.problem.character + 1, message);
		view_out->name = lsp_name_buf;
		view_out->path = lookup.problem.path;
		view_out->depth = 2;
		view_out->is_last_sibling = lookup.item_idx == lookup.item_count - 1;
		view_out->lsp_problem_severity = lookup.problem.severity;
		view_out->lsp_problem_kind_len = (int)strlen(kind);
		return 1;
	}
	case EDITOR_DRAWER_LSP_ENTRY_SYMBOL: {
		const char *symbol_name = lookup.symbol.name != NULL && lookup.symbol.name[0] != '\0' ?
				lookup.symbol.name :
				"(unnamed)";
		snprintf(lsp_name_buf, sizeof(lsp_name_buf), "%s %s:%d",
				editorLspSymbolKindLabel(lookup.symbol.kind), symbol_name,
				lookup.symbol.line + 1);
		view_out->name = lsp_name_buf;
		view_out->path = lookup.symbol.path;
		view_out->line = lookup.symbol.line;
		view_out->character = lookup.symbol.character;
		view_out->depth = 2 + lookup.symbol.depth;
		view_out->is_last_sibling = lookup.symbol.is_last_sibling;
		return 1;
	}
	case EDITOR_DRAWER_LSP_ENTRY_PLACEHOLDER:
		view_out->name =
				lookup.group_idx == EDITOR_DRAWER_LSP_GROUP_SYMBOLS ? "(symbols not loaded yet)" :
										   "(none)";
		view_out->depth = 2;
		view_out->is_placeholder = 1;
		view_out->is_last_sibling = 1;
		return 1;
	default:
		return 0;
	}
}

int editorDrawerLspExpandSelection(int viewport_rows) {
	struct editorDrawerLspLookup lookup;
	if (!editorDrawerLspLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	if (lookup.kind == EDITOR_DRAWER_LSP_ENTRY_ROOT) {
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 0;
	}
	if (lookup.kind != EDITOR_DRAWER_LSP_ENTRY_GROUP ||
			editorDrawerLspGroupExpanded(lookup.group_idx)) {
		return 0;
	}
	E.drawer_lsp_expanded |= 1u << (unsigned int)lookup.group_idx;
	editorDrawerClampSelectionAndScroll(viewport_rows);
	return 1;
}

int editorDrawerLspCollapseSelection(int viewport_rows) {
	struct editorDrawerLspLookup lookup;
	if (!editorDrawerLspLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	if (lookup.kind == EDITOR_DRAWER_LSP_ENTRY_ROOT) {
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 0;
	}
	if (lookup.kind == EDITOR_DRAWER_LSP_ENTRY_GROUP) {
		if (!editorDrawerLspGroupExpanded(lookup.group_idx)) {
			return 0;
		}
		E.drawer_lsp_expanded &= ~(1u << (unsigned int)lookup.group_idx);
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 1;
	}
	if (lookup.kind == EDITOR_DRAWER_LSP_ENTRY_PROBLEM ||
			lookup.kind == EDITOR_DRAWER_LSP_ENTRY_PLACEHOLDER) {
		E.drawer_selected_index = lookup.group_visible_idx;
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 1;
	}
	return 0;
}

int editorDrawerLspToggleSelectionExpanded(int viewport_rows) {
	struct editorDrawerLspLookup lookup;
	if (!editorDrawerLspLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	if (lookup.kind != EDITOR_DRAWER_LSP_ENTRY_GROUP) {
		return 0;
	}
	E.drawer_lsp_expanded ^= 1u << (unsigned int)lookup.group_idx;
	editorDrawerClampSelectionAndScroll(viewport_rows);
	return 1;
}

int editorDrawerLspSelectedIsDirectory(void) {
	struct editorDrawerLspLookup lookup;
	if (!editorDrawerLspLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	return lookup.kind == EDITOR_DRAWER_LSP_ENTRY_ROOT ||
			lookup.kind == EDITOR_DRAWER_LSP_ENTRY_GROUP;
}

int editorDrawerSelectedLspLocation(const char **path_out, int *line_out, int *character_out) {
	if (path_out == NULL || line_out == NULL || character_out == NULL ||
			E.drawer_mode != EDITOR_DRAWER_MODE_LSP) {
		return 0;
	}
	struct editorDrawerLspLookup lookup;
	if (!editorDrawerLspLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	if (lookup.kind == EDITOR_DRAWER_LSP_ENTRY_PROBLEM) {
		if (lookup.problem.path == NULL || lookup.problem.path[0] == '\0') {
			return 0;
		}
		*path_out = lookup.problem.path;
		*line_out = lookup.problem.line;
		*character_out = lookup.problem.character;
		return 1;
	}
	if (lookup.kind == EDITOR_DRAWER_LSP_ENTRY_SYMBOL) {
		if (lookup.symbol.path == NULL || lookup.symbol.path[0] == '\0') {
			return 0;
		}
		*path_out = lookup.symbol.path;
		*line_out = lookup.symbol.line;
		*character_out = lookup.symbol.character;
		return 1;
	}
	return 0;
}
