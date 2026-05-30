#ifndef ROTIDE_INPUT_MOUSE_H
#define ROTIDE_INPUT_MOUSE_H

#include "rotide.h"

typedef int (*editorProcessMappedActionFn)(enum editorAction action, int *effects_out);
typedef int (*editorMouseJumpToPathFn)(const char *path, int line, int character, int preview,
                                       int center);
typedef void (*editorMouseActionFn)(void);

enum editorMouseDispatchEffect {
	EDITOR_MOUSE_DISPATCH_EFFECT_NONE = 0,
	EDITOR_MOUSE_DISPATCH_EFFECT_VIEWPORT_SCROLL = 1 << 0,
	EDITOR_MOUSE_DISPATCH_EFFECT_CURSOR_OR_EDIT = 1 << 1
};

void editorResetDrawerClickTracking(void);
void editorResetTextClickTracking(void);
void editorResetTabClickTracking(void);

int editorMouseIsOverDrawer(const struct editorMouseEvent *event);
int editorClearHoverLinkState(void);
int editorHandleMouseWheel(const struct editorMouseEvent *event);
int editorHandleMouseEventInTerminalPane(const struct editorMouseEvent *event);
int editorResolveMouseToBufferOffset(const struct editorMouseEvent *event, int clamp_to_viewport,
                                     size_t *offset_out);
int editorMoveCursorToMouse(const struct editorMouseEvent *event, int clamp_to_viewport);
int editorHandleMouseMotion(const struct editorMouseEvent *event);
int editorHandleMouseLeftDrag(const struct editorMouseEvent *event);
int editorHandleMouseLeftRelease(const struct editorMouseEvent *event);
int editorOpenEditorContextMenuAt(int screen_row, int screen_col, int has_context_offset,
                                  size_t context_offset);
int editorEditorContextMenuActivate(editorProcessMappedActionFn process_mapped_action,
                                    int *effects_out);
int editorHandleMouseDrawerLeftPress(const struct editorMouseEvent *event, long long now_ms,
                                     int double_click_threshold_ms,
                                     editorProcessMappedActionFn process_mapped_action,
                                     editorMouseJumpToPathFn jump_to_path, int *effects_out);
int editorHandleMouseTextLeftPress(const struct editorMouseEvent *event, long long now_ms,
                                   int multi_click_threshold_ms,
                                   editorMouseActionFn goto_definition, int *effects_out);
int editorHandleMousePaneTabStripClick(const struct editorMouseEvent *event, long long now_ms);
int editorDrawerHeaderModeForColumn(int mouse_col, int drawer_cols,
                                    enum editorDrawerMode *mode_out);
int editorHandleMouseEventDispatch(int drawer_double_click_threshold_ms,
                                   int text_multi_click_threshold_ms,
                                   editorProcessMappedActionFn process_mapped_action,
                                   editorMouseJumpToPathFn jump_to_path,
                                   editorMouseActionFn goto_definition, int *effects_out);

#endif
