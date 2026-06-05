# Input Systems

RotIDE routes keyboard input through an in-tree input-system interface. An input
system owns command naming, mode-specific key bindings, and any small status-bar
segment it wants to expose. Dispatch still owns the editor-wide pre-gates such as
popups, mouse input, terminal panes, DAP console input, and action execution.

The default input system is CUA. It preserves the existing keymap behavior and
uses the existing `enum editorAction` action names as its command names. Vim is a
separate compiled-in system with its own modes and command table; it resolves
keys to Vim commands, and those commands continue to route edits and navigation
through the editor action and editing paths where semantics match.

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
rejected by the registry; config callers fall back to CUA after reporting the
invalid setting.

## Command Resolution

Config stores command names, not raw function pointers. During load, the active
system resolves names through its command table, then binds the resulting
system-local command id to a key. This lets CUA and Vim define different command
sets while keeping configuration deterministic.

CUA command names are the existing names used by `[keymap]`, such as `save`,
`find`, and `move_right`. Vim command names are system-local names for modal
operations, motions, operators, text objects, registers, and the ex command line.
When a Vim command maps directly to existing editor behavior, the implementation
uses the matching `enum editorAction` path rather than bypassing dispatch.

## Configuration

The input-system selector is top-level:

```toml
[input]
system = "cua"
```

Valid values are `cua` and `vim`. Missing or invalid values select CUA. Project
configuration overrides global configuration.

CUA bindings live under `[keymap.cua]`. The legacy `[keymap]` table remains a CUA
alias for backward compatibility. When both are present in the same scope,
`[keymap.cua]` wins over `[keymap]`; project scope wins over global scope.

```toml
[keymap]
save = "ctrl-s"

[keymap.cua]
save = "ctrl-shift-s"
```

Vim bindings live under `[keymap.vim]` and are mode-qualified:

```toml
[keymap.vim]
normal.save = "ctrl-s"
insert.escape = "esc"
visual.yank = "y"
```

Mode names are system-defined. The initial Vim modes are `normal`, `insert`,
`visual`, and `visual_line`.

## Lifecycle

Startup loads global and project config, selects the configured system, and then
loads that system's key bindings. Live reload repeats the same sequence. Switching
systems calls the previous system's deactivate hook before activating the new
system. A reset returns the active system to the same state it has immediately
after activation.

CUA has no visible status segment. Vim uses the status hook for mode labels such
as `-- NORMAL --` and `-- INSERT --`.

## File Layout

The planned source layout is:

- `src/input/input_system.{c,h}` with prefix `inputSystem`
- `src/input/system_cua.c` with prefix `cuaSystem`
- `src/input/system_vim.c` and related Vim files with `vimSystem`, `vimNormal`,
  `vimOperator`, `vimExline`, and `vimState` prefixes
- `src/config/input_config.{c,h}` with prefix `inputConfig`

New `.c` files must be added to `tools/module-prefixes.tsv`.
