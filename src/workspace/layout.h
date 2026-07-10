#ifndef ROTIDE_WORKSPACE_LAYOUT_H
#define ROTIDE_WORKSPACE_LAYOUT_H

#include "rotide.h" /* enum editorPaneKind */

#include <stddef.h>

/*
 * Pane tree for the editor area.
 *
 * The tree subdivides the editor viewport (the rectangle left of the
 * drawer, between the tab bar and the status/message bars) into leaf
 * panes. Leaves carry editor or terminal content; interior nodes hold a
 * split orientation, a ratio, and two children.
 */

enum editorSplitOrientation { EDITOR_SPLIT_HORIZONTAL = 0, EDITOR_SPLIT_VERTICAL };

/*
 * Per-pane view state.
 *
 * Each pane tracks its own active tab independently of the global
 * E.active_tab. active_tab_idx = -1 means "uninitialized; don't load this
 * view." On focus change, editorLayoutSetFocusedLeaf saves the outgoing
 * pane's view, switches the global active tab to the incoming pane's
 * view, then applies the incoming pane's cursor/scroll.
 *
 * `pane_tabs` / `pane_tab_count` are the pane's membership list of global
 * tab indices: the tab bar filters by this list, Ctrl+Tab cycles within
 * it, and a new split inherits only the splitting pane's active tab.
 * `mru_tabs` / `mru_tab_count` record activation order for close-tab
 * fallback without changing the tab strip order. Tabs still live in the
 * shared E.tabs[] array; these lists are views into it.
 *
 * Invariant: every editor leaf has pane_tab_count >= 1 in settled state.
 * See editorTabsEnsurePaneOccupancy in workspace/tabs.h.
 */
#ifndef ROTIDE_PANE_MAX_TABS
#define ROTIDE_PANE_MAX_TABS 128
#endif

struct editorPaneView {
	int active_tab_idx;
	int tab_view_start;
	int cx;
	int cy;
	int rx;
	int rowoff;
	int coloff;
	int wrapoff;
	size_t cursor_offset;
	int viewport_mode;
	int selection_mode_active;
	size_t selection_anchor_offset;
	int column_select_active;
	int column_select_anchor_cy;
	int column_select_anchor_rx;
	int column_select_cursor_rx;
	int pane_tabs[ROTIDE_PANE_MAX_TABS];
	int pane_tab_count;
	int mru_tabs[ROTIDE_PANE_MAX_TABS];
	int mru_tab_count;
	/* Preview is per-pane: a tab shared across a split can be preview in one
	 * pane and pinned in another, so pinning affects only that pane. -1 = none. */
	int preview_tab_idx;
};

struct editorPane {
	/*
	 * Leaf kind. Live leaves are always EDITOR; terminals and the debug console
	 * are tab kinds, not leaf kinds. The one exception is a transient
	 * deserialized `term` placeholder (kind == TERMINAL) that
	 * editorTerminalPaneHydratePlaceholders converts back to an editor leaf
	 * hosting a TERMINAL tab during workspace restore.
	 */
	enum editorPaneKind kind;
	struct editorPaneView view;
};

struct editorPaneNode {
	int is_split;
	union {
		struct editorPane leaf;
		struct {
			enum editorSplitOrientation orientation;
			double ratio;
			struct editorPaneNode *first;
			struct editorPaneNode *second;
		} split;
	} as;
};

struct editorRect {
	int x;
	int y;
	int w;
	int h;
};

struct editorLeafRect {
	struct editorPaneNode *node;
	struct editorRect rect;
};

struct editorLeafLayout {
	struct editorLeafRect *rects;
	int count;
	int capacity;
};

struct editorPaneNode *editorPaneNodeNewLeaf(enum editorPaneKind kind);
void editorPaneNodeFree(struct editorPaneNode *node);

int editorPaneNodeIsLeaf(const struct editorPaneNode *node);

struct editorPaneNode *editorPaneNodeFirstLeaf(struct editorPaneNode *node);
/* Returns NULL if no leaf of `kind` exists in the tree. */
struct editorPaneNode *editorPaneNodeFirstLeafOfKind(struct editorPaneNode *node,
                                                     enum editorPaneKind kind);
int editorPaneNodeContainsLeaf(const struct editorPaneNode *node,
                               const struct editorPaneNode *leaf);

/*
 * Tree mutation primitives. Both SplitLeaf and CloseLeaf may rewrite
 * `*root_ptr` when the affected node was the root. CloseLeaf is a no-op
 * (returns NULL) on the last remaining leaf; otherwise it returns the
 * sibling that took the freed leaf's position, leaving the freed leaf and
 * its parent split node released.
 */
struct editorPaneNode *editorPaneTreeSplitLeaf(struct editorPaneNode **root_ptr,
                                               struct editorPaneNode *leaf,
                                               enum editorSplitOrientation orientation,
                                               double ratio);
struct editorPaneNode *editorPaneTreeCloseLeaf(struct editorPaneNode **root_ptr,
                                               struct editorPaneNode *leaf);
struct editorPaneNode *editorPaneTreeFindParent(struct editorPaneNode *root,
                                                const struct editorPaneNode *child);
int editorPaneTreeLeafCount(const struct editorPaneNode *root);

int editorLayoutComputeInto(const struct editorPaneNode *root, struct editorRect viewport,
                            struct editorLeafLayout *out);
void editorLeafLayoutFree(struct editorLeafLayout *out);

struct editorPaneNode *editorLayoutLeafAt(struct editorLeafLayout *layout, int x, int y);
int editorLayoutPaneTabStripAt(const struct editorLeafLayout *layout, int x, int y,
                               struct editorPaneNode **leaf_out, int *local_col_out,
                               int *strip_cols_out);

/* Non-allocating single-leaf variant of editorLayoutComputeInto. */
int editorLayoutLeafRect(const struct editorPaneNode *root, struct editorRect viewport,
                         const struct editorPaneNode *leaf, struct editorRect *out);

/*
 * Bordered variants reserve `border_size` cols/rows between siblings of a
 * split. The border space is not part of either child's rect; it lives in
 * the gap and is painted separately by the renderer. The non-bordered
 * variants are wrappers passing border_size=0.
 *
 * Pane render uses ROTIDE_PANE_BORDER_SIZE; tree-mutation and pure layout
 * tests use 0 so split children tile the viewport exactly.
 */
#define ROTIDE_PANE_BORDER_SIZE 1

int editorLayoutComputeBorderedInto(const struct editorPaneNode *root, struct editorRect viewport,
                                    int border_size, struct editorLeafLayout *out);
int editorLayoutLeafRectBordered(const struct editorPaneNode *root, struct editorRect viewport,
                                 int border_size, const struct editorPaneNode *leaf,
                                 struct editorRect *out);

/*
 * Border collection from the tree walk.
 *
 * Each interior split node contributes one border rect occupying the gap
 * between its children: orientation VERTICAL → border is `border_size`
 * columns wide spanning the parent's y range; HORIZONTAL → `border_size`
 * rows tall spanning the parent's x range. This is the correct way to
 * classify a gap cell as `─` vs `│` in a nested layout — inferring from
 * "do any leaves intersect this row/col" gives wrong answers when an
 * outer split's leaves span across an inner split's border.
 */
struct editorBorderRect {
	struct editorRect rect;
	enum editorSplitOrientation orientation;
};

struct editorBorderList {
	struct editorBorderRect *rects;
	int count;
	int capacity;
};

int editorLayoutCollectBorders(const struct editorPaneNode *root, struct editorRect viewport,
                               int border_size, struct editorBorderList *out);
void editorBorderListFree(struct editorBorderList *list);

/*
 * Innermost matching border wins, so a hit inside a nested split's gap
 * resolves to the nested node, not its ancestor.
 */
int editorLayoutBorderAt(const struct editorPaneNode *root, struct editorRect viewport,
                         int border_size, int x, int y, struct editorPaneNode **out_node,
                         enum editorSplitOrientation *out_orientation);

/* The returned rect spans both children of `node` and the gap between them. */
int editorLayoutSplitNodeRect(const struct editorPaneNode *root, struct editorRect viewport,
                              int border_size, const struct editorPaneNode *node,
                              struct editorRect *out);

/*
 * Glue helpers that read the current editor state (E) to derive the editor
 * viewport rect (the rectangle the layout tree subdivides) and the focused
 * leaf's rect within it. These are the integration points used by render and
 * input dispatch. Both return 0 if the precondition is missing (no layout
 * root, no focused leaf, zero-sized window). Outputs are 0-based screen
 * coordinates with y=0 at the tab bar row.
 */
int editorLayoutEditorViewport(struct editorRect *out);
int editorLayoutFocusedLeafRect(struct editorRect *out);

/* Init marks a view uninitialized (active_tab_idx = -1). */
void editorPaneViewInit(struct editorPaneView *view);
void editorPaneViewCaptureFromState(struct editorPaneView *view);
/*
 * LoadIntoState overwrites E's cursor/scroll/selection but does NOT switch
 * tabs; the caller must align E.active_tab with view->active_tab_idx first
 * if cross-tab semantics are desired. Returns 0 if uninitialized.
 */
int editorPaneViewLoadIntoState(const struct editorPaneView *view);
/*
 * Capture-from-E into the outgoing view, update E.focused_leaf, switch tabs
 * if the incoming view records a different active_tab_idx, then load the
 * incoming view over E. No-op if new_leaf is already focused or not in
 * E.layout_root.
 */
int editorLayoutSetFocusedLeaf(struct editorPaneNode *new_leaf);

/*
 * Per-pane tab membership helpers. AddTab is a no-op if the index is
 * already present; both AddTab and InsertTabAt return 0 if the list is
 * full. ClearTabs leaves cursor/scroll/selection untouched.
 *
 * ShiftTabIndicesAfterClose (view and tree variants) must run after a tab
 * is removed from the global E.tabs[] so membership indices stay aligned.
 * AnyPaneHasTab gates whether closing a tab in one pane should free the
 * global entry — only the last pane referencing it should.
 */
int editorPaneViewAddTab(struct editorPaneView *view, int tab_idx);
int editorPaneViewInsertTabAt(struct editorPaneView *view, int tab_idx, int slot);
int editorPaneViewActivateTab(struct editorPaneView *view, int tab_idx);
int editorPaneViewMostRecentTab(const struct editorPaneView *view);
void editorPaneViewRemoveTab(struct editorPaneView *view, int tab_idx);
void editorPaneViewClearTabs(struct editorPaneView *view);
int editorPaneViewHasTab(const struct editorPaneView *view, int tab_idx);
int editorPaneViewIndexOfTab(const struct editorPaneView *view, int tab_idx);
void editorPaneViewShiftTabIndicesAfterClose(struct editorPaneView *view, int removed_idx);
int editorPaneTreeAnyPaneHasTab(const struct editorPaneNode *root, int tab_idx);
int editorPaneTreeAnyOtherPaneHasTab(const struct editorPaneNode *root,
                                     const struct editorPaneNode *exclude, int tab_idx);
void editorPaneTreeShiftTabIndicesAfterClose(struct editorPaneNode *root, int removed_idx);
void editorPaneTreeClearPreviewTab(struct editorPaneNode *root, int tab_idx);
int editorPaneTreeAnyPanePreviewsTab(const struct editorPaneNode *root, int tab_idx);

/*
 * E-aware wrappers around the tree primitives: they perform the
 * capture/load dance and update E.focused_leaf. SplitFocused makes the new
 * sibling focused and inheriting the splitter's view. Both return NULL on
 * no-op (CloseFocused on the last leaf) or failure.
 */
struct editorPaneNode *editorLayoutSplitFocused(enum editorSplitOrientation orientation,
                                                double ratio);
struct editorPaneNode *editorLayoutCloseFocused(void);
int editorLayoutCloseOthers(void);

/*
 * "Nearest" in the requested direction means smallest major-axis gap among
 * candidates whose minor-axis range overlaps `from_leaf` — purely overlap-
 * based, not center-distance, so a tall pane next to two short panes picks
 * a unique winner.
 */
enum editorFocusDirection {
	EDITOR_FOCUS_LEFT = 0,
	EDITOR_FOCUS_RIGHT,
	EDITOR_FOCUS_UP,
	EDITOR_FOCUS_DOWN
};

struct editorPaneNode *editorLayoutFindNeighborLeaf(const struct editorLeafLayout *layout,
                                                    const struct editorPaneNode *from_leaf,
                                                    enum editorFocusDirection direction);

/*
 * Focus/resize actions return 1 on mutation, 0 on no-op (no neighbor, no
 * leaf at point, root focused). ResizeFocused nudges the parent split
 * ratio by RESIZE_STEP, clamped to MIN_RATIO; grow=1 enlarges the focused
 * pane. FocusLeafAt takes editor-viewport coordinates.
 */
#define ROTIDE_PANE_MIN_RATIO 0.10
#define ROTIDE_PANE_RESIZE_STEP 0.05

int editorLayoutFocusDirection(enum editorFocusDirection direction);
int editorLayoutFocusNext(int reverse);
int editorLayoutFocusLeafAt(int x, int y);
int editorLayoutResizeFocused(int grow);
int editorLayoutFocusedLeafIndex(int *out_index, int *out_count);

/*
 * Posts a "Pane X/N" status message after split/close/focus actions, but
 * stays silent in the single-leaf case to avoid spamming during normal
 * editing.
 */
void editorPaneAnnounceFocus(void);

/*
 * Serialize the pane tree to a compact s-expression form for persistence.
 * Format: `leaf` for an editor leaf, `term` for a pane whose active tab is a
 * terminal, `(v <ratio> <left> <right>)` for vertical splits, `(h <ratio> <top>
 * <bottom>)` for horizontal splits. Returns the number of bytes written to `out`
 * (excluding the null terminator), or 0 if the buffer is too small. A
 * deserialized `term` token comes back as a kind == TERMINAL placeholder leaf;
 * editorTerminalPaneHydratePlaceholders spawns the PTY and converts it to a tab.
 */
size_t editorLayoutSerialize(const struct editorPaneNode *root, char *out, size_t out_size);

/*
 * Parse a layout string produced by editorLayoutSerialize and return a
 * fresh tree. Caller takes ownership via editorPaneNodeFree. Returns
 * NULL on syntax error or allocation failure.
 */
struct editorPaneNode *editorLayoutDeserialize(const char *s);

#endif
