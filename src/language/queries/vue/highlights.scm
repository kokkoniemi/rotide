; Repo-local Vue highlights, layered on the vendored HTML highlights.
;
; Upstream ships `; inherits: html_tags` plus capture names and predicates
; (`@markup.*`, `@nospell`, `#not-lua-match?`, ...) that RotIDE does not model,
; so this adds only the Vue single-file-component tokens on top of the standard
; HTML markup highlights, using RotIDE's recognized capture classes. The Vue
; grammar reuses HTML's tag/attribute/comment nodes, so the HTML query compiles
; against it unchanged.

; Mustache interpolation delimiters; the `{{ ... }}` body is injected as
; JavaScript by injections.scm.
[
  "{{"
  "}}"
] @punctuation.bracket

; Template directives: v-if, v-for, v-bind, v-on, v-model, etc.
(directive_name) @keyword

; Directive modifiers: .stop, .prevent, .once, ...
(directive_modifier) @function
