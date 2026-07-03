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
- `Alt-B`: show Git blame details for the current line
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
- `<leader>g`: open the Git drawer
- `<leader>b`: open the Git branches view
- `<leader>c`: open the Git commits (log) view
- `<leader>s`: open the Git stash view
- `leader.git_blame_details`: configurable Git blame details action; unbound by
  default because Normal mode already uses `gb`

The leader key (`normal.leader`) and each sub-key (`leader.<command>`) are
configurable under `[keymap.vim]`; see `config.toml.example`.

## Vim window commands

In Vim Normal mode, `Ctrl-W` starts a built-in window-command prefix:

- `Ctrl-W s`: horizontal split (top/bottom)
- `Ctrl-W v`: vertical split (side-by-side)
- `Ctrl-W c` / `Ctrl-W q`: close the focused pane; on the last pane this is a no-op
- `Ctrl-W o`: close other panes, keeping the focused pane
- `Ctrl-W w` / `Ctrl-W Ctrl-W`: focus the next pane
- `Ctrl-W W`: focus the previous pane
- `Ctrl-W h` / `j` / `k` / `l`: focus left/down/up/right; arrow keys work too
- `Ctrl-W H` / `J` / `K` / `L`: move the active tab to the neighbour pane

`Ctrl-W q` closes a pane in RotIDE; it does not quit the app. Use `:q` or
`Ctrl-Q` for app quit semantics.

## Vim ex-command aliases

The `:` prompt accepts these Vim-style aliases:

- `:split` / `:sp` and `:vsplit` / `:vs` / `:vsp`: split the current pane
- `:split <file>` / `:vsplit <file>`: split, then open the file in the new pane
- `:e <file>` / `:edit <file>`: open or switch to a file in the current pane
- `:close` / `:clo`: close the focused pane
- `:only` / `:on`: close other panes
- `:tabclose` / `:tabc`, `:bd` / `:bdelete`: close the active tab
- `:tabnew`: create an empty tab
- `:term` / `:terminal`: open a terminal in a horizontal split
- `:vterm`: open a terminal in a vertical split
- `:git`: open the Git drawer; `:git branches` / `log` / `stash` / `commit` /
  `amend` / `push` / `pull` / `fetch` run the matching Git view or action

Press Tab in the `:` prompt to complete and cycle command names. File-path
arguments are not completed yet. `:q` keeps RotIDE's quit-app behavior.

## Vim find-char motions

In Normal and Visual mode, and after an operator:

- `f<char>` / `F<char>`: move to the next/previous `<char>` on the line
- `t<char>` / `T<char>`: move just before/after the next/previous `<char>`
- `;` / `,`: repeat the last find in the same / opposite direction

These compose with counts and operators, e.g. `df,` deletes through the next
comma and `2fx` jumps to the second `x`.

`{` and `}` jump to the previous/next blank line (paragraph boundary) and also
compose with operators (`d}`).

`%` jumps to the matching bracket (operator-compatible, `d%`). `H`/`M`/`L` move
to the top/middle/bottom of the screen. `*` / `#` search for the word under the
cursor forwards / backwards (and prime `n`/`N`).

## Vim editing and marks

- `r<char>`: replace the character(s) under the cursor (`3rx`)
- `~`: toggle case and advance; `J`: join the current line with the next
- `>>` / `<<`: indent / dedent the line(s); `>`/`<` also take motions (`>ip`) and
  work over a Visual selection
- `m<a-z>` sets a mark; `` `<a-z> `` jumps to it, `'<a-z>` to its line
- `.` repeats the last change; `ZZ` saves and quits, `ZQ` quits without saving
- `gq` re-wraps text to the text width: `gqq` the current line, `gqap` a
  paragraph, `gq{motion}`, or `gq` over a Visual selection (default width 80;
  the first line's indent is preserved)

## Vim text objects

Operators and Visual mode accept text objects via `i` (inner) / `a` (around):

- `iw` / `aw`: word; `ip` / `ap`: paragraph
- `i(` `i)` `ib`, `i{` `i}` `iB`, `i[` `i]`, `i<` `i>`: bracket pairs
- `i"` `i'` `` i` ``: quoted spans
- `it` / `at`: inner / around the enclosing `<tag>…</tag>`

For example `ci(` changes inside the parentheses, `da{` deletes a brace block
(including the braces), and `vi"` visually selects a quoted string.

## Vim LSP navigation

In Normal mode, these keys run LSP navigation where the buffer's language server
provides it:

- `gd`: go to definition
- `gi`: go to implementation
- `gr`: list references (jumps when there's one, otherwise opens a chooser)
- `K`: show hover documentation at the cursor
- `gs`: go to symbol
- `gS`: show the LSP Problems/Symbols drawer
- `gg`: go to the first line (unchanged)

`]g` / `[g` jump to the next / previous diagnostic in the current buffer
(wrapping around).

`eslint_fix`, `scroll_left`, `scroll_right`, `toggle_selection` (modal keyboard
selection, superseded by Shift+move), `diagnostic_next`/`diagnostic_prev` (bound
to `]g`/`[g` in Vim), `goto_references` (bound to `gr` in Vim), `hover` (bound
to `K` in Vim), and `resize_drawer_narrow`/`resize_drawer_widen` are
configurable actions without default bindings in the built-in CUA keymap.

## Vim Git

- `gb`: show Git blame details for the current line

## Git drawer and views

The Git drawer (`Ctrl-Alt-G`, `<leader>g`, or `:git`) lists Staged / Changes /
Untracked / Conflicts. While it has focus, single-letter keys work the same in
Vim and CUA (navigation stays on arrows / `j`/`k`, Enter opens the file's diff):

- `s`: stage the selected file (unstages it when selected under Staged)
- `u`: unstage the selected file
- `a`: stage all changes
- `d`: discard the selected file's changes (asks for confirmation)
- `c` / `A`: open the commit / amend message tab
- `R`: refresh the Git status
- `P` / `p` / `f`: push / pull / fetch (runs as a task; output in a task tab)
- `B` / `L` / `S`: open the branches / commits / stash view

Committing opens an editable `git commit` message tab: type the message and
save (`Ctrl-S` / `:w`) to commit; lines starting with `#` are ignored; closing
the tab without saving aborts the commit.

The branches, commits, and stash views are read-only tabs; move with the usual
navigation keys (search works too), then:

- Branches: Enter or double-click checks out the selected branch; `n` creates a
  branch (prompt); `d` deletes it (confirm); `R` refreshes; `P`/`p`/`f`
  push/pull/fetch
- Commits: Enter or double-click shows the commit as a diff tab; `c`
  cherry-picks; `r` reverts; `t` tags (prompt); `R` refreshes
- Stash: Enter or double-click shows the stash as a diff tab; `a` applies; `p`
  pops; `d` drops (confirm); `R` refreshes

The status bar shows the current branch, a `+` when the tree is dirty, and
`↑N↓M` ahead/behind counts when the branch has an upstream. If a cherry-pick or
revert hits conflicts, the conflicted files appear in the Git drawer's
Conflicts group; resolve them in the editor and use a terminal pane for
`git cherry-pick --continue` / `--abort`.
