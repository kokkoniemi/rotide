; Helm / Go template highlighting.
;
; Authored in-repo for RotIDE's minimal helm grammar. The literal text between
; `{{ ... }}` actions is injected as YAML (see injections.scm); this query only
; colors the template actions themselves.

[
  "{{"
  "{{-"
  "}}"
  "-}}"
  "("
  ")"
] @punctuation.bracket

"," @punctuation.delimiter

(comment) @comment

(keyword) @keyword

(operator) @operator

(boolean) @boolean
(nil) @constant.builtin
(number) @number
(string) @string

(field) @property
(variable) @variable
(dot) @variable.builtin

(identifier) @function
