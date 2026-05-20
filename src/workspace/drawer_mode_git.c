#include "workspace/drawer.h"
#include "workspace/drawer_internal.h"
#include "workspace/file_search.h"
#include "workspace/git.h"
#include "workspace/project_search.h"

#include <stdio.h>
#include <string.h>

enum drawerModeGitGroup {
	EDITOR_DRAWER_GIT_GROUP_STAGED = 0,
	EDITOR_DRAWER_GIT_GROUP_CHANGES,
	EDITOR_DRAWER_GIT_GROUP_UNTRACKED,
	EDITOR_DRAWER_GIT_GROUP_CONFLICTS,
	EDITOR_DRAWER_GIT_GROUP_COUNT
};

enum drawerModeGitEntryKind {
	EDITOR_DRAWER_GIT_ENTRY_ROOT = 0,
	EDITOR_DRAWER_GIT_ENTRY_GROUP,
	EDITOR_DRAWER_GIT_ENTRY_FILE,
	EDITOR_DRAWER_GIT_ENTRY_PLACEHOLDER
};

struct drawerModeGitLookup {
	enum drawerModeGitEntryKind kind;
	int group_idx;
	int entry_idx;
	int item_idx;
	int item_count;
	int visible_idx;
	int parent_visible_idx;
	int group_visible_idx;
	char status_char;
};

static const char *g_drawer_mode_git_group_names[EDITOR_DRAWER_GIT_GROUP_COUNT] = {
        "Staged", "Changes", "Untracked", "Conflicts"};

static int drawerModeGitEntryIsConflict(const struct editorGitEntry *entry) {
	char x = entry->index_status;
	char y = entry->worktree_status;
	return x == 'U' || y == 'U' || (x == 'A' && y == 'A') || (x == 'D' && y == 'D');
}

static int drawerModeGitEntryIsUntracked(const struct editorGitEntry *entry) {
	return entry->index_status == '?' && entry->worktree_status == '?';
}

static int drawerModeGitEntryInGroup(const struct editorGitEntry *entry, int group_idx) {
	if (entry == NULL) {
		return 0;
	}
	int conflict = drawerModeGitEntryIsConflict(entry);
	int untracked = drawerModeGitEntryIsUntracked(entry);
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

static int drawerModeGitGroupItemCount(int group_idx) {
	int count = 0;
	for (int i = 0; i < E.git_entry_count; i++) {
		if (drawerModeGitEntryInGroup(&E.git_entries[i], group_idx)) {
			count++;
		}
	}
	return count;
}

static char drawerModeGitStatusCharForGroup(const struct editorGitEntry *entry, int group_idx) {
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

static int drawerModeGitGroupExpanded(int group_idx) {
	if (group_idx < 0 || group_idx >= EDITOR_DRAWER_GIT_GROUP_COUNT) {
		return 0;
	}
	return (E.drawer_git_expanded & (1u << (unsigned int)group_idx)) != 0;
}

static unsigned int drawerModeGitAllGroupsMask(void) {
	unsigned int mask = 0;
	for (int i = 0; i < EDITOR_DRAWER_GIT_GROUP_COUNT; i++) {
		mask |= 1u << (unsigned int)i;
	}
	return mask;
}

static void drawerModeGitEnsureDefaultExpanded(void) {
	E.drawer_git_expanded = drawerModeGitAllGroupsMask();
}

int editorDrawerGitVisibleCount(void) {
	int count = 1;
	for (int group_idx = 0; group_idx < EDITOR_DRAWER_GIT_GROUP_COUNT; group_idx++) {
		count++;
		if (!drawerModeGitGroupExpanded(group_idx)) {
			continue;
		}
		int item_count = drawerModeGitGroupItemCount(group_idx);
		if (item_count == 0) {
			count++;
		} else {
			count += item_count;
		}
	}
	return count;
}

static int drawerModeGitLookupByVisibleIndex(int visible_idx,
                                             struct drawerModeGitLookup *lookup_out) {
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
			lookup_out->item_count = drawerModeGitGroupItemCount(group_idx);
			return 1;
		}
		cursor++;

		if (!drawerModeGitGroupExpanded(group_idx)) {
			continue;
		}

		int item_count = drawerModeGitGroupItemCount(group_idx);
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
			if (!drawerModeGitEntryInGroup(&E.git_entries[i], group_idx)) {
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
				lookup_out->status_char = drawerModeGitStatusCharForGroup(
				        &E.git_entries[i], group_idx);
				return 1;
			}
			item_idx++;
			cursor++;
		}
	}
	return 0;
}

int editorDrawerGitToggle(void) {
	if (E.drawer_mode == EDITOR_DRAWER_MODE_GIT) {
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
	if (E.git_repo_root != NULL) {
		editorGitRefresh();
	}
	drawerModeGitEnsureDefaultExpanded();
	E.drawer_mode = EDITOR_DRAWER_MODE_GIT;
	E.drawer_selected_index = -1;
	E.drawer_rowoff = 0;
	E.drawer_resize_active = 0;
	(void)editorDrawerSetCollapsed(0);
	E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
	return 1;
}

int editorDrawerGitGetVisibleEntry(int visible_idx, struct editorDrawerEntryView *view_out) {
	if (view_out == NULL) {
		return 0;
	}

	static char git_name_buf[PATH_MAX + 8];
	struct drawerModeGitLookup lookup;
	if (!drawerModeGitLookupByVisibleIndex(visible_idx, &lookup)) {
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
			view_out->name = g_drawer_mode_git_group_names[lookup.group_idx];
			view_out->depth = 1;
			view_out->is_dir = 1;
			view_out->is_expanded = drawerModeGitGroupExpanded(lookup.group_idx);
			view_out->is_last_sibling =
			        lookup.group_idx == EDITOR_DRAWER_GIT_GROUP_COUNT - 1;
			return 1;
		case EDITOR_DRAWER_GIT_ENTRY_FILE: {
			const struct editorGitEntry *entry = &E.git_entries[lookup.entry_idx];
			char status = lookup.status_char;
			if (status == ' ' || status == '\0') {
				status = '?';
			}
			snprintf(git_name_buf, sizeof(git_name_buf), "%c %s", status,
			         entry->rel_path);
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

int editorDrawerGitExpandSelection(int viewport_rows) {
	struct drawerModeGitLookup lookup;
	if (!drawerModeGitLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	if (lookup.kind == EDITOR_DRAWER_GIT_ENTRY_ROOT) {
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 0;
	}
	if (lookup.kind != EDITOR_DRAWER_GIT_ENTRY_GROUP ||
	    drawerModeGitGroupExpanded(lookup.group_idx)) {
		return 0;
	}
	E.drawer_git_expanded |= 1u << (unsigned int)lookup.group_idx;
	editorDrawerClampSelectionAndScroll(viewport_rows);
	return 1;
}

int editorDrawerGitCollapseSelection(int viewport_rows) {
	struct drawerModeGitLookup lookup;
	if (!drawerModeGitLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	if (lookup.kind == EDITOR_DRAWER_GIT_ENTRY_ROOT) {
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 0;
	}
	if (lookup.kind == EDITOR_DRAWER_GIT_ENTRY_GROUP) {
		if (!drawerModeGitGroupExpanded(lookup.group_idx)) {
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

int editorDrawerGitToggleSelectionExpanded(int viewport_rows) {
	struct drawerModeGitLookup lookup;
	if (!drawerModeGitLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	if (lookup.kind != EDITOR_DRAWER_GIT_ENTRY_GROUP) {
		return 0;
	}
	E.drawer_git_expanded ^= 1u << (unsigned int)lookup.group_idx;
	editorDrawerClampSelectionAndScroll(viewport_rows);
	return 1;
}

int editorDrawerGitSelectedIsDirectory(void) {
	struct drawerModeGitLookup lookup;
	if (!drawerModeGitLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	return lookup.kind == EDITOR_DRAWER_GIT_ENTRY_ROOT ||
	       lookup.kind == EDITOR_DRAWER_GIT_ENTRY_GROUP;
}

int editorDrawerSelectedGitEntry(int *entry_idx_out) {
	if (entry_idx_out == NULL || E.drawer_mode != EDITOR_DRAWER_MODE_GIT) {
		return 0;
	}
	struct drawerModeGitLookup lookup;
	if (!drawerModeGitLookupByVisibleIndex(E.drawer_selected_index, &lookup) ||
	    lookup.kind != EDITOR_DRAWER_GIT_ENTRY_FILE) {
		return 0;
	}
	*entry_idx_out = lookup.entry_idx;
	return 1;
}
