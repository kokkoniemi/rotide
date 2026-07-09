# Configuration

RotIDE reads its config from `~/.rotide/config.toml`. On first launch it
auto-creates that file, and `~/.rotide`, with the documented defaults. See
[`config.toml.example`](../../config.toml.example) for the complete option
list.

A project-local `<project>/.rotide.toml` is read only for `[input]`,
`[keymap.cua]`/`[keymap.vim]`, and `[dap.launch.*]`; for those sections it
overrides the global file. Everything else — editor behavior, themes, LSP
server and install commands, DAP adapter commands — is global-only, so opening
an untrusted repo cannot change which commands the editor runs.

Sections:

- `[editor]`: cursor style/blink, line wrap, line numbers, current-line
  highlight, nerd-font icons, indentation, column selection drag modifier.
- `[terminal]`: terminal-pane scrollback size.
- `[theme]`: built-in or custom theme selection. Custom themes can set
  `diff_added_bg` / `diff_removed_bg` under `[theme.ui]` to override the
  derived green/red diff line tints.
- `[lsp]`: language-server enable flags, commands, install commands, and
  autocomplete settings.
- `[dap.adapters]` / `[dap.defaults.*]`: debug-adapter commands and launch
  templates. Launch configs themselves live in the project `.rotide.toml`;
  see [docs/developer/debugging.md](../developer/debugging.md).
- `[input]`: active editing-input system (`vim`, the default, or `cua`).
- `[keymap.cua]`: CUA action bindings. Git actions (`git_stage`, `git_commit`,
  `git_branches`, `git_push`, …) are bindable here; they have no default
  chords because the Git drawer and views expose them as single letters.
- `[keymap.vim]`: mode-qualified Vim bindings (`normal.*`, `insert.*`,
  `visual.*`) plus leader sub-keys (`leader.*`).

Built-in themes: `terminal`, `a11y-dark`, `a11y-light`, `acme`, `silentium`,
`256noir`, `github-light`, `github-dark`, `modus-operandi`,
`modus-operandi-tinted`, `modus-vivendi`, `modus-vivendi-tinted`, `molokai`,
`kanagawa-wave`, `kanagawa-dragon`, and `kanagawa-lotus`.
