; Rotide-authored Markdown (block) highlights.
; Upstream tree-sitter-markdown highlights.scm uses @text.* capture names
; (e.g. @text.title, @text.literal, @text.uri) that rotide's capture-name
; table does not map to a highlight class. This curated query targets
; recognized prefixes so headings, code fences, lists, links, and tables
; render with sensible classes against the pinned grammar.

(atx_heading) @keyword
(setext_heading) @keyword

[
  (atx_h1_marker)
  (atx_h2_marker)
  (atx_h3_marker)
  (atx_h4_marker)
  (atx_h5_marker)
  (atx_h6_marker)
  (setext_h1_underline)
  (setext_h2_underline)
] @punctuation

(fenced_code_block_delimiter) @punctuation
(info_string) @type
(language) @type
(indented_code_block) @string

[
  (list_marker_plus)
  (list_marker_minus)
  (list_marker_star)
  (list_marker_dot)
  (list_marker_parenthesis)
  (thematic_break)
  (block_continuation)
  (block_quote_marker)
] @punctuation

[
  (task_list_marker_checked)
  (task_list_marker_unchecked)
] @constant

(link_label) @constant
(link_destination) @string
(link_title) @string

(backslash_escape) @string.escape

(pipe_table_header) @keyword
[
  "|"
  (pipe_table_delimiter_cell)
] @punctuation
