; Highlight the literal chart body (everything outside `{{ ... }}`) as YAML.
; The text fragments are combined into a single injected YAML document so keys
; split across template actions still parse as one structure.
((text) @injection.content
 (#set! injection.language "yaml")
 (#set! injection.combined))
