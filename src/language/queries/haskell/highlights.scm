; Rotide-authored Haskell highlights.
; Upstream tree-sitter-haskell highlights.scm uses several constructs that
; ts_query_new (against the pinned grammar) rejects with TSQueryErrorStructure
; (e.g. `(decl [name: ... names: ...])` field-named alternation, `decl/bind
; name:` where the field doesn't exist on that supertype variant). Maintaining
; a minimal rotide-curated query keeps Haskell highlighting working until the
; pinned grammar and upstream queries realign.

(comment) @comment
(string) @string
(char) @string
(integer) @number
(float) @number
((constructor) @constant
 (#any-of? @constant "True" "False"))
(constructor) @type
(name) @type
(pragma) @preprocessor
["if" "then" "else" "case" "of" "let" "in" "do" "where"
 "module" "import" "qualified" "as" "hiding" "class" "instance"
 "data" "newtype" "type" "family" "deriving" "via" "stock"
 "anyclass" "forall" "infix" "infixl" "infixr" "pattern" "mdo"
 "rec"] @keyword
