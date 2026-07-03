# Configuration

RotIDE reads TOML config from `~/.rotide/config.toml` only. On first launch it
auto-creates that file, and `~/.rotide`, with the documented defaults.

Project-local `.rotide.toml` files are not loaded for general editor/LSP
settings. Opening an untrusted repo should not let it override LSP server
commands, keybindings, or other settings.

See [`config.toml.example`](../../config.toml.example) for the complete option
list. Common sections:

- `[editor]`: cursor style/blink, line wrap, line numbers, current-line
  highlight, indentation, column selection drag modifier.
- `[theme]`: built-in or custom theme selection.
- `[lsp]`: language-server enable flags, commands, install commands, and
  autocomplete settings.
- `[input]`: active editing-input system (`vim`, the default, or `cua`).
- `[keymap.cua]`: CUA action bindings. Git actions (`git_stage`, `git_commit`,
  `git_branches`, `git_push`, …) are bindable here; they have no default
  chords because the Git drawer and views expose them as single letters.
- `[keymap.vim]`: mode-qualified Vim bindings (`normal.*`, `insert.*`,
  `visual.*`) plus leader sub-keys (`leader.*`, including `leader.git_drawer`).
- Custom themes can set `diff_added_bg` / `diff_removed_bg` under `[theme.ui]`
  to override the derived green/red diff line tints.

Built-in themes include `terminal`, `a11y-dark`, `a11y-light`, `acme`,
`silentium`, `256noir`, `github-light`, `github-dark`, `modus-operandi`,
`modus-operandi-tinted`, `modus-vivendi`, `modus-vivendi-tinted`, `molokai`,
`kanagawa-wave`, `kanagawa-dragon`, and `kanagawa-lotus`.
