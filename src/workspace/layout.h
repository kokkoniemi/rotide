#ifndef ROTIDE_WORKSPACE_LAYOUT_H
#define ROTIDE_WORKSPACE_LAYOUT_H

#include <stddef.h>

/*
 * Pane tree for the editor area.
 *
 * The tree describes how the editor viewport (the rectangle left of the
 * drawer, between the tab bar and the status/message bars) is subdivided
 * into leaf panes. Phase 1 only supports a single leaf of kind EDITOR, so
 * the tree is always one node and every layout computation returns a
 * single rect equal to the viewport. The data model is in place for the
 * later phases that introduce splits and additional pane kinds.
 */

enum editorPaneKind { EDITOR_PANE_KIND_EDITOR = 0, EDITOR_PANE_KIND_TERMINAL };

enum editorSplitOrientation { EDITOR_SPLIT_HORIZONTAL = 0, EDITOR_SPLIT_VERTICAL };

/*
 * Per-pane view state.
 *
 * active_tab_idx records the tab the pane is currently viewing — Phase 5
 * lets each pane track its own active tab independently of the global
 * E.active_tab. -1 means "uninitialized; don't load this view." On focus
 * change, editorLayoutSetFocusedLeaf saves the outgoing pane's view,
 * switches the global active tab to the incoming pane's view, then
 * applies the incoming pane's cursor/scroll.
 *
 * Phase 6: each pane also owns a membership list of which global tab
 * indices live "inside" the pane (`pane_tabs` / `pane_tab_count`). The
 * tab bar filters by this list, Ctrl+Tab cycles within it, and a new
 * split inherits only the splitting pane's active tab. Tabs themselves
 * still live in the shared E.tabs[] array; the list is a view into it.
 */
#ifndef ROTIDE_PANE_MAX_TABS
#define ROTIDE_PANE_MAX_TABS 128
#endif

struct editorPaneView {
	int active_tab_idx;
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
};

struct editorPane {
	enum editorPaneKind kind;
	struct editorPaneView view;
	/*
	 * Kind-specific payload. Editor leaves leave this NULL; terminal
	 * leaves point at a heap-allocated `editorTerminalPane` (or whatever
	 * future kind owns). `kind_state_free` is invoked by
	 * `editorPaneNodeFree` so leaf-specific resources release when the
	 * pane is closed, without layout.c needing to know about every kind.
	 */
	void *kind_state;
	void (*kind_state_free)(void *state);
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
int editorPaneNodeContainsLeaf(const struct editorPaneNode *node,
                               const struct editorPaneNode *leaf);

/*
 * Tree mutation primitives.
 *
 * editorPaneTreeSplitLeaf wraps the focused leaf in a new split node, with
 * `leaf` placed first (left/top) and a freshly allocated sibling placed
 * second (right/bottom). `*root_ptr` is updated if the splitting leaf was
 * the root. Returns the new sibling leaf on success, NULL on allocation
 * failure or if `leaf` is not found under `*root_ptr`.
 *
 * editorPaneTreeCloseLeaf removes `leaf` from the tree, promoting its
 * sibling in place of the parent split node. `*root_ptr` is updated if the
 * parent was the root. Returns the sibling leaf that now occupies the
 * focused position (caller should set focus to it), or NULL if `leaf` is
 * the only leaf (close-last-leaf is a no-op), or NULL if `leaf` is not
 * found. The freed leaf and parent split node are released by this call.
 *
 * editorPaneTreeFindParent locates the split node whose first/second child
 * is `child`. Returns NULL if `child` is the root or is not found.
 *
 * editorPaneTreeLeafCount returns the number of leaves under `root`.
 *
 * editorPaneTreeFirstLeaf is a stable left-most-leaf finder for fallback
 * focus moves.
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

/*
 * Walks the tree and writes the rect of `leaf` into `*out`. Returns 1 if the
 * leaf is found under `root`, else 0. Does not allocate. Useful when the
 * caller only needs one leaf's rect and wants to avoid materializing the full
 * leaf-layout array.
 */
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
 * Glue helpers that read the current editor state (E) to derive the editor
 * viewport rect (the rectangle the layout tree subdivides) and the focused
 * leaf's rect within it. These are the integration points used by render and
 * input dispatch. Both return 0 if the precondition is missing (no layout
 * root, no focused leaf, zero-sized window). Outputs are 0-based screen
 * coordinates with y=0 at the tab bar row.
 */
int editorLayoutEditorViewport(struct editorRect *out);
int editorLayoutFocusedLeafRect(struct editorRect *out);

/*
 * View state capture/load for focus changes between panes.
 *
 * editorPaneViewInit resets a view to "uninitialized" (active_tab_idx=-1).
 *
 * editorPaneViewCaptureFromState snapshots E's cursor/scroll/selection
 * and the current active tab index into the view.
 *
 * editorPaneViewLoadIntoState overwrites E's cursor/scroll/selection from
 * the view. It does NOT switch tabs — the caller is responsible for ensuring
 * E.active_tab matches view->active_tab_idx if cross-tab semantics are
 * desired. Returns 0 if the view is uninitialized (active_tab_idx<0),
 * 1 otherwise.
 *
 * editorLayoutSetFocusedLeaf orchestrates a full focus change including
 * per-pane tab swapping: captures from E into the previously focused
 * leaf's view, updates E.focused_leaf, switches the active tab if the
 * incoming pane's view records a different tab, then loads the incoming
 * pane's cursor/selection over the result. A no-op when new_leaf is already
 * focused or is not a leaf in E.layout_root.
 */
void editorPaneViewInit(struct editorPaneView *view);
void editorPaneViewCaptureFromState(struct editorPaneView *view);
int editorPaneViewLoadIntoState(const struct editorPaneView *view);
int editorLayoutSetFocusedLeaf(struct editorPaneNode *new_leaf);

/*
 * Per-pane tab membership helpers.
 *
 * editorPaneViewAddTab inserts `tab_idx` into the view's tab list if it
 * isn't already there. Returns 1 on success, 0 if the list is full.
 *
 * editorPaneViewRemoveTab removes `tab_idx` from the list (no-op if
 * absent) and shifts subsequent entries down. Used by tab close.
 *
 * editorPaneViewHasTab returns 1 if the tab is in the view's list.
 *
 * editorPaneViewIndexOfTab returns the local position of `tab_idx` in
 * the list, or -1 if not present.
 *
 * editorPaneViewShiftTabIndicesAfterClose decrements every recorded
 * index that is > `removed_idx` so the membership list stays consistent
 * with the global E.tabs[] after a tab is removed from the global array.
 *
 * editorPaneTreeAnyPaneHasTab returns 1 if any leaf anywhere under
 * `root` lists `tab_idx`. Used to decide whether closing a tab in one
 * pane should free the global tab entry.
 */
int editorPaneViewAddTab(struct editorPaneView *view, int tab_idx);
void editorPaneViewRemoveTab(struct editorPaneView *view, int tab_idx);
int editorPaneViewHasTab(const struct editorPaneView *view, int tab_idx);
int editorPaneViewIndexOfTab(const struct editorPaneView *view, int tab_idx);
void editorPaneViewShiftTabIndicesAfterClose(struct editorPaneView *view, int removed_idx);
int editorPaneTreeAnyPaneHasTab(const struct editorPaneNode *root, int tab_idx);
void editorPaneTreeShiftTabIndicesAfterClose(struct editorPaneNode *root, int removed_idx);

/*
 * High-level actions for the focused pane. These wrap the tree mutation
 * primitives with the capture/load dance and update E.focused_leaf.
 *
 * editorLayoutSplitFocused splits the currently focused leaf with the given
 * orientation and ratio. The new sibling inherits the splitting leaf's view
 * and becomes focused. Returns the new sibling on success, NULL on failure.
 *
 * editorLayoutCloseFocused removes the currently focused leaf and shifts
 * focus to its sibling. Returns the newly focused leaf on success, NULL if
 * the focused leaf is the only leaf (no-op) or on failure.
 */
struct editorPaneNode *editorLayoutSplitFocused(enum editorSplitOrientation orientation,
                                                double ratio);
struct editorPaneNode *editorLayoutCloseFocused(void);

/*
 * Geometric neighbor lookup.
 *
 * editorLayoutFindNeighborLeaf scans `layout` for the nearest leaf in the
 * given direction relative to `from_leaf`. "Nearest" means smallest gap on
 * the major axis among candidates whose minor-axis range overlaps the
 * source leaf. Returns NULL if no such neighbor exists.
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
 * High-level E-aware actions for focus and resize. Each returns 1 if it
 * mutated state, 0 if it was a no-op (no neighbor, root focused, etc.).
 *
 * editorLayoutFocusDirection moves focus to the geometric neighbor.
 * editorLayoutFocusLeafAt sets focus to the leaf containing screen point
 * (x, y) in editor-viewport coordinates; returns 0 if no leaf there.
 * editorLayoutResizeFocused nudges the focused leaf's parent split ratio
 * by ROTIDE_PANE_RESIZE_STEP, clamped to ROTIDE_PANE_MIN_RATIO. grow=1
 * makes the focused pane larger; grow=0 makes it smaller.
 * editorLayoutFocusedLeafIndex writes the focused leaf's 0-based position
 * in leftmost-leaf order plus the total leaf count.
 */
#define ROTIDE_PANE_MIN_RATIO 0.10
#define ROTIDE_PANE_RESIZE_STEP 0.05

int editorLayoutFocusDirection(enum editorFocusDirection direction);
int editorLayoutFocusLeafAt(int x, int y);
int editorLayoutResizeFocused(int grow);
int editorLayoutFocusedLeafIndex(int *out_index, int *out_count);

/*
 * When the tree has more than one leaf, push a "Pane X/N" status message
 * so the user gets feedback after a split / close / focus action. No-op
 * for the single-leaf case so it doesn't spam the message bar during
 * normal editing.
 */
void editorPaneAnnounceFocus(void);

/*
 * Serialize the pane tree to a compact s-expression form for persistence.
 * Format: `leaf` for any leaf (kind is not preserved — terminals lapse to
 * editor leaves on restore), `(v <ratio> <left> <right>)` for vertical
 * splits, `(h <ratio> <top> <bottom>)` for horizontal splits. Returns the
 * number of bytes written to `out` (excluding the null terminator), or 0
 * if the buffer is too small.
 */
size_t editorLayoutSerialize(const struct editorPaneNode *root, char *out, size_t out_size);

/*
 * Parse a layout string produced by editorLayoutSerialize and return a
 * fresh tree. Caller takes ownership via editorPaneNodeFree. Returns
 * NULL on syntax error or allocation failure.
 */
struct editorPaneNode *editorLayoutDeserialize(const char *s);

#endif
