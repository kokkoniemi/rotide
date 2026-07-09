# Elixir grammar override

RotIDE replaces the pinned `elixir-lang/tree-sitter-elixir` grammar with a
reduced highlight grammar before generation.

Elixir is homoiconic — `def foo do … end`, `if x do … end`, `import X`, and
pipelines are all just calls — so its highlighting is driven almost entirely by
the call/dot/operator tree shape. Unlike the C-style grammars in
`overrides/swift` or `overrides/perl`, that structure cannot be flattened into a
token stream without losing the function-call, remote-call, def-family, and pipe
coloring the query depends on. So this override is a small, surgical diff against
upstream that keeps every node, field, keyword, operator, literal, sigil, string,
and call shape the highlight and injection queries target. Two changes carry the
reduction:

1. **Flattened operator table.** Upstream models 20 operator precedence tiers as
   20 separate `binary_operator` alternatives. Highlighting only needs the
   `operator:` field captured, not a precedence-correct tree, so every plain
   infix operator collapses into one left- and one right-associative alternative
   at a single precedence. `when` and `|` keep their own alternatives (their
   right-hand side may be a keyword list), and the arity form `name/2` keeps its
   dedicated alternative.

2. **Non-interpolating sigils.** Upstream lets lowercase sigils (`~s`, `~r`,
   `~w`, …) interpolate `#{…}`, which embeds a full expression context inside
   each of the ten delimiter variants and is the single largest source of parser
   states. Both the lower- and upper-case sigil branches here use the
   non-interpolating quoted rules, so a sigil body is one opaque `quoted_content`
   span — still exactly what the injection query extracts. All ten delimiters
   (`" ' """ ''' ( { [ < | /`) are kept.

The external scanner is **unchanged and kept**: the override still declares every
upstream external and the quoted-content rules still consume them, so the scanner
delivers all string/sigil/heredoc/interpolation tokens as before. Refresh does
not install a query override — the upstream `highlights.scm` and `injections.scm`
are vendored as-is.

Known degradation versus upstream: `#{…}` interpolation inside a *sigil* body is
not separately highlighted (it is part of `quoted_content` / the injected
sub-language). Interpolation inside ordinary strings and charlists is unaffected.

Current result: `parser.c` is ~9.4 MB / 4823 states (object ~1.17 MB), down from
upstream's ~13.2 MB / 7001 states (object ~1.60 MB), with the scanner retained.
