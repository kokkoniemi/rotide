# Features

## Status

RotIDE is under active development. Core editing, tabs, drawer navigation,
search, undo/redo, Tree-sitter highlighting, crash recovery, LSP-backed
definition lookup, an LSP Problems drawer, and incremental ESLint integration
are implemented and tested.

## Feature list

- UTF-8/grapheme-safe editing and cursor movement.
- Multi-tab workflow with preview tabs from drawer clicks.
- Project drawer with expand/collapse, mouse resize, file search, project text
  search, Git changes, and LSP Problems/Symbols views.
- Git workflows: stage/unstage/discard from the Git drawer (single letters or
  a per-file right-click menu), an Actions section for commit/amend,
  views, push/pull/fetch and refresh, commit and amend via an editable message
  tab, branches/commits/stash views (checkout, create and delete branches,
  cherry-pick, revert, tag, stash apply/pop/drop), blame, and ahead/behind
  counts in the status bar. Diffs open in a shared preview tab with green/red
  line tints, the file's own syntax highlighting, and a hunks/whole-file
  toggle. When a Git surface has focus, the status bar shows its actions as
  clickable, icon-labelled buttons (like the debug controls). See "Git drawer
  and views" in [keybindings](keybindings.md).
- Search, go to line, matching bracket jump, selection/copy/cut/paste.
- Undo/redo with edit grouping.
- Selectable editing-input systems via `[input] system`: Vim (default — modal
  editing with motions, operators, counts, registers, search, text objects, and
  an ex command line; the current mode shows in the status bar) or CUA.
- Configurable keymap and editor settings (per-system `[keymap.cua]` /
  `[keymap.vim]`).
- Atomic save flow and crash recovery snapshots.
- Optional OSC52 clipboard sync.
- Built-in themes plus custom TOML themes.
- Tree-sitter syntax highlighting for C/C++, Go, Shell, HTML, JavaScript,
  TypeScript, TSX, CSS/SCSS, JSON/JSONC, Python, PHP, Rust, Java, C#,
  Haskell, Ruby, OCaml, Julia, Scala, EJS, ERB, Markdown, TOML, YAML, XML,
  Make, Diff, Regex, LaTeX, BibTeX, HCL, Lua, GLSL, Kotlin, Svelte, and Vue
  (the last two with JavaScript in `<script>` and CSS in `<style>`).
- LSP definition lookup for Go, C/C++, HTML, CSS/SCSS, JSON, and JavaScript.
- LSP-backed document symbols, problems drawer, and autocomplete where enabled.
- ESLint diagnostics and manual `eslint_fix` action for JavaScript buffers.
- Missing-language-server install/help prompts with read-only task-log output.

Syntax fixture samples are stored in
[`tests/syntax/`](../../tests/syntax/README.md).
