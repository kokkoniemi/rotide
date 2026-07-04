; Rotide-authored Kotlin highlights for tree-sitter-grammars/tree-sitter-kotlin
; v1.1.0. The upstream grammar ships no queries; this bundle targets its
; concrete node names (identifier / property_declaration / etc.) rather than
; the fwcd-fork names that nvim-treesitter uses. `true`, `false`, and `null`
; are just `identifier` tokens in this grammar, so they fall through to
; @variable rather than getting @constant.builtin styling.

; Keywords
[
  "package"
  "import"
  "class"
  "interface"
  "object"
  "enum"
  "annotation"
  "typealias"
  "companion"
  "constructor"
  "init"
  "fun"
  "val"
  "var"
  "this"
  "super"
  "return"
  "throw"
  "as"
  "is"
  "in"
  "out"
  "by"
  "field"
  "get"
  "set"
] @keyword

[
  "if"
  "else"
  "when"
  "where"
] @keyword

[
  "for"
  "while"
  "do"
] @keyword

[
  "try"
  "catch"
  "finally"
] @keyword

; Modifiers
[
  "public"
  "private"
  "internal"
  "protected"
  "abstract"
  "final"
  "open"
  "override"
  "sealed"
  "data"
  "inner"
  "inline"
  "noinline"
  "crossinline"
  "vararg"
  "lateinit"
  "const"
  "operator"
  "infix"
  "suspend"
  "tailrec"
  "external"
  "expect"
  "actual"
  "value"
] @keyword

; Operators
[
  "!"
  "!="
  "!=="
  "%"
  "%="
  "&&"
  "*"
  "*="
  "+"
  "++"
  "+="
  "-"
  "--"
  "-="
  "->"
  "/"
  "/="
  "<"
  "<="
  "="
  "=="
  "==="
  ">"
  ">="
  "?:"
  "?."
  "?"
  ".."
  "||"
] @operator

; Punctuation
[
  "("
  ")"
  "["
  "]"
  "{"
  "}"
] @punctuation.bracket

[
  ","
  ";"
  "."
  "::"
] @punctuation.delimiter

; Literals
(number_literal) @number
(float_literal) @number
(character_literal) @string
(escape_sequence) @string.escape

; Strings
(string_literal) @string
(multiline_string_literal) @string
(string_content) @string

; Comments
(line_comment) @comment
(block_comment) @comment

; Types
(user_type
  (identifier) @type)

; Function declarations
(function_declaration
  name: (identifier) @function)

; Class / object / interface names
(class_declaration
  name: (identifier) @type)

(object_declaration
  name: (identifier) @type)

; SCREAMING_SNAKE_CASE identifiers are treated as constants everywhere.
((identifier) @constant
  (#match? @constant "^[A-Z][A-Z0-9_]*$"))

; Every remaining identifier falls through to @variable.
(identifier) @variable
