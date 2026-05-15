#include "workspace/drawer.h"

#include "config/dap_config.h"
#include "debug/dap.h"
#include "language/lsp.h"
#include "language/syntax.h"
#include "support/file_io.h"
#include "workspace/drawer_internal.h"
#include "workspace/file_search.h"
#include "workspace/git.h"
#include "workspace/project_search.h"
#include "workspace/tabs.h"

#include <stdio.h>
#include <string.h>

struct editorDrawerMenuItem {
	const char *name;
	enum editorAction action;
};

struct editorDrawerMenuGroup {
	const char *name;
	const struct editorDrawerMenuItem *items;
	int item_count;
};

enum editorDrawerMenuEntryKind {
	EDITOR_DRAWER_MENU_ENTRY_ROOT = 0,
	EDITOR_DRAWER_MENU_ENTRY_GROUP,
	EDITOR_DRAWER_MENU_ENTRY_ITEM
};

struct editorDrawerMenuLookup {
	enum editorDrawerMenuEntryKind kind;
	int group_idx;
	int item_idx;
	int visible_idx;
	int parent_visible_idx;
	int group_visible_idx;
};

static const struct editorDrawerMenuItem editor_drawer_menu_search_items[] = {
	{"Find File", EDITOR_ACTION_FIND_FILE},
	{"Find in Buffer", EDITOR_ACTION_FIND},
	{"Search Project Text", EDITOR_ACTION_PROJECT_SEARCH},
	{"Find & replace", EDITOR_ACTION_FIND_REPLACE},
	{"Go to Line", EDITOR_ACTION_GOTO_LINE},
	{"Go to Matching Bracket", EDITOR_ACTION_GOTO_MATCHING_BRACKET},
	{"Go to Definition", EDITOR_ACTION_GOTO_DEFINITION},
};

static const struct editorDrawerMenuItem editor_drawer_menu_file_items[] = {
	{"Save", EDITOR_ACTION_SAVE},
	{"New Tab", EDITOR_ACTION_NEW_TAB},
	{"Close Tab", EDITOR_ACTION_CLOSE_TAB},
	{"New File...", EDITOR_ACTION_DRAWER_CREATE_FILE},
	{"New Folder...", EDITOR_ACTION_DRAWER_CREATE_FOLDER},
	{"Rename...", EDITOR_ACTION_DRAWER_RENAME},
	{"Delete...", EDITOR_ACTION_DRAWER_DELETE},
	{"Settings", EDITOR_ACTION_OPEN_SETTINGS},
	{"Quit", EDITOR_ACTION_QUIT},
};

static const struct editorDrawerMenuItem editor_drawer_menu_tabs_items[] = {
	{"Next Tab", EDITOR_ACTION_NEXT_TAB},
	{"Previous Tab", EDITOR_ACTION_PREV_TAB},
};

static const struct editorDrawerMenuItem editor_drawer_menu_edit_items[] = {
	{"Undo", EDITOR_ACTION_UNDO},
	{"Redo", EDITOR_ACTION_REDO},
	{"Toggle Selection", EDITOR_ACTION_TOGGLE_SELECTION},
	{"Copy Selection", EDITOR_ACTION_COPY_SELECTION},
	{"Cut Selection", EDITOR_ACTION_CUT_SELECTION},
	{"Paste", EDITOR_ACTION_PASTE},
	{"Delete Selection", EDITOR_ACTION_DELETE_SELECTION},
	{"Toggle Comment", EDITOR_ACTION_TOGGLE_COMMENT},
};

static const struct editorDrawerMenuItem editor_drawer_menu_view_items[] = {
	{"Project Files", EDITOR_ACTION_MAIN_MENU},
	{"Git Changes", EDITOR_ACTION_GIT_DRAWER},
	{"LSP", EDITOR_ACTION_LSP_DRAWER},
	{"DAP", EDITOR_ACTION_DAP_DRAWER},
	{"Collapse Drawer", EDITOR_ACTION_TOGGLE_DRAWER},
	{"Toggle Line Wrap", EDITOR_ACTION_TOGGLE_LINE_WRAP},
	{"Toggle Line Numbers", EDITOR_ACTION_TOGGLE_LINE_NUMBERS},
	{"Toggle Current Line", EDITOR_ACTION_TOGGLE_CURRENT_LINE_HIGHLIGHT},
};

static const struct editorDrawerMenuGroup editor_drawer_menu_groups[] = {
	{"Find", editor_drawer_menu_search_items,
			(int)(sizeof(editor_drawer_menu_search_items) /
					sizeof(editor_drawer_menu_search_items[0]))},
	{"File", editor_drawer_menu_file_items,
			(int)(sizeof(editor_drawer_menu_file_items) /
					sizeof(editor_drawer_menu_file_items[0]))},
	{"Tabs", editor_drawer_menu_tabs_items,
			(int)(sizeof(editor_drawer_menu_tabs_items) /
					sizeof(editor_drawer_menu_tabs_items[0]))},
	{"Edit", editor_drawer_menu_edit_items,
			(int)(sizeof(editor_drawer_menu_edit_items) /
					sizeof(editor_drawer_menu_edit_items[0]))},
	{"View", editor_drawer_menu_view_items,
			(int)(sizeof(editor_drawer_menu_view_items) /
					sizeof(editor_drawer_menu_view_items[0]))},
};

static const int editor_drawer_menu_group_count =
		(int)(sizeof(editor_drawer_menu_groups) / sizeof(editor_drawer_menu_groups[0]));

enum editorDrawerGitGroup {
	EDITOR_DRAWER_GIT_GROUP_STAGED = 0,
	EDITOR_DRAWER_GIT_GROUP_CHANGES,
	EDITOR_DRAWER_GIT_GROUP_UNTRACKED,
	EDITOR_DRAWER_GIT_GROUP_CONFLICTS,
	EDITOR_DRAWER_GIT_GROUP_COUNT
};

enum editorDrawerGitEntryKind {
	EDITOR_DRAWER_GIT_ENTRY_ROOT = 0,
	EDITOR_DRAWER_GIT_ENTRY_GROUP,
	EDITOR_DRAWER_GIT_ENTRY_FILE,
	EDITOR_DRAWER_GIT_ENTRY_PLACEHOLDER
};

struct editorDrawerGitLookup {
	enum editorDrawerGitEntryKind kind;
	int group_idx;
	int entry_idx;
	int item_idx;
	int item_count;
	int visible_idx;
	int parent_visible_idx;
	int group_visible_idx;
	char status_char;
};

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

enum editorDrawerDapGroup {
	EDITOR_DRAWER_DAP_GROUP_CONFIGURATIONS = 0,
	EDITOR_DRAWER_DAP_GROUP_BREAKPOINTS,
	EDITOR_DRAWER_DAP_GROUP_THREADS,
	EDITOR_DRAWER_DAP_GROUP_STACK,
	EDITOR_DRAWER_DAP_GROUP_VARIABLES,
	EDITOR_DRAWER_DAP_GROUP_OUTPUT,
	EDITOR_DRAWER_DAP_GROUP_COUNT
};

enum editorDrawerDapEntryKind {
	EDITOR_DRAWER_DAP_ENTRY_ROOT = 0,
	EDITOR_DRAWER_DAP_ENTRY_GROUP,
	EDITOR_DRAWER_DAP_ENTRY_LAUNCH,
	EDITOR_DRAWER_DAP_ENTRY_CREATE_PROMPT,
	EDITOR_DRAWER_DAP_ENTRY_DEFAULT,
	EDITOR_DRAWER_DAP_ENTRY_BREAKPOINT,
	EDITOR_DRAWER_DAP_ENTRY_THREAD,
	EDITOR_DRAWER_DAP_ENTRY_STACK_FRAME,
	EDITOR_DRAWER_DAP_ENTRY_SCOPE,
	EDITOR_DRAWER_DAP_ENTRY_VARIABLE,
	EDITOR_DRAWER_DAP_ENTRY_OUTPUT,
	EDITOR_DRAWER_DAP_ENTRY_PLACEHOLDER
};

struct editorDrawerDapLookup {
	enum editorDrawerDapEntryKind kind;
	int group_idx;
	int item_idx;
	int item_count;
	int visible_idx;
	int parent_visible_idx;
	int group_visible_idx;
};

static const char *editor_drawer_git_group_names[EDITOR_DRAWER_GIT_GROUP_COUNT] = {
	"Staged",
	"Changes",
	"Untracked",
	"Conflicts"
};

static const char *editor_drawer_lsp_group_names[EDITOR_DRAWER_LSP_GROUP_COUNT] = {
	"Problems",
	"Symbols"
};

static const char *editor_drawer_dap_group_names[EDITOR_DRAWER_DAP_GROUP_COUNT] = {
	"Configurations",
	"Breakpoints",
	"Threads",
	"Stack",
	"Variables",
	"Output"
};

static int editorDrawerMenuVisibleCount(void);
static int editorDrawerMenuLookupByVisibleIndex(int visible_idx,
		struct editorDrawerMenuLookup *lookup_out);
static void editorDrawerMenuEnsureDefaultExpanded(void);
static int editorDrawerGitVisibleCount(void);
static int editorDrawerGitLookupByVisibleIndex(int visible_idx,
		struct editorDrawerGitLookup *lookup_out);
static void editorDrawerGitEnsureDefaultExpanded(void);
static int editorDrawerGitGroupExpanded(int group_idx);
static int editorDrawerLspVisibleCount(void);
static int editorDrawerLspLookupByVisibleIndex(int visible_idx,
		struct editorDrawerLspLookup *lookup_out);
static void editorDrawerLspEnsureDefaultExpanded(void);
static int editorDrawerLspGroupExpanded(int group_idx);
static int editorDrawerDapVisibleCount(void);
static int editorDrawerDapLookupByVisibleIndex(int visible_idx,
		struct editorDrawerDapLookup *lookup_out);
static void editorDrawerDapEnsureDefaultExpanded(void);
static int editorDrawerDapGroupExpanded(int group_idx);

static unsigned int editorDrawerMenuAllGroupsMask(void) {
	unsigned int mask = 0;
	for (int i = 0; i < editor_drawer_menu_group_count; i++) {
		mask |= 1u << (unsigned int)i;
	}
	return mask;
}

static int editorDrawerMenuGroupExpanded(int group_idx) {
	if (group_idx < 0 || group_idx >= editor_drawer_menu_group_count) {
		return 0;
	}
	return (E.drawer_menu_expanded & (1u << (unsigned int)group_idx)) != 0;
}

static void editorDrawerMenuEnsureDefaultExpanded(void) {
	E.drawer_menu_expanded = editorDrawerMenuAllGroupsMask();
}

static int editorDrawerMenuVisibleCount(void) {
	int count = 1;
	for (int group_idx = 0; group_idx < editor_drawer_menu_group_count; group_idx++) {
		count++;
		if (editorDrawerMenuGroupExpanded(group_idx)) {
			count += editor_drawer_menu_groups[group_idx].item_count;
		}
	}
	return count;
}

static int editorDrawerMenuLookupByVisibleIndex(int visible_idx,
		struct editorDrawerMenuLookup *lookup_out) {
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
		lookup_out->kind = EDITOR_DRAWER_MENU_ENTRY_ROOT;
		return 1;
	}

	int cursor = 1;
	for (int group_idx = 0; group_idx < editor_drawer_menu_group_count; group_idx++) {
		int group_visible_idx = cursor;
		if (visible_idx == group_visible_idx) {
			lookup_out->kind = EDITOR_DRAWER_MENU_ENTRY_GROUP;
			lookup_out->group_idx = group_idx;
			lookup_out->parent_visible_idx = 0;
			lookup_out->group_visible_idx = group_visible_idx;
			return 1;
		}
		cursor++;

		if (!editorDrawerMenuGroupExpanded(group_idx)) {
			continue;
		}

		const struct editorDrawerMenuGroup *group = &editor_drawer_menu_groups[group_idx];
		for (int item_idx = 0; item_idx < group->item_count; item_idx++) {
			if (visible_idx == cursor) {
				lookup_out->kind = EDITOR_DRAWER_MENU_ENTRY_ITEM;
				lookup_out->group_idx = group_idx;
				lookup_out->item_idx = item_idx;
				lookup_out->parent_visible_idx = group_visible_idx;
				lookup_out->group_visible_idx = group_visible_idx;
				return 1;
			}
			cursor++;
		}
	}

	return 0;
}

static int editorGitEntryIsConflict(const struct editorGitEntry *entry) {
	char x = entry->index_status;
	char y = entry->worktree_status;
	return x == 'U' || y == 'U' || (x == 'A' && y == 'A') || (x == 'D' && y == 'D');
}

static int editorGitEntryIsUntracked(const struct editorGitEntry *entry) {
	return entry->index_status == '?' && entry->worktree_status == '?';
}

static int editorGitEntryInGroup(const struct editorGitEntry *entry, int group_idx) {
	if (entry == NULL) {
		return 0;
	}
	int conflict = editorGitEntryIsConflict(entry);
	int untracked = editorGitEntryIsUntracked(entry);
	switch (group_idx) {
	case EDITOR_DRAWER_GIT_GROUP_STAGED:
		if (conflict || untracked) {
			return 0;
		}
		return entry->index_status != ' ' && entry->index_status != '?';
	case EDITOR_DRAWER_GIT_GROUP_CHANGES:
		if (conflict || untracked) {
			return 0;
		}
		return entry->worktree_status != ' ' && entry->worktree_status != '?';
	case EDITOR_DRAWER_GIT_GROUP_UNTRACKED:
		return untracked;
	case EDITOR_DRAWER_GIT_GROUP_CONFLICTS:
		return conflict;
	default:
		return 0;
	}
}

static int editorDrawerGitGroupItemCount(int group_idx) {
	int count = 0;
	for (int i = 0; i < E.git_entry_count; i++) {
		if (editorGitEntryInGroup(&E.git_entries[i], group_idx)) {
			count++;
		}
	}
	return count;
}

static char editorDrawerGitStatusCharForGroup(const struct editorGitEntry *entry, int group_idx) {
	switch (group_idx) {
	case EDITOR_DRAWER_GIT_GROUP_STAGED:
		return entry->index_status;
	case EDITOR_DRAWER_GIT_GROUP_CHANGES:
		return entry->worktree_status;
	case EDITOR_DRAWER_GIT_GROUP_UNTRACKED:
		return '?';
	case EDITOR_DRAWER_GIT_GROUP_CONFLICTS:
		return 'U';
	default:
		return ' ';
	}
}

static int editorDrawerGitGroupExpanded(int group_idx) {
	if (group_idx < 0 || group_idx >= EDITOR_DRAWER_GIT_GROUP_COUNT) {
		return 0;
	}
	return (E.drawer_git_expanded & (1u << (unsigned int)group_idx)) != 0;
}

static unsigned int editorDrawerGitAllGroupsMask(void) {
	unsigned int mask = 0;
	for (int i = 0; i < EDITOR_DRAWER_GIT_GROUP_COUNT; i++) {
		mask |= 1u << (unsigned int)i;
	}
	return mask;
}

static void editorDrawerGitEnsureDefaultExpanded(void) {
	E.drawer_git_expanded = editorDrawerGitAllGroupsMask();
}

static int editorDrawerGitVisibleCount(void) {
	int count = 1;
	for (int group_idx = 0; group_idx < EDITOR_DRAWER_GIT_GROUP_COUNT; group_idx++) {
		count++;
		if (!editorDrawerGitGroupExpanded(group_idx)) {
			continue;
		}
		int item_count = editorDrawerGitGroupItemCount(group_idx);
		if (item_count == 0) {
			count++;
		} else {
			count += item_count;
		}
	}
	return count;
}

static int editorDrawerGitLookupByVisibleIndex(int visible_idx,
		struct editorDrawerGitLookup *lookup_out) {
	if (lookup_out == NULL || visible_idx < 0) {
		return 0;
	}

	memset(lookup_out, 0, sizeof(*lookup_out));
	lookup_out->visible_idx = visible_idx;
	lookup_out->group_idx = -1;
	lookup_out->entry_idx = -1;
	lookup_out->item_idx = -1;
	lookup_out->parent_visible_idx = -1;
	lookup_out->group_visible_idx = -1;
	lookup_out->status_char = ' ';

	if (visible_idx == 0) {
		lookup_out->kind = EDITOR_DRAWER_GIT_ENTRY_ROOT;
		return 1;
	}

	int cursor = 1;
	for (int group_idx = 0; group_idx < EDITOR_DRAWER_GIT_GROUP_COUNT; group_idx++) {
		int group_visible_idx = cursor;
		if (visible_idx == group_visible_idx) {
			lookup_out->kind = EDITOR_DRAWER_GIT_ENTRY_GROUP;
			lookup_out->group_idx = group_idx;
			lookup_out->parent_visible_idx = 0;
			lookup_out->group_visible_idx = group_visible_idx;
			lookup_out->item_count = editorDrawerGitGroupItemCount(group_idx);
			return 1;
		}
		cursor++;

		if (!editorDrawerGitGroupExpanded(group_idx)) {
			continue;
		}

		int item_count = editorDrawerGitGroupItemCount(group_idx);
		if (item_count == 0) {
			if (visible_idx == cursor) {
				lookup_out->kind = EDITOR_DRAWER_GIT_ENTRY_PLACEHOLDER;
				lookup_out->group_idx = group_idx;
				lookup_out->parent_visible_idx = group_visible_idx;
				lookup_out->group_visible_idx = group_visible_idx;
				lookup_out->item_count = 0;
				return 1;
			}
			cursor++;
			continue;
		}

		int item_idx = 0;
		for (int i = 0; i < E.git_entry_count; i++) {
			if (!editorGitEntryInGroup(&E.git_entries[i], group_idx)) {
				continue;
			}
			if (visible_idx == cursor) {
				lookup_out->kind = EDITOR_DRAWER_GIT_ENTRY_FILE;
				lookup_out->group_idx = group_idx;
				lookup_out->entry_idx = i;
				lookup_out->item_idx = item_idx;
				lookup_out->item_count = item_count;
				lookup_out->parent_visible_idx = group_visible_idx;
				lookup_out->group_visible_idx = group_visible_idx;
				lookup_out->status_char = editorDrawerGitStatusCharForGroup(
						&E.git_entries[i], group_idx);
				return 1;
			}
			item_idx++;
			cursor++;
		}
	}
	return 0;
}

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

static int editorDrawerLspProblemAt(int problem_idx,
		struct editorDrawerLspProblem *problem_out) {
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

static int editorDrawerLspSymbolAt(int symbol_idx,
		struct editorDrawerLspSymbolEntry *symbol_out) {
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

static int editorDrawerLspVisibleCount(void) {
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

static unsigned int editorDrawerDapAllGroupsMask(void) {
	unsigned int mask = 0;
	for (int i = 0; i < EDITOR_DRAWER_DAP_GROUP_COUNT; i++) {
		mask |= 1u << (unsigned int)i;
	}
	return mask;
}

static int editorDrawerDapGroupExpanded(int group_idx) {
	if (group_idx < 0 || group_idx >= EDITOR_DRAWER_DAP_GROUP_COUNT) {
		return 0;
	}
	return (E.drawer_dap_expanded & (1u << (unsigned int)group_idx)) != 0;
}

static void editorDrawerDapEnsureDefaultExpanded(void) {
	E.drawer_dap_expanded = editorDrawerDapAllGroupsMask();
}

static int editorDrawerDapGroupItemCount(int group_idx) {
	switch (group_idx) {
	case EDITOR_DRAWER_DAP_GROUP_CONFIGURATIONS:
		if (E.dap_launch_count > 0) {
			return E.dap_launch_count;
		}
		if (E.dap_default_count > 0) {
			return 1 + E.dap_default_count;
		}
		return 1;
	case EDITOR_DRAWER_DAP_GROUP_BREAKPOINTS:
		return E.dap_breakpoint_count;
	case EDITOR_DRAWER_DAP_GROUP_THREADS:
		return E.dap_thread_count;
	case EDITOR_DRAWER_DAP_GROUP_STACK:
		return E.dap_stack_frame_count;
	case EDITOR_DRAWER_DAP_GROUP_VARIABLES:
		return E.dap_scope_count + E.dap_variable_count;
	case EDITOR_DRAWER_DAP_GROUP_OUTPUT:
		return E.dap_output_len > 0 ? 1 : 0;
	default:
		return 0;
	}
}

static int editorDrawerDapVisibleCount(void) {
	int count = 1;
	for (int group_idx = 0; group_idx < EDITOR_DRAWER_DAP_GROUP_COUNT; group_idx++) {
		count++;
		if (!editorDrawerDapGroupExpanded(group_idx)) {
			continue;
		}
		int item_count = editorDrawerDapGroupItemCount(group_idx);
		count += item_count > 0 ? item_count : 1;
	}
	return count;
}

static int editorDrawerDapLookupByVisibleIndex(int visible_idx,
		struct editorDrawerDapLookup *lookup_out) {
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
		lookup_out->kind = EDITOR_DRAWER_DAP_ENTRY_ROOT;
		return 1;
	}

	int cursor = 1;
	for (int group_idx = 0; group_idx < EDITOR_DRAWER_DAP_GROUP_COUNT; group_idx++) {
		int group_visible_idx = cursor;
		int item_count = editorDrawerDapGroupItemCount(group_idx);
		if (visible_idx == group_visible_idx) {
			lookup_out->kind = EDITOR_DRAWER_DAP_ENTRY_GROUP;
			lookup_out->group_idx = group_idx;
			lookup_out->item_count = item_count;
			lookup_out->parent_visible_idx = 0;
			lookup_out->group_visible_idx = group_visible_idx;
			return 1;
		}
		cursor++;
		if (!editorDrawerDapGroupExpanded(group_idx)) {
			continue;
		}
		if (item_count == 0) {
			if (visible_idx == cursor) {
				lookup_out->kind = EDITOR_DRAWER_DAP_ENTRY_PLACEHOLDER;
				lookup_out->group_idx = group_idx;
				lookup_out->parent_visible_idx = group_visible_idx;
				lookup_out->group_visible_idx = group_visible_idx;
				return 1;
			}
			cursor++;
			continue;
		}
		for (int item_idx = 0; item_idx < item_count; item_idx++) {
			if (visible_idx != cursor) {
				cursor++;
				continue;
			}
			lookup_out->group_idx = group_idx;
			lookup_out->item_idx = item_idx;
			lookup_out->item_count = item_count;
			lookup_out->parent_visible_idx = group_visible_idx;
			lookup_out->group_visible_idx = group_visible_idx;
			if (group_idx == EDITOR_DRAWER_DAP_GROUP_CONFIGURATIONS) {
				if (E.dap_launch_count > 0) {
					lookup_out->kind = EDITOR_DRAWER_DAP_ENTRY_LAUNCH;
				} else if (E.dap_default_count > 0 && item_idx == 0) {
					lookup_out->kind = EDITOR_DRAWER_DAP_ENTRY_CREATE_PROMPT;
				} else if (E.dap_default_count > 0) {
					lookup_out->kind = EDITOR_DRAWER_DAP_ENTRY_DEFAULT;
					lookup_out->item_idx = item_idx - 1;
				} else {
					lookup_out->kind = EDITOR_DRAWER_DAP_ENTRY_PLACEHOLDER;
				}
				return 1;
			}
			if (group_idx == EDITOR_DRAWER_DAP_GROUP_BREAKPOINTS) {
				lookup_out->kind = EDITOR_DRAWER_DAP_ENTRY_BREAKPOINT;
				return 1;
			}
			if (group_idx == EDITOR_DRAWER_DAP_GROUP_THREADS) {
				lookup_out->kind = EDITOR_DRAWER_DAP_ENTRY_THREAD;
				return 1;
			}
			if (group_idx == EDITOR_DRAWER_DAP_GROUP_STACK) {
				lookup_out->kind = EDITOR_DRAWER_DAP_ENTRY_STACK_FRAME;
				return 1;
			}
			if (group_idx == EDITOR_DRAWER_DAP_GROUP_VARIABLES) {
				if (item_idx < E.dap_scope_count) {
					lookup_out->kind = EDITOR_DRAWER_DAP_ENTRY_SCOPE;
				} else {
					lookup_out->kind = EDITOR_DRAWER_DAP_ENTRY_VARIABLE;
					lookup_out->item_idx = item_idx - E.dap_scope_count;
				}
				return 1;
			}
			if (group_idx == EDITOR_DRAWER_DAP_GROUP_OUTPUT) {
				lookup_out->kind = EDITOR_DRAWER_DAP_ENTRY_OUTPUT;
				return 1;
			}
			return 0;
		}
	}
	return 0;
}

int editorDrawerMainMenuToggle(void) {
	if (E.drawer_mode == EDITOR_DRAWER_MODE_MAIN_MENU) {
		E.drawer_mode = EDITOR_DRAWER_MODE_TREE;
		E.drawer_selected_index = -1;
		E.drawer_rowoff = 0;
		E.drawer_resize_active = 0;
		E.pane_focus = EDITOR_PANE_DRAWER;
		return 1;
	}

	if (editorFileSearchIsActive()) {
		editorFileSearchExit(1);
	}
	if (editorProjectSearchIsActive()) {
		editorProjectSearchExit(1);
	}
	editorDrawerMenuEnsureDefaultExpanded();
	E.drawer_mode = EDITOR_DRAWER_MODE_MAIN_MENU;
	E.drawer_selected_index = -1;
	E.drawer_rowoff = 0;
	E.drawer_resize_active = 0;
	(void)editorDrawerSetCollapsed(0);
	E.pane_focus = EDITOR_PANE_DRAWER;
	return 1;
}

int editorDrawerGitToggle(void) {
	if (E.drawer_mode == EDITOR_DRAWER_MODE_GIT) {
		E.drawer_mode = EDITOR_DRAWER_MODE_TREE;
		E.drawer_selected_index = -1;
		E.drawer_rowoff = 0;
		E.drawer_resize_active = 0;
		E.pane_focus = EDITOR_PANE_DRAWER;
		return 1;
	}

	if (editorFileSearchIsActive()) {
		editorFileSearchExit(1);
	}
	if (editorProjectSearchIsActive()) {
		editorProjectSearchExit(1);
	}
	if (E.git_repo_root != NULL) {
		editorGitRefresh();
	}
	editorDrawerGitEnsureDefaultExpanded();
	E.drawer_mode = EDITOR_DRAWER_MODE_GIT;
	E.drawer_selected_index = -1;
	E.drawer_rowoff = 0;
	E.drawer_resize_active = 0;
	(void)editorDrawerSetCollapsed(0);
	E.pane_focus = EDITOR_PANE_DRAWER;
	return 1;
}

int editorDrawerLspToggle(void) {
	if (E.drawer_mode == EDITOR_DRAWER_MODE_LSP) {
		E.drawer_mode = EDITOR_DRAWER_MODE_TREE;
		E.drawer_selected_index = -1;
		E.drawer_rowoff = 0;
		E.drawer_resize_active = 0;
		E.pane_focus = EDITOR_PANE_DRAWER;
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
	E.pane_focus = EDITOR_PANE_DRAWER;
	return 1;
}

int editorDrawerDapToggle(void) {
	if (E.drawer_mode == EDITOR_DRAWER_MODE_DAP) {
		E.drawer_mode = EDITOR_DRAWER_MODE_TREE;
		E.drawer_selected_index = -1;
		E.drawer_rowoff = 0;
		E.drawer_resize_active = 0;
		E.pane_focus = EDITOR_PANE_DRAWER;
		return 1;
	}

	if (editorFileSearchIsActive()) {
		editorFileSearchExit(1);
	}
	if (editorProjectSearchIsActive()) {
		editorProjectSearchExit(1);
	}
	(void)editorDapConfigReloadProject(E.drawer_root_path);
	editorDrawerDapEnsureDefaultExpanded();
	E.drawer_mode = EDITOR_DRAWER_MODE_DAP;
	E.drawer_selected_index = -1;
	E.drawer_rowoff = 0;
	E.drawer_resize_active = 0;
	(void)editorDrawerSetCollapsed(0);
	E.pane_focus = EDITOR_PANE_DRAWER;
	return 1;
}

int editorDrawerVisibleCount(void) {
	if (editorFileSearchIsActive()) {
		return editorFileSearchVisibleCount();
	}
	if (editorProjectSearchIsActive()) {
		return editorProjectSearchVisibleCount();
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_MAIN_MENU) {
		return editorDrawerMenuVisibleCount();
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_GIT) {
		return editorDrawerGitVisibleCount();
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_LSP) {
		return editorDrawerLspVisibleCount();
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_DAP) {
		return editorDrawerDapVisibleCount();
	}
	return editorDrawerCountVisibleFromNode(E.drawer_root);
}

int editorDrawerGetVisibleEntry(int visible_idx, struct editorDrawerEntryView *view_out) {
	if (view_out == NULL) {
		return 0;
	}
	if (editorFileSearchIsActive()) {
		return editorFileSearchGetVisibleEntry(visible_idx, view_out);
	}
	if (editorProjectSearchIsActive()) {
		return editorProjectSearchGetVisibleEntry(visible_idx, view_out);
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_MAIN_MENU) {
		struct editorDrawerMenuLookup lookup;
		if (!editorDrawerMenuLookupByVisibleIndex(visible_idx, &lookup)) {
			return 0;
		}

		memset(view_out, 0, sizeof(*view_out));
		view_out->is_selected = visible_idx == E.drawer_selected_index;
		view_out->parent_visible_idx = lookup.parent_visible_idx;
		switch (lookup.kind) {
		case EDITOR_DRAWER_MENU_ENTRY_ROOT:
			view_out->name = "Main Menu";
			view_out->depth = 0;
			view_out->is_dir = 1;
			view_out->is_expanded = 1;
			view_out->is_root = 1;
			view_out->is_last_sibling = 1;
			return 1;
		case EDITOR_DRAWER_MENU_ENTRY_GROUP:
			view_out->name = editor_drawer_menu_groups[lookup.group_idx].name;
			view_out->depth = 1;
			view_out->is_dir = 1;
			view_out->is_expanded = editorDrawerMenuGroupExpanded(lookup.group_idx);
			view_out->is_last_sibling = lookup.group_idx == editor_drawer_menu_group_count - 1;
			return 1;
		case EDITOR_DRAWER_MENU_ENTRY_ITEM:
			view_out->name =
					editor_drawer_menu_groups[lookup.group_idx].items[lookup.item_idx].name;
			view_out->depth = 2;
			view_out->is_last_sibling =
					lookup.item_idx ==
					editor_drawer_menu_groups[lookup.group_idx].item_count - 1;
			return 1;
		default:
			return 0;
		}
	}

	if (E.drawer_mode == EDITOR_DRAWER_MODE_GIT) {
		static char git_name_buf[PATH_MAX + 8];
		struct editorDrawerGitLookup lookup;
		if (!editorDrawerGitLookupByVisibleIndex(visible_idx, &lookup)) {
			return 0;
		}

		memset(view_out, 0, sizeof(*view_out));
		view_out->is_selected = visible_idx == E.drawer_selected_index;
		view_out->parent_visible_idx = lookup.parent_visible_idx;
		switch (lookup.kind) {
		case EDITOR_DRAWER_GIT_ENTRY_ROOT:
			view_out->name = "Git";
			view_out->depth = 0;
			view_out->is_dir = 1;
			view_out->is_expanded = 1;
			view_out->is_root = 1;
			view_out->is_last_sibling = 1;
			return 1;
		case EDITOR_DRAWER_GIT_ENTRY_GROUP:
			view_out->name = editor_drawer_git_group_names[lookup.group_idx];
			view_out->depth = 1;
			view_out->is_dir = 1;
			view_out->is_expanded = editorDrawerGitGroupExpanded(lookup.group_idx);
			view_out->is_last_sibling =
					lookup.group_idx == EDITOR_DRAWER_GIT_GROUP_COUNT - 1;
			return 1;
		case EDITOR_DRAWER_GIT_ENTRY_FILE: {
			const struct editorGitEntry *entry = &E.git_entries[lookup.entry_idx];
			char status = lookup.status_char;
			if (status == ' ' || status == '\0') {
				status = '?';
			}
			snprintf(git_name_buf, sizeof(git_name_buf), "%c %s", status, entry->rel_path);
			view_out->name = git_name_buf;
			view_out->depth = 2;
			view_out->is_last_sibling = lookup.item_idx == lookup.item_count - 1;
			view_out->git_status = entry->status;
			return 1;
		}
		case EDITOR_DRAWER_GIT_ENTRY_PLACEHOLDER:
			view_out->name = "(empty)";
			view_out->depth = 2;
			view_out->is_placeholder = 1;
			view_out->is_last_sibling = 1;
			return 1;
		default:
			return 0;
		}
	}

	if (E.drawer_mode == EDITOR_DRAWER_MODE_LSP) {
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
				snprintf(lsp_name_buf, sizeof(lsp_name_buf), "Problems (%d)",
						lookup.item_count);
				view_out->name = lsp_name_buf;
			} else {
				view_out->name = editor_drawer_lsp_group_names[lookup.group_idx];
			}
			view_out->depth = 1;
			view_out->is_dir = 1;
			view_out->is_expanded = editorDrawerLspGroupExpanded(lookup.group_idx);
			view_out->is_last_sibling =
					lookup.group_idx == EDITOR_DRAWER_LSP_VISIBLE_GROUP_COUNT - 1;
			return 1;
		case EDITOR_DRAWER_LSP_ENTRY_PROBLEM: {
			const char *path = lookup.problem.path != NULL ? lookup.problem.path : "";
			const char *slash = strrchr(path, '/');
			const char *base = slash != NULL ? slash + 1 : path;
			const char *message = lookup.problem.message != NULL ? lookup.problem.message : "";
			const char *kind = lookup.problem.source == EDITOR_DRAWER_LSP_PROBLEM_SYNTAX ?
					"Syntax" : "Info";
			if (lookup.problem.source == EDITOR_DRAWER_LSP_PROBLEM_LSP) {
				if (lookup.problem.severity == 1) {
					kind = "Error";
				} else if (lookup.problem.severity == 2) {
					kind = "Warning";
				}
			}
			snprintf(lsp_name_buf, sizeof(lsp_name_buf), "%s %s:%d:%d %s", kind,
					base != NULL && base[0] != '\0' ? base : "(untitled)",
					lookup.problem.line + 1, lookup.problem.character + 1, message);
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
					lookup.symbol.name : "(unnamed)";
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
			view_out->name = lookup.group_idx == EDITOR_DRAWER_LSP_GROUP_SYMBOLS ?
					"(symbols not loaded yet)" : "(none)";
			view_out->depth = 2;
			view_out->is_placeholder = 1;
			view_out->is_last_sibling = 1;
			return 1;
		default:
			return 0;
		}
	}

	if (E.drawer_mode == EDITOR_DRAWER_MODE_DAP) {
		static char dap_name_buf[PATH_MAX + 128];
		struct editorDrawerDapLookup lookup;
		if (!editorDrawerDapLookupByVisibleIndex(visible_idx, &lookup)) {
			return 0;
		}

		memset(view_out, 0, sizeof(*view_out));
		view_out->is_selected = visible_idx == E.drawer_selected_index;
		view_out->parent_visible_idx = lookup.parent_visible_idx;
		switch (lookup.kind) {
		case EDITOR_DRAWER_DAP_ENTRY_ROOT:
			snprintf(dap_name_buf, sizeof(dap_name_buf), "DAP%s%s",
					E.dap_running ? " - running" : "",
					E.dap_stopped ? " (stopped)" : "");
			view_out->name = dap_name_buf;
			view_out->depth = 0;
			view_out->is_dir = 1;
			view_out->is_expanded = 1;
			view_out->is_root = 1;
			view_out->is_last_sibling = 1;
			return 1;
		case EDITOR_DRAWER_DAP_ENTRY_GROUP:
			if (lookup.group_idx == EDITOR_DRAWER_DAP_GROUP_CONFIGURATIONS) {
				snprintf(dap_name_buf, sizeof(dap_name_buf), "Configurations (%d)",
						E.dap_launch_count);
				view_out->name = dap_name_buf;
			} else if (lookup.group_idx == EDITOR_DRAWER_DAP_GROUP_BREAKPOINTS) {
				snprintf(dap_name_buf, sizeof(dap_name_buf), "Breakpoints (%d)",
						E.dap_breakpoint_count);
				view_out->name = dap_name_buf;
			} else {
				view_out->name = editor_drawer_dap_group_names[lookup.group_idx];
			}
			view_out->depth = 1;
			view_out->is_dir = 1;
			view_out->is_expanded = editorDrawerDapGroupExpanded(lookup.group_idx);
			view_out->is_last_sibling = lookup.group_idx == EDITOR_DRAWER_DAP_GROUP_COUNT - 1;
			return 1;
		case EDITOR_DRAWER_DAP_ENTRY_LAUNCH: {
			const struct editorDapLaunchConfig *config = &E.dap_launches[lookup.item_idx];
			snprintf(dap_name_buf, sizeof(dap_name_buf), "%s%s%s",
					lookup.item_idx == E.dap_selected_launch ? "* " : "",
					config->name[0] != '\0' ? config->name : config->id,
					editorDapAdapterById(config->adapter) != NULL ? "" :
							" (missing adapter)");
			view_out->name = dap_name_buf;
			view_out->depth = 2;
			view_out->is_last_sibling = lookup.item_idx == lookup.item_count - 1;
			return 1;
		}
		case EDITOR_DRAWER_DAP_ENTRY_CREATE_PROMPT:
			view_out->name = "Create debug config from default";
			view_out->depth = 2;
			return 1;
		case EDITOR_DRAWER_DAP_ENTRY_DEFAULT: {
			const struct editorDapLaunchConfig *config = &E.dap_defaults[lookup.item_idx];
			snprintf(dap_name_buf, sizeof(dap_name_buf), "+ %s",
					config->name[0] != '\0' ? config->name : config->id);
			view_out->name = dap_name_buf;
			view_out->depth = 3;
			view_out->is_last_sibling = lookup.item_idx == E.dap_default_count - 1;
			return 1;
		}
		case EDITOR_DRAWER_DAP_ENTRY_BREAKPOINT: {
			const struct editorDapBreakpoint *bp = &E.dap_breakpoints[lookup.item_idx];
			const char *slash = strrchr(bp->path, '/');
			const char *base = slash != NULL ? slash + 1 : bp->path;
			snprintf(dap_name_buf, sizeof(dap_name_buf), "%s:%d", base, bp->line + 1);
			view_out->name = dap_name_buf;
			view_out->path = bp->path;
			view_out->line = bp->line;
			view_out->depth = 2;
			view_out->is_last_sibling = lookup.item_idx == lookup.item_count - 1;
			return 1;
		}
		case EDITOR_DRAWER_DAP_ENTRY_THREAD:
			snprintf(dap_name_buf, sizeof(dap_name_buf), "%d %s",
					E.dap_threads[lookup.item_idx].id, E.dap_threads[lookup.item_idx].name);
			view_out->name = dap_name_buf;
			view_out->depth = 2;
			view_out->is_last_sibling = lookup.item_idx == lookup.item_count - 1;
			return 1;
		case EDITOR_DRAWER_DAP_ENTRY_STACK_FRAME: {
			const struct editorDapStackFrame *frame = &E.dap_stack_frames[lookup.item_idx];
			snprintf(dap_name_buf, sizeof(dap_name_buf), "%s:%d",
					frame->name[0] != '\0' ? frame->name : "(frame)", frame->line);
			view_out->name = dap_name_buf;
			view_out->path = frame->path;
			view_out->line = frame->line > 0 ? frame->line - 1 : 0;
			view_out->character = frame->column > 0 ? frame->column - 1 : 0;
			view_out->depth = 2;
			view_out->is_last_sibling = lookup.item_idx == lookup.item_count - 1;
			return 1;
		}
		case EDITOR_DRAWER_DAP_ENTRY_SCOPE:
			view_out->name = E.dap_scopes[lookup.item_idx].name;
			view_out->depth = 2;
			return 1;
		case EDITOR_DRAWER_DAP_ENTRY_VARIABLE:
			snprintf(dap_name_buf, sizeof(dap_name_buf), "%s = %s",
					E.dap_variables[lookup.item_idx].name,
					E.dap_variables[lookup.item_idx].value);
			view_out->name = dap_name_buf;
			view_out->depth = 3;
			return 1;
		case EDITOR_DRAWER_DAP_ENTRY_OUTPUT:
			snprintf(dap_name_buf, sizeof(dap_name_buf), "%.120s", E.dap_output);
			view_out->name = dap_name_buf;
			view_out->depth = 2;
			return 1;
		case EDITOR_DRAWER_DAP_ENTRY_PLACEHOLDER:
			if (lookup.group_idx == EDITOR_DRAWER_DAP_GROUP_CONFIGURATIONS) {
				view_out->name = E.dap_project_config_invalid ?
						"Invalid .rotide.toml DAP config" :
						"No DAP defaults in ~/.rotide/config.toml";
			} else {
				view_out->name = "(none)";
			}
			view_out->depth = 2;
			view_out->is_placeholder = 1;
			view_out->is_last_sibling = 1;
			return 1;
		default:
			return 0;
		}
	}

	struct editorDrawerLookup lookup;
	if (!editorDrawerLookupByVisibleIndex(visible_idx, &lookup)) {
		return 0;
	}

	memset(view_out, 0, sizeof(*view_out));
	view_out->name = lookup.node->name;
	view_out->path = lookup.node->path;
	view_out->depth = lookup.depth;
	view_out->is_dir = lookup.node->is_dir;
	view_out->is_expanded = lookup.node->is_expanded;
	view_out->is_selected = visible_idx == E.drawer_selected_index;
	view_out->has_scan_error = lookup.node->scan_error;
	view_out->is_root = lookup.node == E.drawer_root;
	view_out->parent_visible_idx = lookup.parent_visible_idx;
	if (lookup.node->parent != NULL && lookup.node->parent->child_count > 0 &&
			lookup.node->parent->children[lookup.node->parent->child_count - 1] == lookup.node) {
		view_out->is_last_sibling = 1;
	} else {
		view_out->is_last_sibling = lookup.node->parent == NULL;
	}
	view_out->is_active_file = !lookup.node->is_dir && E.filename != NULL &&
			editorPathsReferToSameFile(lookup.node->path, E.filename);
	if (E.git_repo_root != NULL) {
		if (lookup.node->is_dir) {
			view_out->git_status = editorGitDirStatus(lookup.node->path);
		} else {
			view_out->git_status = editorGitFileStatus(lookup.node->path);
		}
	}
	return 1;
}

int editorDrawerExpandSelection(int viewport_rows) {
	if (editorFileSearchIsActive() || editorProjectSearchIsActive()) {
		return 0;
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_MAIN_MENU) {
		struct editorDrawerMenuLookup lookup;
		if (!editorDrawerMenuLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
			return 0;
		}
		if (lookup.kind == EDITOR_DRAWER_MENU_ENTRY_ROOT) {
			editorDrawerClampSelectionAndScroll(viewport_rows);
			return 0;
		}
		if (lookup.kind != EDITOR_DRAWER_MENU_ENTRY_GROUP ||
				editorDrawerMenuGroupExpanded(lookup.group_idx)) {
			return 0;
		}
		E.drawer_menu_expanded |= 1u << (unsigned int)lookup.group_idx;
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 1;
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_GIT) {
		struct editorDrawerGitLookup lookup;
		if (!editorDrawerGitLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
			return 0;
		}
		if (lookup.kind == EDITOR_DRAWER_GIT_ENTRY_ROOT) {
			editorDrawerClampSelectionAndScroll(viewport_rows);
			return 0;
		}
		if (lookup.kind != EDITOR_DRAWER_GIT_ENTRY_GROUP ||
				editorDrawerGitGroupExpanded(lookup.group_idx)) {
			return 0;
		}
		E.drawer_git_expanded |= 1u << (unsigned int)lookup.group_idx;
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 1;
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_LSP) {
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
	if (E.drawer_mode == EDITOR_DRAWER_MODE_DAP) {
		struct editorDrawerDapLookup lookup;
		if (!editorDrawerDapLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
			return 0;
		}
		if (lookup.kind == EDITOR_DRAWER_DAP_ENTRY_ROOT) {
			editorDrawerClampSelectionAndScroll(viewport_rows);
			return 0;
		}
		if (lookup.kind != EDITOR_DRAWER_DAP_ENTRY_GROUP ||
				editorDrawerDapGroupExpanded(lookup.group_idx)) {
			return 0;
		}
		E.drawer_dap_expanded |= 1u << (unsigned int)lookup.group_idx;
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 1;
	}

	struct editorDrawerLookup lookup;
	if (!editorDrawerLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	if (!lookup.node->is_dir) {
		return 0;
	}

	if (lookup.node == E.drawer_root) {
		lookup.node->is_expanded = 1;
		(void)editorDrawerEnsureScanned(lookup.node);
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 1;
	}

	lookup.node->is_expanded = 1;
	(void)editorDrawerEnsureScanned(lookup.node);
	editorDrawerClampSelectionAndScroll(viewport_rows);
	return 1;
}

int editorDrawerCollapseSelection(int viewport_rows) {
	if (editorFileSearchIsActive() || editorProjectSearchIsActive()) {
		return 0;
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_MAIN_MENU) {
		struct editorDrawerMenuLookup lookup;
		if (!editorDrawerMenuLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
			return 0;
		}
		if (lookup.kind == EDITOR_DRAWER_MENU_ENTRY_ROOT) {
			editorDrawerClampSelectionAndScroll(viewport_rows);
			return 0;
		}
		if (lookup.kind == EDITOR_DRAWER_MENU_ENTRY_GROUP) {
			if (!editorDrawerMenuGroupExpanded(lookup.group_idx)) {
				return 0;
			}
			E.drawer_menu_expanded &= ~(1u << (unsigned int)lookup.group_idx);
			editorDrawerClampSelectionAndScroll(viewport_rows);
			return 1;
		}
		if (lookup.kind == EDITOR_DRAWER_MENU_ENTRY_ITEM) {
			E.drawer_selected_index = lookup.group_visible_idx;
			editorDrawerClampSelectionAndScroll(viewport_rows);
			return 1;
		}
		return 0;
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_GIT) {
		struct editorDrawerGitLookup lookup;
		if (!editorDrawerGitLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
			return 0;
		}
		if (lookup.kind == EDITOR_DRAWER_GIT_ENTRY_ROOT) {
			editorDrawerClampSelectionAndScroll(viewport_rows);
			return 0;
		}
		if (lookup.kind == EDITOR_DRAWER_GIT_ENTRY_GROUP) {
			if (!editorDrawerGitGroupExpanded(lookup.group_idx)) {
				return 0;
			}
			E.drawer_git_expanded &= ~(1u << (unsigned int)lookup.group_idx);
			editorDrawerClampSelectionAndScroll(viewport_rows);
			return 1;
		}
		if (lookup.kind == EDITOR_DRAWER_GIT_ENTRY_FILE ||
				lookup.kind == EDITOR_DRAWER_GIT_ENTRY_PLACEHOLDER) {
			E.drawer_selected_index = lookup.group_visible_idx;
			editorDrawerClampSelectionAndScroll(viewport_rows);
			return 1;
		}
		return 0;
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_LSP) {
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
	if (E.drawer_mode == EDITOR_DRAWER_MODE_DAP) {
		struct editorDrawerDapLookup lookup;
		if (!editorDrawerDapLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
			return 0;
		}
		if (lookup.kind == EDITOR_DRAWER_DAP_ENTRY_ROOT) {
			editorDrawerClampSelectionAndScroll(viewport_rows);
			return 0;
		}
		if (lookup.kind == EDITOR_DRAWER_DAP_ENTRY_GROUP) {
			if (!editorDrawerDapGroupExpanded(lookup.group_idx)) {
				return 0;
			}
			E.drawer_dap_expanded &= ~(1u << (unsigned int)lookup.group_idx);
			editorDrawerClampSelectionAndScroll(viewport_rows);
			return 1;
		}
		E.drawer_selected_index = lookup.group_visible_idx;
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 1;
	}
	struct editorDrawerLookup lookup;
	if (!editorDrawerLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}

	if (lookup.node == E.drawer_root) {
		lookup.node->is_expanded = 1;
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 0;
	}

	if (lookup.node->is_dir && lookup.node->is_expanded) {
		lookup.node->is_expanded = 0;
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 1;
	}

	if (lookup.parent_visible_idx >= 0) {
		E.drawer_selected_index = lookup.parent_visible_idx;
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 1;
	}

	return 0;
}

int editorDrawerToggleSelectionExpanded(int viewport_rows) {
	if (editorFileSearchIsActive() || editorProjectSearchIsActive()) {
		return 0;
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_MAIN_MENU) {
		struct editorDrawerMenuLookup lookup;
		if (!editorDrawerMenuLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
			return 0;
		}
		if (lookup.kind == EDITOR_DRAWER_MENU_ENTRY_ROOT) {
			return 0;
		}
		if (lookup.kind != EDITOR_DRAWER_MENU_ENTRY_GROUP) {
			return 0;
		}
		E.drawer_menu_expanded ^= 1u << (unsigned int)lookup.group_idx;
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 1;
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_GIT) {
		struct editorDrawerGitLookup lookup;
		if (!editorDrawerGitLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
			return 0;
		}
		if (lookup.kind != EDITOR_DRAWER_GIT_ENTRY_GROUP) {
			return 0;
		}
		E.drawer_git_expanded ^= 1u << (unsigned int)lookup.group_idx;
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 1;
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_LSP) {
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
	struct editorDrawerLookup lookup;
	if (!editorDrawerLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	if (!lookup.node->is_dir) {
		return 0;
	}
	if (lookup.node == E.drawer_root) {
		lookup.node->is_expanded = 1;
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 0;
	}

	if (lookup.node->is_expanded) {
		lookup.node->is_expanded = 0;
	} else {
		lookup.node->is_expanded = 1;
		(void)editorDrawerEnsureScanned(lookup.node);
	}

	editorDrawerClampSelectionAndScroll(viewport_rows);
	return 1;
}

int editorDrawerSelectedIsDirectory(void) {
	if (editorFileSearchIsActive()) {
		return editorFileSearchSelectedIsDirectory();
	}
	if (editorProjectSearchIsActive()) {
		return editorProjectSearchSelectedIsDirectory();
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_MAIN_MENU) {
		struct editorDrawerMenuLookup lookup;
		if (!editorDrawerMenuLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
			return 0;
		}
		return lookup.kind == EDITOR_DRAWER_MENU_ENTRY_ROOT ||
				lookup.kind == EDITOR_DRAWER_MENU_ENTRY_GROUP;
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_GIT) {
		struct editorDrawerGitLookup lookup;
		if (!editorDrawerGitLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
			return 0;
		}
		return lookup.kind == EDITOR_DRAWER_GIT_ENTRY_ROOT ||
				lookup.kind == EDITOR_DRAWER_GIT_ENTRY_GROUP;
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_LSP) {
		struct editorDrawerLspLookup lookup;
		if (!editorDrawerLspLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
			return 0;
		}
		return lookup.kind == EDITOR_DRAWER_LSP_ENTRY_ROOT ||
				lookup.kind == EDITOR_DRAWER_LSP_ENTRY_GROUP;
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_DAP) {
		struct editorDrawerDapLookup lookup;
		if (!editorDrawerDapLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
			return 0;
		}
		return lookup.kind == EDITOR_DRAWER_DAP_ENTRY_ROOT ||
				lookup.kind == EDITOR_DRAWER_DAP_ENTRY_GROUP;
	}
	struct editorDrawerLookup lookup;
	if (!editorDrawerLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	return lookup.node->is_dir;
}

int editorDrawerSelectedGitEntry(int *entry_idx_out) {
	if (entry_idx_out == NULL || E.drawer_mode != EDITOR_DRAWER_MODE_GIT) {
		return 0;
	}
	struct editorDrawerGitLookup lookup;
	if (!editorDrawerGitLookupByVisibleIndex(E.drawer_selected_index, &lookup) ||
			lookup.kind != EDITOR_DRAWER_GIT_ENTRY_FILE) {
		return 0;
	}
	*entry_idx_out = lookup.entry_idx;
	return 1;
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

int editorDrawerSelectedDapLaunch(int *launch_idx_out) {
	if (launch_idx_out == NULL || E.drawer_mode != EDITOR_DRAWER_MODE_DAP) {
		return 0;
	}
	struct editorDrawerDapLookup lookup;
	if (!editorDrawerDapLookupByVisibleIndex(E.drawer_selected_index, &lookup) ||
			lookup.kind != EDITOR_DRAWER_DAP_ENTRY_LAUNCH) {
		return 0;
	}
	*launch_idx_out = lookup.item_idx;
	return 1;
}

int editorDrawerSelectedDapDefault(int *default_idx_out) {
	if (default_idx_out == NULL || E.drawer_mode != EDITOR_DRAWER_MODE_DAP) {
		return 0;
	}
	struct editorDrawerDapLookup lookup;
	if (!editorDrawerDapLookupByVisibleIndex(E.drawer_selected_index, &lookup) ||
			lookup.kind != EDITOR_DRAWER_DAP_ENTRY_DEFAULT) {
		return 0;
	}
	*default_idx_out = lookup.item_idx;
	return 1;
}

int editorDrawerSelectedDapLocation(const char **path_out, int *line_out, int *character_out) {
	if (path_out == NULL || line_out == NULL || character_out == NULL ||
			E.drawer_mode != EDITOR_DRAWER_MODE_DAP) {
		return 0;
	}
	struct editorDrawerDapLookup lookup;
	if (!editorDrawerDapLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	if (lookup.kind == EDITOR_DRAWER_DAP_ENTRY_BREAKPOINT) {
		const struct editorDapBreakpoint *bp = &E.dap_breakpoints[lookup.item_idx];
		*path_out = bp->path;
		*line_out = bp->line;
		*character_out = 0;
		return 1;
	}
	if (lookup.kind == EDITOR_DRAWER_DAP_ENTRY_STACK_FRAME) {
		const struct editorDapStackFrame *frame = &E.dap_stack_frames[lookup.item_idx];
		if (frame->path[0] == '\0') {
			return 0;
		}
		*path_out = frame->path;
		*line_out = frame->line > 0 ? frame->line - 1 : 0;
		*character_out = frame->column > 0 ? frame->column - 1 : 0;
		return 1;
	}
	return 0;
}

int editorDrawerSelectedMenuAction(enum editorAction *action_out) {
	if (action_out == NULL || E.drawer_mode != EDITOR_DRAWER_MODE_MAIN_MENU) {
		return 0;
	}
	struct editorDrawerMenuLookup lookup;
	if (!editorDrawerMenuLookupByVisibleIndex(E.drawer_selected_index, &lookup) ||
			lookup.kind != EDITOR_DRAWER_MENU_ENTRY_ITEM) {
		return 0;
	}
	*action_out = editor_drawer_menu_groups[lookup.group_idx].items[lookup.item_idx].action;
	return 1;
}
