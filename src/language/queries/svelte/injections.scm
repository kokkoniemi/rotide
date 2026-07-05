; Repo-local Svelte injections.
;
; Upstream ships `; inherits: html_tags` plus a catch-all `(raw_text)` pattern
; that RotIDE's injection registry does not model (no query inheritance, and the
; catch-all would inject <style> bodies as JavaScript). This mirrors the vendored
; HTML injections instead: <script> bodies parse as JavaScript and <style>
; bodies parse as CSS. The Svelte grammar reuses HTML's script_element,
; style_element, and raw_text node types, so these patterns compile against it.

((script_element
  (raw_text) @injection.content)
 (#set! injection.language "javascript"))

((style_element
  (raw_text) @injection.content)
 (#set! injection.language "css"))
