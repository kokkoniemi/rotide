;; Repo-local GDScript highlights.
;;
;; PrestonKnopp/tree-sitter-gdscript vendors no highlight queries of its own, so
;; RotIDE ships this query instead of concatenating an upstream one. It targets
;; the node names that actually exist in the pinned grammar (root node `source`)
;; and uses only capture names / predicates RotIDE models (no `#lua-match?`).
;;
;; RotIDE resolves overlapping captures last-wins by query order: broad patterns
;; come first, naming-convention heuristics next, and specific structural
;; overrides (calls, parameters, definitions) last so they win.

;; --- Broad identifiers (overridden below) --------------------------------

(identifier) @variable
(name) @variable

;; --- Naming-convention heuristics ----------------------------------------

;; TitleCase identifiers read as types; SCREAMING_SNAKE_CASE as constants.
((identifier) @type
 (#match? @type "^[A-Z]"))

((identifier) @constant
 (#match? @constant "^[A-Z][A-Z0-9_]*$"))

((identifier) @constant.builtin
 (#any-of? @constant.builtin "self" "super"))

;; --- Types ---------------------------------------------------------------

(type (identifier) @type)
(class_name_statement (name) @type)

;; --- Comments ------------------------------------------------------------

(comment) @comment
(region_start) @comment
(region_end) @comment

;; --- Literals ------------------------------------------------------------

(string) @string
(string_name) @string
[
  (node_path)
  (get_node)
] @string.special
(escape_sequence) @string.escape
(integer) @number
(float) @number.float
[
  (true)
  (false)
] @boolean
(null) @constant.builtin

;; --- Annotations (@export, @onready, @tool, ...) -------------------------

(annotation
  "@" @attribute
  (identifier) @attribute)

;; --- Definitions ---------------------------------------------------------

(const_statement (name) @constant)
(enumerator left: (identifier) @constant)

(function_definition (name) @function)
(constructor_definition "_init" @function)
[
  (setter)
  (getter)
] @function

(set_body "set" @keyword)
(get_body "get" @keyword)

;; --- Parameters ----------------------------------------------------------

(parameters (identifier) @variable.parameter)
(typed_parameter (identifier) @variable.parameter)
(default_parameter (identifier) @variable.parameter)
(typed_default_parameter (identifier) @variable.parameter)

;; --- Calls and attribute access ------------------------------------------

(call (identifier) @function.call)
(base_call (identifier) @function.call)
(attribute_call (identifier) @function.method)
(attribute_subscript (identifier) @property)
(attribute
  (_)
  (identifier) @property)

;; --- Punctuation ---------------------------------------------------------

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
  "."
  ":"
  ";"
] @punctuation.delimiter

;; --- Operators -----------------------------------------------------------

[
  "+"
  "-"
  "*"
  "/"
  "%"
  "**"
  "~"
  "<<"
  ">>"
  "&"
  "^"
  "|"
  "<"
  ">"
  "<="
  ">="
  "=="
  "!="
  "!"
  "&&"
  "||"
  "="
  "+="
  "-="
  "*="
  "/="
  "%="
  "**="
  ">>="
  "<<="
  "&="
  "^="
  "|="
  "->"
] @operator
(inferred_type) @operator
(pattern_open_ending) @operator

[
  "and"
  "or"
  "not"
  "in"
  "is"
  "as"
] @keyword.operator

;; --- Keywords ------------------------------------------------------------

[
  "if"
  "elif"
  "else"
  "match"
] @keyword.conditional
(pattern_guard "when" @keyword.conditional)

[
  "for"
  "while"
] @keyword.repeat
[
  (break_statement)
  (continue_statement)
] @keyword.repeat

[
  "var"
  "const"
  "class_name"
  "extends"
  "signal"
  "onready"
  "setget"
] @keyword
(static_keyword) @keyword.modifier
(remote_keyword) @keyword
(pass_statement) @keyword
(breakpoint_statement) @keyword.debug
"export" @keyword

[
  "enum"
  "class"
] @keyword.type

"func" @keyword.function
"return" @keyword.return
"await" @keyword
