# Configuration

RotIDE reads its config from `~/.rotide/config.toml`. On first launch it
auto-creates that file, and `~/.rotide`, with the documented defaults. See
[`config.toml.example`](../../config.toml.example) for the complete option
list.

A project-local `<project>/.rotide.toml` is read for `[keymap.vim]`,
`[dap.launch.*]`, and the safe, project-specific SyncTeX settings
`texlab_pdf_viewer`, `texlab_aux_directory`, `texlab_pdf_directory`, and
`texlab_forward_search_after_build` under `[lsp]`. Those values override the
global file. Everything else — editor behavior, themes, LSP server/build/install
commands, automatic build-on-save, and DAP adapter commands — is global-only,
so opening an untrusted repo cannot replace an executable command or silently
enable a build.

Sections:

- `[editor]`: cursor style/blink, line wrap, line numbers, current-line
  highlight, nerd-font icons, indentation, column selection drag modifier.
- `[terminal]`: terminal-pane scrollback size.
- `[theme]`: built-in or custom theme selection. Custom themes can set
  `diff_added_bg` / `diff_removed_bg` under `[theme.ui]` to override the
  derived green/red diff line tints.
- `[lsp]`: language-server enable flags, commands, install commands,
  autocomplete settings, and Texlab/SyncTeX integration. See
  [LaTeX and SyncTeX](latex-synctex.md).
- `[dap.adapters]` / `[dap.defaults.*]`: debug-adapter commands and launch
  templates. Launch configs themselves live in the project `.rotide.toml`;
  see [docs/developer/debugging.md](../developer/debugging.md).
- `[keymap.vim]`: mode-qualified Vim bindings (`normal.*`, `insert.*`,
  `visual.*`) plus leader sub-keys (`leader.*`).

Built-in themes: `terminal`, `a11y-dark`, `a11y-light`, `acme`, `silentium`,
`256noir`, `github-light`, `github-dark`, `modus-operandi`,
`modus-operandi-tinted`, `modus-vivendi`, `modus-vivendi-tinted`, `molokai`,
`kanagawa-wave`, `kanagawa-dragon`, and `kanagawa-lotus`.
