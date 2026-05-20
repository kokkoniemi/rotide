#include "workspace/drawer.h"
#include "workspace/drawer_internal.h"
#include "workspace/file_search.h"
#include "workspace/project_search.h"

#include <string.h>

struct drawerModeMenuItem {
	const char *name;
	enum editorAction action;
};

struct drawerModeMenuGroup {
	const char *name;
	const struct drawerModeMenuItem *items;
	int item_count;
};

enum drawerModeMenuEntryKind {
	EDITOR_DRAWER_MENU_ENTRY_ROOT = 0,
	EDITOR_DRAWER_MENU_ENTRY_GROUP,
	EDITOR_DRAWER_MENU_ENTRY_ITEM
};

struct drawerModeMenuLookup {
	enum drawerModeMenuEntryKind kind;
	int group_idx;
	int item_idx;
	int visible_idx;
	int parent_visible_idx;
	int group_visible_idx;
};

static const struct drawerModeMenuItem g_drawer_mode_menu_search_items[] = {
        {"Find File", EDITOR_ACTION_FIND_FILE},
        {"Find in Buffer", EDITOR_ACTION_FIND},
        {"Search Project Text", EDITOR_ACTION_PROJECT_SEARCH},
        {"Find & replace", EDITOR_ACTION_FIND_REPLACE},
        {"Go to Line", EDITOR_ACTION_GOTO_LINE},
        {"Go to Matching Bracket", EDITOR_ACTION_GOTO_MATCHING_BRACKET},
        {"Go to Definition", EDITOR_ACTION_GOTO_DEFINITION},
};

static const struct drawerModeMenuItem g_drawer_mode_menu_file_items[] = {
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

static const struct drawerModeMenuItem g_drawer_mode_menu_tabs_items[] = {
        {"Next Tab", EDITOR_ACTION_NEXT_TAB},
        {"Previous Tab", EDITOR_ACTION_PREV_TAB},
};

static const struct drawerModeMenuItem g_drawer_mode_menu_edit_items[] = {
        {"Undo", EDITOR_ACTION_UNDO},
        {"Redo", EDITOR_ACTION_REDO},
        {"Toggle Selection", EDITOR_ACTION_TOGGLE_SELECTION},
        {"Copy Selection", EDITOR_ACTION_COPY_SELECTION},
        {"Cut Selection", EDITOR_ACTION_CUT_SELECTION},
        {"Paste", EDITOR_ACTION_PASTE},
        {"Delete Selection", EDITOR_ACTION_DELETE_SELECTION},
        {"Toggle Comment", EDITOR_ACTION_TOGGLE_COMMENT},
};

static const struct drawerModeMenuItem g_drawer_mode_menu_view_items[] = {
        {"Project Files", EDITOR_ACTION_MAIN_MENU},
        {"Git Changes", EDITOR_ACTION_GIT_DRAWER},
        {"LSP", EDITOR_ACTION_LSP_DRAWER},
        {"DAP", EDITOR_ACTION_DAP_DRAWER},
        {"Collapse Drawer", EDITOR_ACTION_TOGGLE_DRAWER},
        {"Toggle Line Wrap", EDITOR_ACTION_TOGGLE_LINE_WRAP},
        {"Toggle Line Numbers", EDITOR_ACTION_TOGGLE_LINE_NUMBERS},
        {"Toggle Current Line", EDITOR_ACTION_TOGGLE_CURRENT_LINE_HIGHLIGHT},
};

static const struct drawerModeMenuGroup g_drawer_mode_menu_groups[] = {
        {"Find", g_drawer_mode_menu_search_items,
         (int)(sizeof(g_drawer_mode_menu_search_items) /
               sizeof(g_drawer_mode_menu_search_items[0]))},
        {"File", g_drawer_mode_menu_file_items,
         (int)(sizeof(g_drawer_mode_menu_file_items) / sizeof(g_drawer_mode_menu_file_items[0]))},
        {"Tabs", g_drawer_mode_menu_tabs_items,
         (int)(sizeof(g_drawer_mode_menu_tabs_items) / sizeof(g_drawer_mode_menu_tabs_items[0]))},
        {"Edit", g_drawer_mode_menu_edit_items,
         (int)(sizeof(g_drawer_mode_menu_edit_items) / sizeof(g_drawer_mode_menu_edit_items[0]))},
        {"View", g_drawer_mode_menu_view_items,
         (int)(sizeof(g_drawer_mode_menu_view_items) / sizeof(g_drawer_mode_menu_view_items[0]))},
};

static const int g_drawer_mode_menu_group_count =
        (int)(sizeof(g_drawer_mode_menu_groups) / sizeof(g_drawer_mode_menu_groups[0]));

static unsigned int drawerModeMenuAllGroupsMask(void) {
	unsigned int mask = 0;
	for (int i = 0; i < g_drawer_mode_menu_group_count; i++) {
		mask |= 1u << (unsigned int)i;
	}
	return mask;
}

static int drawerModeMenuGroupExpanded(int group_idx) {
	if (group_idx < 0 || group_idx >= g_drawer_mode_menu_group_count) {
		return 0;
	}
	return (E.drawer_menu_expanded & (1u << (unsigned int)group_idx)) != 0;
}

static void drawerModeMenuEnsureDefaultExpanded(void) {
	E.drawer_menu_expanded = drawerModeMenuAllGroupsMask();
}

int editorDrawerMenuVisibleCount(void) {
	int count = 1;
	for (int group_idx = 0; group_idx < g_drawer_mode_menu_group_count; group_idx++) {
		count++;
		if (drawerModeMenuGroupExpanded(group_idx)) {
			count += g_drawer_mode_menu_groups[group_idx].item_count;
		}
	}
	return count;
}

static int drawerModeMenuLookupByVisibleIndex(int visible_idx,
                                              struct drawerModeMenuLookup *lookup_out) {
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
	for (int group_idx = 0; group_idx < g_drawer_mode_menu_group_count; group_idx++) {
		int group_visible_idx = cursor;
		if (visible_idx == group_visible_idx) {
			lookup_out->kind = EDITOR_DRAWER_MENU_ENTRY_GROUP;
			lookup_out->group_idx = group_idx;
			lookup_out->parent_visible_idx = 0;
			lookup_out->group_visible_idx = group_visible_idx;
			return 1;
		}
		cursor++;

		if (!drawerModeMenuGroupExpanded(group_idx)) {
			continue;
		}

		const struct drawerModeMenuGroup *group = &g_drawer_mode_menu_groups[group_idx];
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

int editorDrawerMainMenuToggle(void) {
	if (E.drawer_mode == EDITOR_DRAWER_MODE_MAIN_MENU) {
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
	drawerModeMenuEnsureDefaultExpanded();
	E.drawer_mode = EDITOR_DRAWER_MODE_MAIN_MENU;
	E.drawer_selected_index = -1;
	E.drawer_rowoff = 0;
	E.drawer_resize_active = 0;
	(void)editorDrawerSetCollapsed(0);
	E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
	return 1;
}

int editorDrawerMenuGetVisibleEntry(int visible_idx, struct editorDrawerEntryView *view_out) {
	if (view_out == NULL) {
		return 0;
	}
	struct drawerModeMenuLookup lookup;
	if (!drawerModeMenuLookupByVisibleIndex(visible_idx, &lookup)) {
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
			view_out->name = g_drawer_mode_menu_groups[lookup.group_idx].name;
			view_out->depth = 1;
			view_out->is_dir = 1;
			view_out->is_expanded = drawerModeMenuGroupExpanded(lookup.group_idx);
			view_out->is_last_sibling =
			        lookup.group_idx == g_drawer_mode_menu_group_count - 1;
			return 1;
		case EDITOR_DRAWER_MENU_ENTRY_ITEM:
			view_out->name = g_drawer_mode_menu_groups[lookup.group_idx]
			                         .items[lookup.item_idx]
			                         .name;
			view_out->depth = 2;
			view_out->is_last_sibling =
			        lookup.item_idx ==
			        g_drawer_mode_menu_groups[lookup.group_idx].item_count - 1;
			return 1;
		default:
			return 0;
	}
}

int editorDrawerMenuExpandSelection(int viewport_rows) {
	struct drawerModeMenuLookup lookup;
	if (!drawerModeMenuLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	if (lookup.kind == EDITOR_DRAWER_MENU_ENTRY_ROOT) {
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 0;
	}
	if (lookup.kind != EDITOR_DRAWER_MENU_ENTRY_GROUP ||
	    drawerModeMenuGroupExpanded(lookup.group_idx)) {
		return 0;
	}
	E.drawer_menu_expanded |= 1u << (unsigned int)lookup.group_idx;
	editorDrawerClampSelectionAndScroll(viewport_rows);
	return 1;
}

int editorDrawerMenuCollapseSelection(int viewport_rows) {
	struct drawerModeMenuLookup lookup;
	if (!drawerModeMenuLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	if (lookup.kind == EDITOR_DRAWER_MENU_ENTRY_ROOT) {
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 0;
	}
	if (lookup.kind == EDITOR_DRAWER_MENU_ENTRY_GROUP) {
		if (!drawerModeMenuGroupExpanded(lookup.group_idx)) {
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

int editorDrawerMenuToggleSelectionExpanded(int viewport_rows) {
	struct drawerModeMenuLookup lookup;
	if (!drawerModeMenuLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	if (lookup.kind == EDITOR_DRAWER_MENU_ENTRY_ROOT ||
	    lookup.kind != EDITOR_DRAWER_MENU_ENTRY_GROUP) {
		return 0;
	}
	E.drawer_menu_expanded ^= 1u << (unsigned int)lookup.group_idx;
	editorDrawerClampSelectionAndScroll(viewport_rows);
	return 1;
}

int editorDrawerMenuSelectedIsDirectory(void) {
	struct drawerModeMenuLookup lookup;
	if (!drawerModeMenuLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	return lookup.kind == EDITOR_DRAWER_MENU_ENTRY_ROOT ||
	       lookup.kind == EDITOR_DRAWER_MENU_ENTRY_GROUP;
}

int editorDrawerSelectedMenuAction(enum editorAction *action_out) {
	if (action_out == NULL || E.drawer_mode != EDITOR_DRAWER_MODE_MAIN_MENU) {
		return 0;
	}
	struct drawerModeMenuLookup lookup;
	if (!drawerModeMenuLookupByVisibleIndex(E.drawer_selected_index, &lookup) ||
	    lookup.kind != EDITOR_DRAWER_MENU_ENTRY_ITEM) {
		return 0;
	}
	*action_out = g_drawer_mode_menu_groups[lookup.group_idx].items[lookup.item_idx].action;
	return 1;
}
