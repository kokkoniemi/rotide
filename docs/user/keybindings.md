# Default keybindings

RotIDE uses Vim input exclusively. Normal, Insert, Visual, and Visual-Block
editing follow the modal bindings below. Workspace commands route through the
leader, `g` prefixes, `Ctrl-W`, ex commands, or focus-specific Git and drawer
keys. `Alt-Left`/`Alt-Right`, `Alt-Up`/`Alt-Down`, `Alt-C`, and
`Alt-Z`/`Alt-N`/`Alt-H` remain explicit Vim-owned shortcuts.

## Vim leader bindings

In the Vim system, Normal mode supports a leader key (Space by default) followed
by one key:

- `<leader>p`: search files by name
- `<leader>f`: search text across the project
- `<leader>e`: open the explorer drawer (file tree)
- `<leader>m`: open the main menu
- `<leader>g`: open the Git drawer (`B`/`L`/`S` inside it open the Git views)
- `<leader>l` / `<leader>d`: open the LSP Problems/Symbols / Debugger drawer
- `leader.toggle_drawer` and `leader.git_blame_details`: bindable but unbound
  by default (Normal mode already has `gb` for blame)

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

`Ctrl-W q` closes a pane in RotIDE; it does not quit the app. Use `:q` for
app quit semantics.

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
- `:lsp`: open the LSP Problems/Symbols drawer; `:lsp install-server <name>`
  installs a language server (`gopls`, `clangd`, `texlab`,
  `typescript-language-server`, `vscode-langservers-extracted`), and
  `:lsp install-server` with no name targets the current buffer's language

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
- `m<a-z>` sets a mark; `` `<a-z> `` jumps to it, `'<a-z>` to its line. Marks are
  file-qualified: jumping to a mark set in another file switches to that file
- `.` repeats the last change; `ZZ` saves and quits, `ZQ` quits without saving
- `gq` re-wraps text to the text width: `gqq` the current line, `gqap` a
  paragraph, `gq{motion}`, or `gq` over a Visual selection (default width 80;
  the first line's indent is preserved)

## Vim jumplist

Rotide keeps a per-pane jumplist of the positions you jumped from, so you can
retrace navigation across lines and files:

- `Ctrl-O`: jump back to the previous position
- `Ctrl-I` / `Tab`: jump forward again
- both take a count, e.g. `3` `Ctrl-O` jumps back three entries
- `:jumps` prints a compact summary (`jumps <index>/<count>`) with the immediate
  back and forward targets

"Jump" motions that record an entry match Vim: `G`, `gg`, `/`, `?`, `n`, `N`,
`*`, `#`, `{`, `}`, `%`, `H`/`M`/`L`, mark jumps (`` ` ``/`'`), `:<number>`, and
LSP navigation (`gd`/`gi`/`gr`). Stepwise motions (`h`/`j`/`k`/`l`, `w`/`b`,
`f`/`t`, scrolling) do not. Each editor pane has its own list; a split inherits
the list of the pane it was split from, and the lists persist across sessions in
the workspace state.

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
available editor actions without default Vim bindings.

## Vim Git

- `gb`: show Git blame details for the current line

## Git drawer and views

The Git drawer (`<leader>g` or `:git`) lists an Actions section
followed by Staged / Changes / Untracked / Conflicts. The Actions rows (Commit
staged…, Amend last commit…, Branches, Commit log, Stashes, Push, Pull, Fetch,
Refresh) run on Enter or a mouse click; push/pull/fetch run as background tasks
with their output in a task tab.

On a file row, Enter or a plain mouse click opens the file's diff directly.
Right-click opens a small menu (Open Diff / Stage-or-Unstage / Discard…) that
is navigable with the arrow keys. Right-clicking a group header offers Stage
all / Unstage all for that group.

While a Git surface has focus (the drawer or any git tab), the status bar
replaces the tab name with the actions that currently apply as clickable
buttons — in the drawer, stage/unstage/discard appear only when a file row is
selected (stage or unstage matching the selected group), "Stage all" /
"Unstage all" when a group header is selected, and commit only when something
is staged —
with nerd-font icons when enabled, like the debug controls — and each label
starts with the key that runs it, so the shortcuts teach themselves.
Single-letter shortcuts take precedence while the drawer has focus:

- `s`: stage the selected file (unstages it when selected under Staged); on a
  group header (Staged / Changes / Untracked / Conflicts) it applies to every
  file in that group
- `u`: unstage the selected file, or the whole group on the Staged header
- `d`: discard the selected file's changes (asks for confirmation)
- `c` / `A`: open the commit / amend message tab
- `B` / `L` / `S`: open the branches / commits / stash view

Committing opens an editable `git commit` message tab: type the message and
save with `:w` to commit; lines starting with `#` are ignored; closing
the tab without saving aborts the commit.

Diff tabs open as preview tabs (one shared slot, like drawer file previews), so
browsing diffs does not accumulate tabs. The `+`/`-` patch prefixes are
stripped: added lines get a green-tinted background, removed lines a red one,
and single-file diffs are syntax highlighted with the file's own language.
Inside a diff tab `z` toggles between the changed hunks and the whole file,
and `R` regenerates the diff. Custom themes can override the tint colors with
the `diff_added_bg` / `diff_removed_bg` UI roles.

The branches, commits, and stash views are read-only tabs; move with the usual
navigation keys (search works too), then:

- Branches: Enter or double-click checks out the selected branch; `n` creates a
  branch (prompt); `d` deletes it (confirm); `R` refreshes. Push/pull/fetch
  intentionally have no single-letter keys (accident-prone); run them from the
  drawer's Actions rows, the status-bar buttons, or bound chords
- Commits: Enter or double-click shows the commit as a diff tab; `c`
  cherry-picks; `r` reverts; `t` tags (prompt); `R` refreshes
- Stash: Enter or double-click shows the stash as a diff tab; `a` applies; `p`
  pops; `d` drops (confirm); `R` refreshes

The status bar shows the current branch, a `+` when the tree is dirty, and
`↑N↓M` ahead/behind counts when the branch has an upstream. If a cherry-pick or
revert hits conflicts, the conflicted files appear in the Git drawer's
Conflicts group; resolve them in the editor and use a terminal pane for
`git cherry-pick --continue` / `--abort`.
