RotIDE highlight-oriented Haskell grammar override.

This grammar keeps the named nodes used by RotIDE's highlight, locals, and injection queries
(`comment`, `pragma`, `string`, `char`, `integer`, `float`, `constructor`, `name`,
`signature`, `function`, `variable`, `quasiquote`, `quoter`, and `quasiquote_body`) while
collapsing the full upstream grammar and removing the external layout scanner.
