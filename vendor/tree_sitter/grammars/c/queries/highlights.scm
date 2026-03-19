; RotIDE v1 C highlight query for semantic class mapping.

(comment) @comment
(string_literal) @string
(char_literal) @string
(number_literal) @number
[(true) (false)] @constant

(primitive_type) @type
(type_identifier) @type

(call_expression function: (identifier) @function)
(function_declarator (identifier) @function)

[
  (preproc_include)
  (preproc_def)
  (preproc_function_def)
  (preproc_call)
] @preprocessor

[
  "if" "else" "switch" "case" "default" "while" "for" "do"
  "return" "break" "continue" "goto"
  "typedef" "struct" "enum" "union"
  "sizeof" "static" "extern" "inline" "const" "volatile"
] @keyword
