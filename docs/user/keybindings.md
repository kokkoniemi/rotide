# Default keybindings

These are the CUA system's bindings (`[input] system = "cua"`). The default
system is Vim, whose modal keys (motions, operators, `:` ex commands, etc.) are
summarized in [`docs/developer/input-systems.md`](../developer/input-systems.md);
many of these chords (save/quit/tabs/drawer/panes) still apply in Vim too.

- `Ctrl-S`: save
- `Ctrl-Q`: quit, confirming when dirty or a task is running
- `Ctrl-N`: new tab
- `Ctrl-W`: close tab
- `Alt-Right` / `Alt-Left`: next/previous tab
- `Ctrl-Shift-Alt-Left` / `-Right` / `-Up` / `-Down`: move the active tab to the neighbouring pane
- `Ctrl-E`: toggle focus between editor and drawer
- `Ctrl-B`: toggle the drawer (collapse/expand the sidebar)
- `Alt-M`: open the main menu in the drawer
- `Ctrl-P`: search files in the drawer
- `Ctrl-Alt-F`: search text across the project
- `Ctrl-Alt-L`: show LSP Problems/Symbols drawer
- `Ctrl-F`: search
- `Ctrl-G`: go to line
- `Ctrl-]`: jump to matching bracket
- `Ctrl-O` / `Ctrl + left click`: go to definition for supported source buffers
- `Alt-Z`: toggle soft line wrapping
- `Alt-N`: toggle absolute line numbers
- `Alt-H`: toggle current-line highlight
- `Ctrl-A`: select all
- `Shift-Arrow`: extend selection (a plain arrow collapses it); `Ctrl-Shift-Left`/`-Right` extend by word; `Shift-Home`/`Shift-End` extend to line start/end
- `Alt-Shift-Arrow`: column (box) selection
- `Ctrl-C` / `Ctrl-X` / `Ctrl-D` / `Ctrl-V`: copy/cut/delete/paste selection
- `Ctrl-Z` / `Ctrl-Y`: undo/redo
- `Ctrl-Left` / `Ctrl-Right`: move by word
- `Ctrl-Up` / `Ctrl-Down`: scroll the viewport up/down without moving the cursor
- arrows, home/end, page up/page down: movement and viewport navigation

## Vim leader bindings

In the Vim system, Normal mode supports a leader key (Space by default) followed
by one key:

- `<leader>p`: search files by name
- `<leader>f`: search text across the project
- `<leader>e`: toggle the drawer (explorer)
- `<leader>m`: open the main menu

`eslint_fix`, `scroll_left`, `scroll_right`, `toggle_selection` (modal keyboard
selection, superseded by Shift+move), and `resize_drawer_narrow`/
`resize_drawer_widen` are configurable actions without default bindings in the
built-in keymap.
