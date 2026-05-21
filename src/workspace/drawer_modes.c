#include "support/file_io.h"
#include "workspace/drawer.h"
#include "workspace/drawer_internal.h"
#include "workspace/file_search.h"
#include "workspace/git.h"
#include "workspace/project_search.h"

#include <string.h>

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

int editorDrawerVisibleEntryView(int visible_idx, struct editorDrawerEntryView *view_out) {
	if (view_out == NULL) {
		return 0;
	}
	if (editorFileSearchIsActive()) {
		return editorFileSearchVisibleEntryView(visible_idx, view_out);
	}
	if (editorProjectSearchIsActive()) {
		return editorProjectSearchVisibleEntryView(visible_idx, view_out);
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_MAIN_MENU) {
		return editorDrawerMenuVisibleEntryView(visible_idx, view_out);
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_GIT) {
		return editorDrawerGitVisibleEntryView(visible_idx, view_out);
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_LSP) {
		return editorDrawerLspVisibleEntryView(visible_idx, view_out);
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_DAP) {
		return editorDrawerDapVisibleEntryView(visible_idx, view_out);
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
		return editorDrawerMenuExpandSelection(viewport_rows);
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_GIT) {
		return editorDrawerGitExpandSelection(viewport_rows);
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_LSP) {
		return editorDrawerLspExpandSelection(viewport_rows);
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_DAP) {
		return editorDrawerDapExpandSelection(viewport_rows);
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
		return editorDrawerMenuCollapseSelection(viewport_rows);
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_GIT) {
		return editorDrawerGitCollapseSelection(viewport_rows);
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_LSP) {
		return editorDrawerLspCollapseSelection(viewport_rows);
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_DAP) {
		return editorDrawerDapCollapseSelection(viewport_rows);
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
		return editorDrawerMenuToggleSelectionExpanded(viewport_rows);
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_GIT) {
		return editorDrawerGitToggleSelectionExpanded(viewport_rows);
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_LSP) {
		return editorDrawerLspToggleSelectionExpanded(viewport_rows);
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_DAP) {
		return editorDrawerDapToggleSelectionExpanded(viewport_rows);
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
		return editorDrawerMenuSelectedIsDirectory();
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_GIT) {
		return editorDrawerGitSelectedIsDirectory();
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_LSP) {
		return editorDrawerLspSelectedIsDirectory();
	}
	if (E.drawer_mode == EDITOR_DRAWER_MODE_DAP) {
		return editorDrawerDapSelectedIsDirectory();
	}
	struct editorDrawerLookup lookup;
	if (!editorDrawerLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	return lookup.node->is_dir;
}
