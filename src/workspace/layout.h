#ifndef ROTIDE_LAYOUT_H
#define ROTIDE_LAYOUT_H

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

enum editorPaneKind {
	EDITOR_PANE_KIND_EDITOR = 0
};

enum editorSplitOrientation {
	EDITOR_SPLIT_HORIZONTAL = 0,
	EDITOR_SPLIT_VERTICAL
};

/*
 * Per-pane view state.
 *
 * cached_for_tab_idx is the global active-tab index this view was last
 * captured against. -1 means "no valid cache; on focus-in the new pane
 * should inherit whatever E currently shows." When focus moves into a pane
 * whose cached_for_tab_idx matches the current active tab, the cached
 * cursor/scroll is loaded. When it doesn't, E is left as-is.
 *
 * Phase 2 restriction: all panes share the global active tab. The cache
 * therefore stays valid only while the active tab is unchanged; tab
 * switches invalidate every pane's cache via cached_for_tab_idx mismatch.
 * Per-pane independent active tabs are a Phase 5 follow-up.
 */
struct editorPaneView {
	int cached_for_tab_idx;
	int cx;
	int cy;
	int rx;
	int rowoff;
	int coloff;
	int wrapoff;
	size_t cursor_offset;
	int viewport_mode;
};

struct editorPane {
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

int editorLayoutComputeInto(const struct editorPaneNode *root,
		struct editorRect viewport,
		struct editorLeafLayout *out);
void editorLeafLayoutFree(struct editorLeafLayout *out);

struct editorPaneNode *editorLayoutLeafAt(struct editorLeafLayout *layout,
		int x, int y);

/*
 * Walks the tree and writes the rect of `leaf` into `*out`. Returns 1 if the
 * leaf is found under `root`, else 0. Does not allocate. Useful when the
 * caller only needs one leaf's rect and wants to avoid materializing the full
 * leaf-layout array.
 */
int editorLayoutLeafRect(const struct editorPaneNode *root,
		struct editorRect viewport,
		const struct editorPaneNode *leaf,
		struct editorRect *out);

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

int editorLayoutComputeBorderedInto(const struct editorPaneNode *root,
		struct editorRect viewport,
		int border_size,
		struct editorLeafLayout *out);
int editorLayoutLeafRectBordered(const struct editorPaneNode *root,
		struct editorRect viewport,
		int border_size,
		const struct editorPaneNode *leaf,
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

/*
 * View state capture/load for focus changes between panes.
 *
 * editorPaneViewInit resets a view to its "no valid cache" state.
 *
 * editorPaneViewCaptureFromState snapshots E's cursor/scroll into the view
 * and stamps the cache with the current active tab index.
 *
 * editorPaneViewLoadIntoState overwrites E's cursor/scroll from the view,
 * but only if the cached tab matches the current active tab. Returns 1 if
 * the load happened, 0 if the cache was stale (leaving E unchanged).
 *
 * editorLayoutSetFocusedLeaf orchestrates a focus change: captures from E
 * into the previously focused leaf's view, updates E.focused_leaf, and
 * loads the new leaf's view back into E. A no-op when new_leaf is already
 * focused or is not a leaf in E.layout_root.
 */
void editorPaneViewInit(struct editorPaneView *view);
void editorPaneViewCaptureFromState(struct editorPaneView *view);
int editorPaneViewLoadIntoState(const struct editorPaneView *view);
int editorLayoutSetFocusedLeaf(struct editorPaneNode *new_leaf);

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
struct editorPaneNode *editorLayoutSplitFocused(
		enum editorSplitOrientation orientation, double ratio);
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

struct editorPaneNode *editorLayoutFindNeighborLeaf(
		const struct editorLeafLayout *layout,
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

#endif
