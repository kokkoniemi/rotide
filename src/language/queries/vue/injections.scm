; Repo-local Vue injections.
;
; Upstream inherits html_tags and relies on `#not-lua-match?` / `#lua-match?` /
; `#gsub!` predicates RotIDE's injection engine does not implement. This mirrors
; the vendored HTML injections instead: <script> bodies parse as JavaScript,
; <style> bodies as CSS, and `{{ ... }}` interpolation expressions as
; JavaScript. The Vue grammar reuses HTML's script_element / style_element /
; raw_text nodes and adds `interpolation`, so these patterns compile against it.

((script_element
  (raw_text) @injection.content)
 (#set! injection.language "javascript"))

((style_element
  (raw_text) @injection.content)
 (#set! injection.language "css"))

((interpolation
  (raw_text) @injection.content)
 (#set! injection.language "javascript"))
