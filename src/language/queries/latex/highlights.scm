; Rotide-authored LaTeX highlights. The upstream tree-sitter-latex grammar
; intentionally does not ship highlight queries.

[
  (line_comment)
  (comment)
] @comment

(command_name) @function
(todo_command_name) @function

(begin
  command: "\\begin" @keyword
  name: (curly_group_text) @type)
(end
  command: "\\end" @keyword
  name: (curly_group_text) @type)

(generic_command
  command: (command_name) @function)

(class_include
  command: "\\documentclass" @keyword
  path: (curly_group_path) @string)
(package_include
  command: ["\\usepackage" "\\RequirePackage"] @keyword
  paths: (curly_group_path_list) @string)

(part
  command: ["\\part" "\\part*"] @keyword)
(chapter
  command: ["\\chapter" "\\chapter*"] @keyword)
(section
  command: ["\\section" "\\section*" "\\addsec" "\\addsec*"] @keyword)
(subsection
  command: ["\\subsection" "\\subsection*"] @keyword)
(subsubsection
  command: ["\\subsubsection" "\\subsubsection*"] @keyword)
(paragraph
  command: ["\\paragraph" "\\paragraph*"] @keyword)

(citation
  command: _ @function
  keys: (curly_group_text_list) @constant)
(label_definition
  command: "\\label" @function
  name: (curly_group_label) @constant)
(label_reference
  command: _ @function
  names: (curly_group_label_list) @constant)
(label_reference_range
  command: _ @function
  from: (curly_group_label) @constant
  to: (curly_group_label) @constant)

[
  (path)
  (uri)
] @string

(value_literal) @number
(label) @constant

[
  "$"
  "$$"
  "\\("
  "\\)"
  "\\["
  "\\]"
  "_"
  "^"
] @operator

(math_delimiter) @operator
(letter) @variable
