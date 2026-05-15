#include "input/actions_workspace.h"

#include "editing/edit.h"
#include "workspace/drawer.h"

void editorSetDrawerCollapseStatus(int collapsed) {
	editorSetStatusMsg(collapsed ? "Drawer collapsed" : "Drawer expanded");
}

void editorExpandDrawerForFocus(void) {
	if (editorDrawerSetCollapsed(0)) {
		editorSetDrawerCollapseStatus(0);
	}
	E.pane_focus = EDITOR_PANE_DRAWER;
}

void editorToggleDrawerFocus(void) {
	if (E.pane_focus == EDITOR_PANE_DRAWER) {
		E.pane_focus = EDITOR_PANE_TEXT;
		return;
	}
	editorExpandDrawerForFocus();
}
