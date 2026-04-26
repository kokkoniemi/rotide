((comment) @injection.content
  (#set! injection.language "phpdoc"))

(heredoc
  (heredoc_body) @injection.content
  (heredoc_end) @injection.language
  (#set! injection.include-children))

(nowdoc
  (nowdoc_body) @injection.content
  (heredoc_end) @injection.language
  (#set! injection.include-children))
