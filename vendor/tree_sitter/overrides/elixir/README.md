# Elixir grammar override

RotIDE replaces the pinned `elixir-lang/tree-sitter-elixir` grammar with a
reduced highlight grammar before generation.

The external scanner is **unchanged and kept**: the override still declares every
upstream external and the quoted-content rules still consume them, so the scanner
delivers all string/sigil/heredoc/interpolation tokens as before. Refresh does
not install a query override — the upstream `highlights.scm` and `injections.scm`
are vendored as-is.

Known degradation versus upstream: `#{…}` interpolation inside a *sigil* body is
not separately highlighted (it is part of `quoted_content` / the injected
sub-language). Interpolation inside ordinary strings and charlists is unaffected.

