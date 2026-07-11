# Input Systems

RotIDE routes keyboard input through an in-tree input-system interface. An input
system owns command naming, mode-specific key bindings, and any small status-bar
segment it wants to expose. Dispatch still owns the editor-wide pre-gates such as
popups, mouse input, terminal panes, DAP console input, and action execution.

Two of those pre-gates serve the Git UI: `dispatchTryGitDrawerKey` claims
single-letter Git shortcuts while the Git drawer has focus, and
`dispatchTryGitViewKey` does the same inside the branches/log/stash view tabs.
Both run before the active input system's `handle_key`, so the letters behave
identically under Vim and CUA, and both translate keys into `enum editorAction`
values dispatched through `editorDispatchProcessMappedAction`. Unclaimed keys
fall through to the input system, which keeps navigation and search working;
the view buffers are read-only, so stray operator keys cannot mutate them.

The default input system is Vim: a compiled-in system with its own modes and
command table that resolves keys to Vim commands, which continue to route edits
and navigation through the editor action and editing paths where semantics match.
CUA is the other built-in system; it preserves the classic desktop-editor keymap
behavior and uses the existing `enum editorAction` action names as its command
names. Both are selectable via `[input] system`.

## Interface

Each system provides a `struct editorInputSystem`:

- `id`: stable config name, such as `cua` or `vim`.
- `on_activate` / `on_deactivate`: lifecycle hooks for switching systems.
- `handle_key`: consumes a terminal key and reports viewport or cursor/edit
  effects to the caller.
- `resolve_command`: maps a configured command name to a system-local command id.
- `bind_key`: binds a mode-qualified command name to a terminal key.
- `status_segment`: writes a short optional status-bar segment.
- `reset`: returns the system to its activation state.

The registry is static and compiled in. `editorInputSystemActivate(id)` selects
the active system, `editorInputSystemActive()` observes it, and
`editorInputSystemById(id)` resolves a known implementation. Unknown ids are
rejected by the registry; config callers fall back to the default (Vim) after
reporting the invalid setting.

## Command Resolution

Config stores command names, not raw function pointers. During load, the active
system resolves names through its command table, then binds the resulting
system-local command id to a key. This lets CUA and Vim define different command
sets while keeping configuration deterministic.

CUA command names are the existing `enum editorAction` names, such as `save`,
`find`, and `move_right`. Vim command names are system-local names for modal
operations, motions, operators, text objects, registers, and the ex command line.
When a Vim command maps directly to existing editor behavior, the implementation
uses the matching `enum editorAction` path rather than bypassing dispatch.

## Configuration

The input-system selector is top-level:

```toml
[input]
system = "vim"
```

Valid values are `cua` and `vim`; Vim is the default. Missing or invalid values
fall back to Vim. Project configuration overrides global configuration.

Each system has its own first-class keymap table — `[keymap.cua]` for CUA and
`[keymap.vim]` for Vim — and the two are treated symmetrically. There is no bare
`[keymap]` alias. Project scope wins over global scope.

```toml
[keymap.cua]
save = "ctrl-shift-s"
```

Vim bindings live under `[keymap.vim]` and are mode-qualified
(`<mode>.<command>`):

```toml
[keymap.vim]
normal.move_left = "h"
normal.delete = "d"
visual.yank = "y"
insert.normal_mode = "esc"
```

Bindable mode names are `normal`, `insert`, and `visual` (the `visual` table also
applies to Visual-Line). Values are usually a single, case-sensitive printable
character; named keys such as `esc` and `ctrl+c` are also accepted. Each command
takes one key per mode — rebinding relocates it, so the built-in default key
stops triggering that command unless it is itself bound to another command.
Commands left unset keep their built-in defaults.

Normal mode also supports a **leader** key: `<leader>` followed by one key
dispatches an editor action. The leader key is set with `normal.leader` (default
`space`; `"space"` and a literal `" "` both resolve to Space) and sub-keys are
bound under a synthetic `leader` table:

```toml
[keymap.vim]
normal.leader = "space"
leader.find_file = "p"
leader.project_search = "f"
leader.explorer_drawer = "e"
leader.main_menu = "m"
# Unbound by default; Normal mode keeps `gb`.
leader.git_blame_details = "b"
```

Default leader bindings are `p`/`f`/`e`/`m` as above plus `g` (Git drawer),
`l` (LSP drawer), and `d` (DAP drawer); `toggle_drawer` and
`git_blame_details` are bindable but unbound.

Leader sub-key names are editor-action names backed by `g_vim_leader_map`; the
leader trigger sits after count/operator gating so it cannot fire mid-sequence.
Leader and sub-keys must be plain printable characters.

Normal mode also has a built-in `Ctrl-W` window prefix tracked by
`E.input_vim_pending_ctrl_w`. The resolver maps `s`/`v` to split actions,
`c`/`q` to close-pane, `o` to close-other-panes, `w`/`Ctrl-W` and `W` to
next/previous pane focus, `h`/`j`/`k`/`l` and arrows to directional focus, and
`H`/`J`/`K`/`L` to move the active tab to a neighbour pane. The prefix is reset
by the shared pending-state reset and participates in idle checks so counts and
dot-repeat do not treat a half-entered window command as normal input.

`g`-prefixed keys (`gb gd gi gr gs gS gg`) and `[`/`]` prefixes (`]g`/`[g` for
next/previous diagnostic) are handled by dedicated pending-state branches in the
Normal handler. `gb` dispatches `git_blame_details` and opens a Git blame popup.
`gr` lists references via the shared location-menu UI. `K` dispatches the
`hover` action and opens an LSP hover popup. The diagnostic jumps dispatch the
`diagnostic_next` / `diagnostic_prev` editor actions, which navigate
`E.lsp_diagnostics` directly.

`gq` starts a reflow operator (`VIM_SYSTEM_OPERATOR_REFLOW`): `gq` after the `g`
prefix sets the pending operator, so `gqq`, `gqap`, and `gq{motion}` flow through
the normal operator machinery, while Visual `gq` reflows the selection.
`vimSystemReflowLines` word-wraps to `E.text_width` (default
`ROTIDE_TEXT_WIDTH_DEFAULT`), preserving the first line's indent.

The ex prompt (`:`) uses `editorPromptWithCompletion` and
`vimSystemExCompleteFn` to complete/cycle command names on Tab. Aliases that
dispatch plain actions live in the shared `g_vim_ex_commands[]` table, so the
dispatcher and completer read the same spelling list. Bespoke commands such as
`:w`, `:q`, `:q!`, `:wq`, `:x`, line-number jumps, `%s`, and argument-bearing
`:e`/`:split`/`:vsplit` forms stay outside that table because they need custom
control flow. Completion adds those built-in names as a name-only superset.

Counts (`3dd`), registers (`"a`), in-buffer search (`/ ? n N`), text objects
(`iw aw ip ap`, bracket/quote pairs `i( a( i{ i[ i< i" i' i\``, tags `it`/`at`),
find-char motions (`f F t T ; ,`), marks (`m` `` ` `` `'`), `%`, `H`/`M`/`L`,
`*`/`#`, `r`/`~`/`J`, `>>`/`<<`, the `.` repeat, and the ex command line (`:`)
are structural built-ins and are
not rebindable. Loading resets Vim bindings to defaults first, then applies the
global file, then the project file; an invalid entry reverts that scope to
defaults and reports a status message.

## Lifecycle

Startup loads global and project config, selects the configured system, and then
loads that system's key bindings. Live reload repeats the same sequence. Switching
systems calls the previous system's deactivate hook before activating the new
system. A reset returns the active system to the same state it has immediately
after activation.

CUA has no visible status segment. Vim uses the status hook for mode labels such
as `-- NORMAL --` and `-- INSERT --`.

## Control keys

In Vim mode the system owns the C0 control range: it runs its own control
bindings and swallows the rest, so CUA `[keymap.cua]` shortcuts (Ctrl-P, Ctrl-Y,
…) never fire while editing. Tab, Enter, and Esc are left to the text/mode paths,
and the non-control navigation key codes (arrows, Home/End, Page Up/Down, Delete,
Backspace) still fall through to their CUA actions so they keep working.
`vimSystemIsControlKey` defines the captured range.

Built-in control bindings:

- Normal: `Ctrl-R` redo, `Ctrl-C` cancels any pending sequence, `Ctrl-V` starts
  Visual-Block.
- Normal/Visual/Visual-Block: `Ctrl-D`/`Ctrl-U` half-page, `Ctrl-F`/`Ctrl-B`
  page (all move the cursor so the scroll sticks), `Ctrl-E`/`Ctrl-Y` scroll one
  line and pull the cursor only as far as needed to stay on screen.
- Visual/Visual-Block: `Ctrl-C` leaves to Normal; `Ctrl-V` toggles to/from
  Visual-Block, preserving the anchor.
- Insert: `Ctrl-C` returns to Normal, `Ctrl-H` backspace, `Ctrl-W` delete the
  word before the cursor, `Ctrl-U` delete to the first non-blank (then to column
  zero). Other Insert control keys are swallowed.

`u` undoes and `Ctrl-R` redoes through the shared `EDITOR_ACTION_UNDO`/`REDO`
paths; both suppress dot-repeat recording so `.` never replays an undo/redo.

## Terminal focus input

A focused terminal leaf intercepts keys before the active input system's document
handling, in `dispatchTryTerminalPaneKey` (`src/input/dispatch.c`). The routing
differs by input system, and the terminal input mode is stored per terminal tab
(`editorTerminalPane.input_mode`), not in the global Vim state.

Vim terminals are modal, mirroring Vim's `:terminal`:

- Job/Insert mode (the default for a new terminal) forwards every key to the
  child PTY through `editorTerminalPaneSendKey`, including `Esc` and every
  Esc-prefixed sequence, `Space`, and Vim command letters — the `<leader>` map is
  not consulted, so `Space` then `p` sends the literal bytes `" p"`. The one
  reserved key is `Ctrl-W`: `Ctrl-W h/j/k/l` (and the other window commands)
  switch panes, `Ctrl-W N` enters Terminal Normal mode, and `Ctrl-W .` sends a
  literal `Ctrl-W` (`0x17`) to the child. The window sub-keys resolve through the
  shared `editorVimCtrlWAction`, so `Ctrl-W t` opens a terminal tab here too.
- Terminal Normal mode resolves `<leader>` and `Ctrl-W` sequences against rotide
  via `editorVimLeaderAction` / `editorVimCtrlWAction` (the same live maps editor
  buffers use, not a copy) and never forwards to the child; `i`/`a`/`I`/`A` return
  to Job/Insert. Other keys are inert, leaving room for future scrollback motions.
  The cursor renders as a steady block and the status badge shows `NORMAL`.

CUA terminals have no modes: every key goes to the PTY except `terminal_prefix`
(default `Ctrl-Alt-A`), which routes exactly one following command to normal
dispatch (`dispatchTerminalCuaKey` returns 0 so the keymap sees the next key).

Pending terminal sequence state (`pending_ctrl_w`, `pending_leader`) is cleared
on focus change via `editorTerminalPaneResetPendingInput`. The focused terminal
owns the status bar's left context segment with a mode badge and clickable,
hotkey-labelled buttons that dispatch actions independent of the PTY, so a
fullscreen child that grabs the keyboard can still be escaped with the mouse.

## Visual selection rendering

Charwise Visual is cursor-inclusive and Visual-Line spans whole lines, which the
generic stream-selection renderer (anchor→cursor, exclusive) does not express on
its own. Two buffer flags bridge this: `E.selection_inclusive` extends the
rendered range one cluster past the trailing end, and `E.selection_linewise`
makes it cover full lines. `editorGetSelectionRange` honors both; CUA leaves them
zero. `vimSystemSyncVisualSelectionFlags` keeps them in step with the Vim visual
state (an explicit half-open range from a text object clears `inclusive`).

Visual-Block reuses the column-selection machinery (`editorColumnSelection*`):
`Ctrl-V` activates it, `hjkl` extend the rect (`vimSystemBlockSync` recomputes
the inclusive `rx` span from the fixed anchor cell and the live cursor), and
`d`/`x`/`y`/`c` operate on the block. It renders for free because the screen
already draws `E.column_select_active`.

## Vim Internals

`system_vim.c` keeps modal state per buffer (`E.input_vim_*` fields) so each tab
remembers its mode, pending operator, count, and active register. Named registers
`a`–`z` are global (`E.vim_registers`), with the system clipboard as the default
register. The handlers switch on canonical keys; a per-mode remap table
(`g_vim_commands`) translates configured keys to those canonical keys before
dispatch, which is why `[keymap.vim]` can rebind keys without touching the
handlers. `[keymap.vim]` parsing lives in `keymap.c` and routes each entry through
the active system's `bind_key`.

## File Layout

The source layout is:

- `src/input/input_system.{c,h}` with prefix `inputSystem`
- `src/input/system_cua.c` with prefix `cuaSystem`
- `src/input/system_vim.c` with prefix `vimSystem`
- `src/config/input_config.{c,h}` with prefix `inputConfig`

New `.c` files must be added to `tools/module-prefixes.tsv` (checked in CI
by `tools/lint-prefixes.sh`).
