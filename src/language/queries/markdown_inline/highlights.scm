; Rotide-authored Markdown inline highlights.
; Upstream tree-sitter-markdown-inline highlights.scm uses @text.* capture
; names that rotide's capture-name table does not map to a highlight class.
; This curated query targets recognized prefixes so emphasis, code spans,
; links, autolinks, and inline LaTeX render with sensible classes against
; the pinned grammar.

(emphasis) @keyword
(strong_emphasis) @keyword
(strikethrough) @comment

(code_span) @string
(latex_block) @string

[
  (emphasis_delimiter)
  (code_span_delimiter)
  (latex_span_delimiter)
] @punctuation

(link_text) @function
(image_description) @function
(link_label) @constant
(link_destination) @string
(link_title) @string

(uri_autolink) @string
(email_autolink) @string
(html_tag) @type

[
  (backslash_escape)
  (hard_line_break)
] @string.escape

[
  (entity_reference)
  (numeric_character_reference)
] @constant
