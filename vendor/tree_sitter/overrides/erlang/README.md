# Erlang grammar override

RotIDE replaces the pinned `WhatsApp/tree-sitter-erlang` grammar with a reduced
highlight grammar before generation.

The upstream grammar parses a broad Erlang/erlfmt AST and ships a small external
scanner for triple-quoted strings and sigils. For RotIDE syntax highlighting, the
full AST is more detail than the query path needs. This override keeps the node
and field names used by the Erlang highlight query, removes externals, flattens
most expression bodies into token streams, and recognizes only the structured
islands that materially affect highlighting: attributes, macros, calls, remotes,
records, tuples/lists/binaries/maps, simple operators, and clauses.

Refresh also installs `highlights.scm` from this directory. It is the upstream
query with the wildcard-attribute `attr_name` pattern dropped because the reduced
parser state graph cannot make that nested structure query-compilable. The
fixture contract records the resulting degradation: broad literals and common
keywords still highlight, but some full-AST semantic captures such as module
attribute keyword grouping, function-clause names, record type names, and strings
inside flat expression bodies are intentionally reduced.

Current result: `parser.c` is ~1.28 MB / 484 states with no `scanner.c`, down
from ~2.18 MB / 1567 states plus the upstream scanner.
