#include "config/dap_config.h"
#include "debug/dap.h"
#include "rotide.h"
#include "workspace/drawer.h"
#include "workspace/drawer_internal.h"
#include "workspace/file_search.h"
#include "workspace/project_search.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

enum drawerModeDapGroup {
	EDITOR_DRAWER_DAP_GROUP_CONFIGURATIONS = 0,
	EDITOR_DRAWER_DAP_GROUP_BREAKPOINTS,
	EDITOR_DRAWER_DAP_GROUP_THREADS,
	EDITOR_DRAWER_DAP_GROUP_STACK,
	EDITOR_DRAWER_DAP_GROUP_VARIABLES,
	EDITOR_DRAWER_DAP_GROUP_OUTPUT,
	EDITOR_DRAWER_DAP_GROUP_COUNT
};

enum drawerModeDapEntryKind {
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

struct drawerModeDapLookup {
	enum drawerModeDapEntryKind kind;
	int group_idx;
	int item_idx;
	int item_count;
	int visible_idx;
	int parent_visible_idx;
	int group_visible_idx;
	int group_display_idx;
};

static const char *g_drawer_mode_dap_group_names[EDITOR_DRAWER_DAP_GROUP_COUNT] = {
        "Configurations", "Breakpoints", "Threads", "Stack", "Variables", "Output"};

static const int g_drawer_mode_dap_display_groups[EDITOR_DRAWER_DAP_GROUP_COUNT] = {
        EDITOR_DRAWER_DAP_GROUP_CONFIGURATIONS, EDITOR_DRAWER_DAP_GROUP_BREAKPOINTS,
        EDITOR_DRAWER_DAP_GROUP_VARIABLES,      EDITOR_DRAWER_DAP_GROUP_STACK,
        EDITOR_DRAWER_DAP_GROUP_THREADS,        EDITOR_DRAWER_DAP_GROUP_OUTPUT};

static unsigned int drawerModeDapAllGroupsMask(void) {
	unsigned int mask = 0;
	for (int i = 0; i < EDITOR_DRAWER_DAP_GROUP_COUNT; i++) {
		mask |= 1u << (unsigned int)i;
	}
	return mask;
}

static int drawerModeDapGroupExpanded(int group_idx) {
	if (group_idx < 0 || group_idx >= EDITOR_DRAWER_DAP_GROUP_COUNT) {
		return 0;
	}
	return (E.drawer_dap_expanded & (1u << (unsigned int)group_idx)) != 0;
}

static void drawerModeDapEnsureDefaultExpanded(void) {
	E.drawer_dap_expanded = drawerModeDapAllGroupsMask();
}

static int drawerModeDapScopeExpanded(int scope_idx) {
	if (scope_idx < 0 || scope_idx >= 64) {
		return 1;
	}
	return (E.drawer_dap_scope_collapsed & (1ull << (unsigned int)scope_idx)) == 0;
}

/* Number of collected variables tagged as belonging to scope `scope_idx`. */
static int drawerModeDapScopeVarCount(int scope_idx) {
	int count = 0;
	for (int i = 0; i < E.dap_variable_count; i++) {
		if (E.dap_variables[i].scope_index == scope_idx) {
			count++;
		}
	}
	return count;
}

/* Flat E.dap_variables[] index of the `nth` (0-based) variable in scope
 * `scope_idx`, or -1 if there is none. */
static int drawerModeDapScopeVarIndex(int scope_idx, int nth) {
	int seen = 0;
	for (int i = 0; i < E.dap_variable_count; i++) {
		if (E.dap_variables[i].scope_index != scope_idx) {
			continue;
		}
		if (seen == nth) {
			return i;
		}
		seen++;
	}
	return -1;
}

/* Visible rows in the expanded Variables group body: one header per scope, plus
 * the variables of each expanded scope. Zero when there are no scopes (the
 * caller then renders a "(none)" placeholder). */
static int drawerModeDapVariablesRowCount(void) {
	int rows = 0;
	for (int s = 0; s < E.dap_scope_count; s++) {
		rows++;
		if (drawerModeDapScopeExpanded(s)) {
			rows += drawerModeDapScopeVarCount(s);
		}
	}
	return rows;
}

static int drawerModeDapGroupItemCount(int group_idx) {
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
			return drawerModeDapVariablesRowCount();
		case EDITOR_DRAWER_DAP_GROUP_OUTPUT:
			return E.dap_output_len > 0 ? 1 : 0;
		default:
			return 0;
	}
}

int editorDrawerDapVisibleCount(void) {
	int count = 1;
	for (int display_idx = 0; display_idx < EDITOR_DRAWER_DAP_GROUP_COUNT; display_idx++) {
		int group_idx = g_drawer_mode_dap_display_groups[display_idx];
		count++;
		if (!drawerModeDapGroupExpanded(group_idx)) {
			continue;
		}
		int item_count = drawerModeDapGroupItemCount(group_idx);
		count += item_count > 0 ? item_count : 1;
	}
	return count;
}

/* Decode a 0-based row offset within the expanded Variables group body into a
 * scope header or a variable, filling kind/item_idx/parent on `out`.
 * `group_visible_idx` is the visible index of the Variables group header.
 * For a scope, item_idx is the scope index; for a variable it is the flat
 * E.dap_variables[] index. Returns 1 on success. */
static int drawerModeDapDecodeVariableRow(int row_offset, int group_visible_idx,
                                          struct drawerModeDapLookup *out) {
	int consumed = 0;
	for (int s = 0; s < E.dap_scope_count; s++) {
		int header_body_offset = consumed;
		if (row_offset == header_body_offset) {
			out->kind = EDITOR_DRAWER_DAP_ENTRY_SCOPE;
			out->item_idx = s;
			out->parent_visible_idx = group_visible_idx;
			return 1;
		}
		consumed++; /* scope header */
		int var_count = drawerModeDapScopeExpanded(s) ? drawerModeDapScopeVarCount(s) : 0;
		if (row_offset < consumed + var_count) {
			out->kind = EDITOR_DRAWER_DAP_ENTRY_VARIABLE;
			out->item_idx = drawerModeDapScopeVarIndex(s, row_offset - consumed);
			out->parent_visible_idx = group_visible_idx + 1 + header_body_offset;
			return 1;
		}
		consumed += var_count;
	}
	return 0;
}

static int drawerModeDapLookupByVisibleIndex(int visible_idx,
                                             struct drawerModeDapLookup *lookup_out) {
	if (lookup_out == NULL || visible_idx < 0) {
		return 0;
	}
	memset(lookup_out, 0, sizeof(*lookup_out));
	lookup_out->visible_idx = visible_idx;
	lookup_out->group_idx = -1;
	lookup_out->item_idx = -1;
	lookup_out->parent_visible_idx = -1;
	lookup_out->group_visible_idx = -1;
	lookup_out->group_display_idx = -1;

	if (visible_idx == 0) {
		lookup_out->kind = EDITOR_DRAWER_DAP_ENTRY_ROOT;
		return 1;
	}

	int cursor = 1;
	for (int display_idx = 0; display_idx < EDITOR_DRAWER_DAP_GROUP_COUNT; display_idx++) {
		int group_idx = g_drawer_mode_dap_display_groups[display_idx];
		int group_visible_idx = cursor;
		int item_count = drawerModeDapGroupItemCount(group_idx);
		if (visible_idx == group_visible_idx) {
			lookup_out->kind = EDITOR_DRAWER_DAP_ENTRY_GROUP;
			lookup_out->group_idx = group_idx;
			lookup_out->group_display_idx = display_idx;
			lookup_out->item_count = item_count;
			lookup_out->parent_visible_idx = 0;
			lookup_out->group_visible_idx = group_visible_idx;
			return 1;
		}
		cursor++;
		if (!drawerModeDapGroupExpanded(group_idx)) {
			continue;
		}
		if (item_count == 0) {
			if (visible_idx == cursor) {
				lookup_out->kind = EDITOR_DRAWER_DAP_ENTRY_PLACEHOLDER;
				lookup_out->group_idx = group_idx;
				lookup_out->group_display_idx = display_idx;
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
			lookup_out->group_display_idx = display_idx;
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
				(void)drawerModeDapDecodeVariableRow(item_idx, group_visible_idx,
				                                     lookup_out);
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

int editorDrawerDapToggle(void) {
	if (E.drawer_mode == EDITOR_DRAWER_MODE_DAP) {
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
	(void)editorDapConfigReloadProject(E.drawer_root_path);
	drawerModeDapEnsureDefaultExpanded();
	E.drawer_mode = EDITOR_DRAWER_MODE_DAP;
	E.drawer_selected_index = -1;
	E.drawer_rowoff = 0;
	E.drawer_resize_active = 0;
	(void)editorDrawerSetCollapsed(0);
	E.primary_focus = EDITOR_PRIMARY_FOCUS_DRAWER;
	return 1;
}

int editorDrawerDapVisibleEntryView(int visible_idx, struct editorDrawerEntryView *view_out) {
	if (view_out == NULL) {
		return 0;
	}

	struct drawerModeDapLookup lookup;
	if (!drawerModeDapLookupByVisibleIndex(visible_idx, &lookup)) {
		return 0;
	}

	memset(view_out, 0, sizeof(*view_out));
	view_out->is_selected = visible_idx == E.drawer_selected_index;
	view_out->parent_visible_idx = lookup.parent_visible_idx;
	switch (lookup.kind) {
		case EDITOR_DRAWER_DAP_ENTRY_ROOT:
			(void)snprintf(view_out->name_buf, sizeof(view_out->name_buf),
			               "Debugger%s%s", E.dap_running ? " - running" : "",
			               E.dap_stopped ? " (stopped)" : "");
			view_out->name = view_out->name_buf;
			view_out->depth = 0;
			view_out->is_dir = 1;
			view_out->is_expanded = 1;
			view_out->is_root = 1;
			view_out->is_last_sibling = 1;
			return 1;
		case EDITOR_DRAWER_DAP_ENTRY_GROUP:
			if (lookup.group_idx == EDITOR_DRAWER_DAP_GROUP_CONFIGURATIONS) {
				(void)snprintf(view_out->name_buf, sizeof(view_out->name_buf),
				               "Configurations (%d)", E.dap_launch_count);
				view_out->name = view_out->name_buf;
			} else if (lookup.group_idx == EDITOR_DRAWER_DAP_GROUP_BREAKPOINTS) {
				(void)snprintf(view_out->name_buf, sizeof(view_out->name_buf),
				               "Breakpoints (%d)", E.dap_breakpoint_count);
				view_out->name = view_out->name_buf;
			} else {
				view_out->name = g_drawer_mode_dap_group_names[lookup.group_idx];
			}
			view_out->depth = 1;
			view_out->is_dir = 1;
			view_out->is_expanded = drawerModeDapGroupExpanded(lookup.group_idx);
			view_out->is_last_sibling =
			        lookup.group_display_idx == EDITOR_DRAWER_DAP_GROUP_COUNT - 1;
			return 1;
		case EDITOR_DRAWER_DAP_ENTRY_LAUNCH: {
			const struct editorDapLaunchConfig *config =
			        &E.dap_launches[lookup.item_idx];
			(void)snprintf(view_out->name_buf, sizeof(view_out->name_buf), "%s%s%s",
			               lookup.item_idx == E.dap_selected_launch ? "* " : "",
			               config->name[0] != '\0' ? config->name : config->id,
			               editorDapAdapterById(config->adapter) != NULL
			                       ? ""
			                       : " (missing adapter)");
			view_out->name = view_out->name_buf;
			view_out->depth = 2;
			view_out->is_last_sibling = lookup.item_idx == lookup.item_count - 1;
			view_out->icon_kind = EDITOR_DRAWER_ENTRY_ICON_DAP_START;
			view_out->icon_color = EDITOR_DRAWER_ENTRY_ICON_COLOR_DAP_START;
			return 1;
		}
		case EDITOR_DRAWER_DAP_ENTRY_CREATE_PROMPT:
			view_out->name = "Create debug config from default";
			view_out->depth = 2;
			view_out->icon_kind = EDITOR_DRAWER_ENTRY_ICON_NONE;
			return 1;
		case EDITOR_DRAWER_DAP_ENTRY_DEFAULT: {
			const struct editorDapLaunchConfig *config =
			        &E.dap_defaults[lookup.item_idx];
			(void)snprintf(view_out->name_buf, sizeof(view_out->name_buf), "+ %s",
			               config->name[0] != '\0' ? config->name : config->id);
			view_out->name = view_out->name_buf;
			view_out->depth = 3;
			view_out->is_last_sibling = lookup.item_idx == E.dap_default_count - 1;
			view_out->icon_kind = EDITOR_DRAWER_ENTRY_ICON_DAP_START;
			view_out->icon_color = EDITOR_DRAWER_ENTRY_ICON_COLOR_DAP_START;
			return 1;
		}
		case EDITOR_DRAWER_DAP_ENTRY_BREAKPOINT: {
			const struct editorDapBreakpoint *bp = &E.dap_breakpoints[lookup.item_idx];
			const char *slash = strrchr(bp->path, '/');
			const char *base = slash != NULL ? slash + 1 : bp->path;
			(void)snprintf(view_out->name_buf, sizeof(view_out->name_buf), "%s:%d",
			               base, bp->line + 1);
			view_out->name = view_out->name_buf;
			view_out->path = bp->path;
			view_out->line = bp->line;
			view_out->depth = 2;
			view_out->is_last_sibling = lookup.item_idx == lookup.item_count - 1;
			view_out->dap_breakpoint_kind = bp->kind;
			view_out->icon_kind = EDITOR_DRAWER_ENTRY_ICON_DAP_BREAKPOINT;
			view_out->icon_color = EDITOR_DRAWER_ENTRY_ICON_COLOR_DAP_BREAKPOINT;
			return 1;
		}
		case EDITOR_DRAWER_DAP_ENTRY_THREAD:
			(void)snprintf(view_out->prefix_buf, sizeof(view_out->prefix_buf), "#%d",
			               E.dap_threads[lookup.item_idx].id);
			view_out->prefix = view_out->prefix_buf;
			view_out->prefix_muted = 1;
			view_out->name = E.dap_threads[lookup.item_idx].name[0] != '\0'
			                         ? E.dap_threads[lookup.item_idx].name
			                         : "(thread)";
			view_out->depth = 2;
			view_out->is_last_sibling = lookup.item_idx == lookup.item_count - 1;
			view_out->icon_kind = EDITOR_DRAWER_ENTRY_ICON_NONE;
			return 1;
		case EDITOR_DRAWER_DAP_ENTRY_STACK_FRAME: {
			const struct editorDapStackFrame *frame =
			        &E.dap_stack_frames[lookup.item_idx];
			(void)snprintf(view_out->name_buf, sizeof(view_out->name_buf), "%s:%d",
			               frame->name[0] != '\0' ? frame->name : "(frame)",
			               frame->line);
			view_out->name = view_out->name_buf;
			view_out->path = frame->path;
			view_out->line = frame->line > 0 ? frame->line - 1 : 0;
			view_out->character = frame->column > 0 ? frame->column - 1 : 0;
			view_out->depth = 2;
			view_out->is_last_sibling = lookup.item_idx == lookup.item_count - 1;
			view_out->icon_kind = EDITOR_DRAWER_ENTRY_ICON_NONE;
			return 1;
		}
		case EDITOR_DRAWER_DAP_ENTRY_SCOPE:
			(void)snprintf(view_out->name_buf, sizeof(view_out->name_buf), "%s (%d)",
			               E.dap_scopes[lookup.item_idx].name,
			               drawerModeDapScopeVarCount(lookup.item_idx));
			view_out->name = view_out->name_buf;
			view_out->depth = 2;
			view_out->is_dir = 1;
			view_out->is_expanded = drawerModeDapScopeExpanded(lookup.item_idx);
			view_out->is_last_sibling = lookup.item_idx == E.dap_scope_count - 1;
			return 1;
		case EDITOR_DRAWER_DAP_ENTRY_VARIABLE: {
			if (lookup.item_idx < 0 || lookup.item_idx >= E.dap_variable_count) {
				view_out->name = "(none)";
				view_out->depth = 3;
				view_out->is_placeholder = 1;
				return 1;
			}
			const struct editorDapVariable *var = &E.dap_variables[lookup.item_idx];
			view_out->name = var->name;
			view_out->detail_type = var->type;
			view_out->detail_value = var->value;
			view_out->detail_preview = var->preview;
			view_out->detail_address = var->memory_reference;
			view_out->variable_reference = var->variables_reference;
			view_out->depth = 3;
			view_out->icon_kind = EDITOR_DRAWER_ENTRY_ICON_NONE;
			int is_last = 1;
			for (int i = lookup.item_idx + 1; i < E.dap_variable_count; i++) {
				if (E.dap_variables[i].scope_index == var->scope_index) {
					is_last = 0;
					break;
				}
			}
			view_out->is_last_sibling = is_last;
			return 1;
		}
		case EDITOR_DRAWER_DAP_ENTRY_OUTPUT:
			(void)snprintf(view_out->name_buf, sizeof(view_out->name_buf), "%.120s",
			               E.dap_output);
			view_out->name = view_out->name_buf;
			view_out->depth = 2;
			return 1;
		case EDITOR_DRAWER_DAP_ENTRY_PLACEHOLDER:
			if (lookup.group_idx == EDITOR_DRAWER_DAP_GROUP_CONFIGURATIONS) {
				view_out->name =
				        E.dap_project_config_invalid
				                ? "Invalid .rotide.toml DAP config"
				                : "No DAP defaults in ~/.rotide/config.toml";
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

int editorDrawerDapExpandSelection(int viewport_rows) {
	struct drawerModeDapLookup lookup;
	if (!drawerModeDapLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	if (lookup.kind == EDITOR_DRAWER_DAP_ENTRY_ROOT) {
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 0;
	}
	if (lookup.kind == EDITOR_DRAWER_DAP_ENTRY_SCOPE) {
		if (drawerModeDapScopeExpanded(lookup.item_idx) || lookup.item_idx >= 64) {
			return 0;
		}
		E.drawer_dap_scope_collapsed &= ~(1ull << (unsigned int)lookup.item_idx);
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 1;
	}
	if (lookup.kind != EDITOR_DRAWER_DAP_ENTRY_GROUP ||
	    drawerModeDapGroupExpanded(lookup.group_idx)) {
		return 0;
	}
	E.drawer_dap_expanded |= 1u << (unsigned int)lookup.group_idx;
	editorDrawerClampSelectionAndScroll(viewport_rows);
	return 1;
}

int editorDrawerDapCollapseSelection(int viewport_rows) {
	struct drawerModeDapLookup lookup;
	if (!drawerModeDapLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	if (lookup.kind == EDITOR_DRAWER_DAP_ENTRY_ROOT) {
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 0;
	}
	if (lookup.kind == EDITOR_DRAWER_DAP_ENTRY_GROUP) {
		if (!drawerModeDapGroupExpanded(lookup.group_idx)) {
			return 0;
		}
		E.drawer_dap_expanded &= ~(1u << (unsigned int)lookup.group_idx);
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 1;
	}
	if (lookup.kind == EDITOR_DRAWER_DAP_ENTRY_SCOPE &&
	    drawerModeDapScopeExpanded(lookup.item_idx) && lookup.item_idx < 64) {
		E.drawer_dap_scope_collapsed |= 1ull << (unsigned int)lookup.item_idx;
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 1;
	}
	E.drawer_selected_index = lookup.parent_visible_idx >= 0 ? lookup.parent_visible_idx
	                                                         : lookup.group_visible_idx;
	editorDrawerClampSelectionAndScroll(viewport_rows);
	return 1;
}

int editorDrawerDapToggleSelectionExpanded(int viewport_rows) {
	struct drawerModeDapLookup lookup;
	if (!drawerModeDapLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	if (lookup.kind == EDITOR_DRAWER_DAP_ENTRY_SCOPE && lookup.item_idx < 64) {
		E.drawer_dap_scope_collapsed ^= 1ull << (unsigned int)lookup.item_idx;
		editorDrawerClampSelectionAndScroll(viewport_rows);
		return 1;
	}
	if (lookup.kind != EDITOR_DRAWER_DAP_ENTRY_GROUP) {
		return 0;
	}
	E.drawer_dap_expanded ^= 1u << (unsigned int)lookup.group_idx;
	editorDrawerClampSelectionAndScroll(viewport_rows);
	return 1;
}

int editorDrawerDapSelectedIsDirectory(void) {
	struct drawerModeDapLookup lookup;
	if (!drawerModeDapLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
		return 0;
	}
	return lookup.kind == EDITOR_DRAWER_DAP_ENTRY_ROOT ||
	       lookup.kind == EDITOR_DRAWER_DAP_ENTRY_GROUP ||
	       lookup.kind == EDITOR_DRAWER_DAP_ENTRY_SCOPE;
}

int editorDrawerSelectedDapLaunch(int *launch_idx_out) {
	if (launch_idx_out == NULL || E.drawer_mode != EDITOR_DRAWER_MODE_DAP) {
		return 0;
	}
	struct drawerModeDapLookup lookup;
	if (!drawerModeDapLookupByVisibleIndex(E.drawer_selected_index, &lookup) ||
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
	struct drawerModeDapLookup lookup;
	if (!drawerModeDapLookupByVisibleIndex(E.drawer_selected_index, &lookup) ||
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
	struct drawerModeDapLookup lookup;
	if (!drawerModeDapLookupByVisibleIndex(E.drawer_selected_index, &lookup)) {
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
