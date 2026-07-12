# Vim Input

RotIDE has one editing model: Vim. Keyboard dispatch keeps focus-specific gates
in `src/input/dispatch.c`, then calls `editorVimHandleKey` directly. There is
no input-system selector or registry.

## Dispatch boundary

`editorProcessKeypress` handles input in this order:

1. synthetic events and popups;
2. mouse input;
3. DAP console and focused terminal input;
4. focus-specific Git drawer/view keys;
5. file-search and project-search text fields;
6. the Vim modal handler.

Focus-specific keys translate to `enum editorAction` and use
`editorDispatchProcessMappedAction`. Vim does the same whenever an existing
action expresses the operation. Text mutations continue through the normal edit
pipeline; input code does not write derived rows directly.

Search fields are deliberately small plain-text contexts. They accept text,
Backspace/Delete, arrows, Enter, and Esc without pretending to be another
editing system or changing the active document.

## Vim state and commands

`src/input/system_vim.c` owns Normal, Insert, Visual, Visual-Line, and
Visual-Block state. Modal state is stored with the active buffer in
`E.input_vim_*`, so tabs remember their mode and pending sequences. Named Vim
registers are global; the default register uses the editor clipboard.

Counts, operators, registers, search, text objects, find-character motions,
marks, bracket matching, screen motions, dot repeat, and ex commands are
structural built-ins. Shared workspace and language behavior remains
action-driven.

The direct interface is declared in `src/input/system_vim.h`:

- `editorVimInitialize` and `editorVimReset`;
- `editorVimHandleKey` and `editorVimKeySequencePending`;
- `editorVimBindKey` and `editorVimKeymapResetDefaults`;
- status and cursor helpers used by rendering;
- leader and `Ctrl-W` resolvers shared with terminal input.

## Configuration

`[keymap.vim]` is the only keymap table. Global bindings load first and the
project `.rotide.toml` overrides them. Invalid entries reset the affected load
to defaults and report the corresponding global or project error.

Bindings are mode-qualified:

```toml
[keymap.vim]
normal.move_left = "h"
normal.delete = "d"
visual.yank = "y"
insert.normal_mode = "esc"
normal.leader = "space"
leader.find_file = "p"
```

Bindable modes are `normal`, `insert`, `visual`, and the synthetic
`leader` table. Values are a case-sensitive printable character or a named key
such as `space`, `esc`, or `ctrl+c`. Rebinding relocates a command within
its mode. Structural grammar keys are rejected.

The default leader is Space. Default sub-keys are `p` file search, `f`
project search, `e` explorer, `m` main menu, `g` Git, `l` LSP, and `d`
DAP. Toggle-drawer and blame actions are bindable but unbound; Normal mode uses
`gb` for blame.

## Explicit modified keys

Terminal decoding for Alt and modified arrows remains protocol support. The
following shortcuts are intentionally owned by Vim rather than inherited from a
second keymap:

- `Alt-Left` / `Alt-Right`: previous/next tab;
- `Alt-Up` / `Alt-Down`: move the current line;
- `Alt-C`: toggle comment;
- `Alt-Z` / `Alt-N` / `Alt-H`: toggle wrapping, line numbers, and
  current-line highlight;
- arrows, Home/End, Page Up/Down, Delete, Backspace, Enter, and Ctrl-arrow
  navigation where the active Vim mode accepts them.

Duplicate workspace chords were removed in favor of leader, `g`, `Ctrl-W`,
Visual-Block, and ex routes.

## Control keys

Vim owns the C0 control range. Unmapped controls are inert. Built-ins include:

- Normal: `Ctrl-R` redo, `Ctrl-C` cancel pending input, `Ctrl-V`
  Visual-Block;
- Normal/Visual: `Ctrl-D`/`Ctrl-U` half-page,
  `Ctrl-F`/`Ctrl-B` page, `Ctrl-E`/`Ctrl-Y` one-line scroll;
- Insert: `Ctrl-C` Normal mode, `Ctrl-H` backspace, `Ctrl-W` delete word,
  and `Ctrl-U` delete to indentation/line start.

`Ctrl-W` in Normal mode is the window prefix. It handles splits, pane close,
pane focus, moving tabs between panes, and opening a terminal tab. The
`g`-prefix handles LSP and Git navigation such as `gd`, `gi`, `gr`,
`gs`, and `gb`.

## Terminal focus

A focused terminal intercepts input before document Vim handling. Job/Insert
mode forwards every key to the child PTY except the reserved `Ctrl-W` prefix.
`Ctrl-W N` enters Terminal Normal mode and `Ctrl-W .` sends a literal
`Ctrl-W`.

Terminal Normal mode resolves the live leader and window maps against RotIDE and
does not forward ordinary keys. `i`, `a`, `I`, and `A` return to
Job/Insert. Pending terminal sequences are stored per terminal tab and reset on
focus changes.

## File layout

- `src/input/system_vim.c`: modal implementation, prefix `vimSystem`;
- `src/input/system_vim.h`: direct public Vim interface;
- `src/input/dispatch.c`: focus gates and action dispatch;
- `src/config/keymap.c`: Vim-only configuration loader.

New `.c` files must be listed in `tools/module-prefixes.tsv`.
