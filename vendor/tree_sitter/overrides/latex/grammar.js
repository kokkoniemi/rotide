/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

const commaSep = rule => optional(seq(rule, repeat(seq(',', rule))));

module.exports = grammar({
  name: 'latex',

  extras: $ => [$._whitespace],

  word: $ => $.command_name,

  rules: {
    source_file: $ => repeat(choice(
      $.line_comment,
      $.comment,
      $.class_include,
      $.package_include,
      $.path_include,
      $.path_list_include,
      $.import_include,
      $.hyperlink,
      $.counter_assignment,
      $.part,
      $.chapter,
      $.section,
      $.subsection,
      $.subsubsection,
      $.paragraph,
      $.citation,
      $.label_definition,
      $.label_reference_range,
      $.label_reference,
      $.begin,
      $.end,
      $.displayed_equation,
      $.inline_formula,
      $.math_delimiter,
      $.subscript,
      $.superscript,
      $.generic_command,
      $.curly_group_uri,
      $.curly_group_value,
      $.text,
    )),

    _whitespace: $ => /\s+/,

    line_comment: $ => token(seq('%', /[^\r\n]*/)),

    comment: $ => token(seq('\\iffalse', repeat(choice(/[^\\]+/, /\\[^f]/)), optional('\\fi'))),

    class_include: $ => seq(
      field('command', '\\documentclass'),
      optional($.brack_group),
      field('path', $.curly_group_path),
    ),

    package_include: $ => seq(
      field('command', choice('\\usepackage', '\\RequirePackage')),
      optional($.brack_group),
      field('paths', $.curly_group_path_list),
    ),

    path_include: $ => seq(
      field('command', choice(
        '\\include', '\\subfileinclude', '\\input', '\\subfile',
        '\\bibliographystyle', '\\includegraphics', '\\includesvg',
        '\\includeinkscape', '\\verbatiminput', '\\VerbatimInput',
      )),
      optional($.brack_group),
      field('path', $.curly_group_path),
    ),

    path_list_include: $ => seq(
      field('command', choice('\\bibliography', '\\usepgflibrary', '\\usetikzlibrary')),
      field('paths', $.curly_group_path_list),
    ),

    import_include: $ => seq(
      field('command', choice(
        '\\import', '\\subimport', '\\inputfrom', '\\subimportfrom',
        '\\includefrom', '\\subincludefrom',
      )),
      field('directory', $.curly_group_path),
      field('file', $.curly_group_path),
    ),

    hyperlink: $ => choice(
      seq(
        field('command', '\\url'),
        field('uri', $.curly_group_uri),
      ),
      seq(
        field('command', '\\href'),
        field('uri', $.curly_group_uri),
        field('label', $.curly_group),
      ),
    ),

    counter_assignment: $ => seq(
      field('command', choice('\\setcounter', '\\addtocounter')),
      field('counter', $.curly_group),
      field('value', $.curly_group_value),
    ),

    part: $ => seq(
      field('command', choice('\\part', '\\part*')),
      optional($.brack_group),
      $.curly_group,
    ),

    chapter: $ => seq(
      field('command', choice('\\chapter', '\\chapter*')),
      optional($.brack_group),
      $.curly_group,
    ),

    section: $ => seq(
      field('command', choice('\\section', '\\section*', '\\addsec', '\\addsec*')),
      optional($.brack_group),
      $.curly_group,
    ),

    subsection: $ => seq(
      field('command', choice('\\subsection', '\\subsection*')),
      optional($.brack_group),
      $.curly_group,
    ),

    subsubsection: $ => seq(
      field('command', choice('\\subsubsection', '\\subsubsection*')),
      optional($.brack_group),
      $.curly_group,
    ),

    paragraph: $ => seq(
      field('command', choice('\\paragraph', '\\paragraph*')),
      optional($.brack_group),
      $.curly_group,
    ),

    citation: $ => seq(
      field('command', $.citation_command),
      repeat($.brack_group),
      field('keys', $.curly_group_text_list),
    ),

    citation_command: $ => token(prec(2, /\\([A-Za-z]*cite[A-Za-z]*|nocite)\*?/)),

    label_definition: $ => seq(
      field('command', '\\label'),
      field('name', $.curly_group_label),
    ),

    label_reference: $ => seq(
      field('command', $.label_reference_command),
      field('names', $.curly_group_label_list),
    ),

    label_reference_command: $ => seq(choice(
      '\\ref', '\\eqref', '\\vref', '\\Vref', '\\autoref', '\\pageref',
      '\\autopageref', '\\cref', '\\Cref', '\\cpageref', '\\Cpageref',
      '\\namecref', '\\nameCref', '\\lcnamecref', '\\namecrefs',
      '\\nameCrefs', '\\lcnamecrefs', '\\labelcref', '\\labelcpageref',
    ), optional('*')),

    label_reference_range: $ => seq(
      field('command', $.label_reference_range_command),
      field('from', $.curly_group_label),
      field('to', $.curly_group_label),
    ),

    label_reference_range_command: $ => seq(choice(
      '\\crefrange', '\\Crefrange', '\\cpagerefrange', '\\Cpagerefrange',
    ), optional('*')),

    begin: $ => seq(
      field('command', '\\begin'),
      field('name', $.curly_group_text),
    ),

    end: $ => seq(
      field('command', '\\end'),
      field('name', $.curly_group_text),
    ),

    generic_command: $ => prec.right(seq(
      field('command', choice($.todo_command_name, $.command_name)),
      repeat(choice($.brack_group, $.curly_group)),
    )),

    command_name: $ => token(seq('\\', /[A-Za-z@]+|./)),

    todo_command_name: $ => token(prec(3, /\\[A-Za-z]?[A-Za-z]?todo/)),

    curly_group: $ => seq('{', repeat(choice($.generic_command, $.inline_formula, $.text)), '}'),

    curly_group_text: $ => seq('{', field('text', $.text), '}'),

    curly_group_text_list: $ => seq('{', commaSep(field('text', $.text)), '}'),

    curly_group_label: $ => seq('{', field('label', $.label), '}'),

    curly_group_label_list: $ => seq('{', commaSep(field('label', $.label)), '}'),

    curly_group_path: $ => seq('{', field('path', $.path), '}'),

    curly_group_path_list: $ => seq('{', commaSep(field('path', $.path)), '}'),

    curly_group_uri: $ => seq('{', field('uri', $.uri), '}'),

    curly_group_value: $ => seq('{', field('value', $.value_literal), '}'),

    brack_group: $ => seq('[', repeat(choice($.generic_command, $.text)), ']'),

    displayed_equation: $ => choice(
      seq('$$', repeat(choice($.superscript, $.subscript, $.math_text)), '$$'),
      seq('\\[', repeat(choice($.superscript, $.subscript, $.math_text)), '\\]'),
    ),

    inline_formula: $ => choice(
      seq('$', repeat(choice($.superscript, $.subscript, $.math_text)), '$'),
      seq('\\(', repeat(choice($.superscript, $.subscript, $.math_text)), '\\)'),
    ),

    math_delimiter: $ => seq(
      field('left_command', choice('\\left', '\\bigl', '\\Bigl', '\\biggl', '\\Biggl')),
      field('left_delimiter', $._math_delimiter_part),
      repeat(choice($.superscript, $.subscript, $.math_text)),
      field('right_command', choice('\\right', '\\bigr', '\\Bigr', '\\biggr', '\\Biggr')),
      field('right_delimiter', $._math_delimiter_part),
    ),

    _math_delimiter_part: $ => choice($.command_name, '[', ']', '(', ')', '|'),

    subscript: $ => seq('_', field('subscript', choice($.curly_group, $.letter, $.command_name))),

    superscript: $ => seq('^', field('superscript', choice($.curly_group, $.letter, $.command_name))),

    math_text: $ => token(prec(-1, /[^$\\_^]+|\\./)),

    letter: $ => /[^\\%{}$#_^]/,

    path: $ => /[^*"\[\]:;,|{}<>]+/,

    uri: $ => /[^\[\]{}]+/,

    label: $ => /[^\\\[\]{}$()=&%\s_^#~,]+/,

    value_literal: $ => /(\d+\.)?\d+/,

    text: $ => /[^\\%${}\[\]_^]+/,
  },
});
