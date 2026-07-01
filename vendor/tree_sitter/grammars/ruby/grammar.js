/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

module.exports = grammar({
  name: 'ruby',

  extras: $ => [/[ \t\f]/, $.comment],
  word: $ => $.identifier,

  rules: {
    program: $ => repeat(choice($._statement, $.newline)),

    _statement: $ => choice(
      $.module,
      $.class,
      $.singleton_method,
      $.method,
      $.alias,
      $.operator_assignment,
      $.assignment,
      $.keyword,
      $.expression_statement,
      $.compatibility_marker,
    ),

    newline: $ => /\r?\n/,
    comment: $ => token(choice('#', seq('#', /[^{\r\n][^\r\n]*/))),

    module: $ => prec.right(12, seq(
      'module', field('name', $.constant), $.newline,
      optional(field('body', $.body_statement)), 'end',
    )),

    class: $ => prec.right(12, seq(
      'class', field('name', $.constant), $.newline,
      optional(field('body', $.body_statement)), 'end',
    )),

    method: $ => prec.right(11, seq(
      'def', field('name', choice($.setter, $.identifier, $.constant)),
      optional(field('parameters', $.method_parameters)), $.newline,
      optional(field('body', $.body_statement)), 'end',
    )),

    singleton_method: $ => prec.right(12, seq(
      'def', field('object', choice($.self, $.constant, $.identifier)), '.',
      field('name', choice($.identifier, $.constant)),
      optional(field('parameters', $.method_parameters)), $.newline,
      optional(field('body', $.body_statement)), 'end',
    )),

    setter: $ => seq(field('name', $.identifier), '='),

    body_statement: $ => repeat1(choice($._statement, $.newline)),
    block_body: $ => repeat1(choice($._statement, $.newline)),

    method_parameters: $ => seq(
      '(', optional(seq($._parameter, repeat(seq(',', $._parameter)))), ')',
    ),

    _parameter: $ => choice(
      $.keyword_parameter,
      $.optional_parameter,
      $.splat_parameter,
      $.hash_splat_parameter,
      $.block_parameter,
      $.destructured_parameter,
      $.identifier,
    ),

    keyword_parameter: $ => seq(
      field('name', $.identifier), ':', optional(field('value', $._expression)),
    ),
    optional_parameter: $ => seq(
      field('name', $.identifier), '=', field('value', $._expression),
    ),
    splat_parameter: $ => seq('*', field('name', $.identifier)),
    hash_splat_parameter: $ => seq('**', field('name', $.identifier)),
    block_parameter: $ => seq('&', field('name', $.identifier)),
    destructured_parameter: $ => seq('(', $.identifier, repeat(seq(',', $.identifier)), ')'),

    lambda: $ => prec.right(10, seq(
      '->', optional(field('parameters', $.lambda_parameters)),
      field('body', $.block),
    )),
    lambda_parameters: $ => seq(
      '(', optional(seq($.identifier, repeat(seq(',', $.identifier)))), ')',
    ),

    block: $ => seq('{', optional(field('body', $.block_body)), '}'),
    do_block: $ => seq(
      'do', optional(field('parameters', $.block_parameters)), $.newline,
      optional(field('body', $.body_statement)), 'end',
    ),
    block_parameters: $ => seq(
      '|', optional(seq($.identifier, repeat(seq(',', $.identifier)))), '|',
    ),

    alias: $ => seq('alias', $.identifier, $.identifier),

    assignment: $ => prec.right(9, seq(
      field('left', choice(
        $.identifier, $.instance_variable, $.class_variable,
        $.left_assignment_list, $.rest_assignment, $.destructured_left_assignment,
      )),
      '=', field('right', choice($.call, $._expression)),
    )),

    operator_assignment: $ => prec.right(9, seq(
      field('left', choice($.identifier, $.instance_variable, $.class_variable)),
      choice('+=', '-=', '*=', '/=', '||=', '&&='),
      field('right', choice($.call, $._expression)),
    )),

    left_assignment_list: $ => seq($.identifier, ',', $.identifier),
    rest_assignment: $ => seq('*', $.identifier),
    destructured_left_assignment: $ => seq('(', $.identifier, ',', $.identifier, ')'),

    expression_statement: $ => choice($.call, $._statement_expression),

    _statement_expression: $ => choice(
      $.conditional,
      $.lambda,
      $.element_reference,
      $.array,
      $.string_array,
      $.symbol_array,
      $.string,
      $.subshell,
      $.regex,
      $.delimited_symbol,
      $.simple_symbol,
      $.integer,
      $.float,
      $.nil,
      $.true,
      $.false,
      $.self,
      $.super,
      $.file,
      $.line,
      $.encoding,
      $.instance_variable,
      $.class_variable,
    ),

    _expression: $ => choice(
      $.conditional,
      $.lambda,
      $.element_reference,
      $.array,
      $.string_array,
      $.symbol_array,
      $.string,
      $.subshell,
      $.regex,
      $.delimited_symbol,
      $.simple_symbol,
      $.integer,
      $.float,
      $.nil,
      $.true,
      $.false,
      $.self,
      $.super,
      $.file,
      $.line,
      $.encoding,
      $.instance_variable,
      $.class_variable,
      $.constant,
      $.identifier,
    ),

    conditional: $ => prec.right(2, seq(
      field('condition', choice($.call, $._expression_atom)), '?',
      field('consequence', choice($.call, $._expression)), ':',
      field('alternative', choice($.call, $._expression)),
    )),

    _expression_atom: $ => choice(
      $.call, $.element_reference, $.string, $.integer, $.float,
      $.nil, $.true, $.false, $.instance_variable, $.class_variable,
      $.constant, $.identifier,
    ),

    call: $ => choice(
      prec.right(12, seq(field('receiver', choice(
        $.element_reference, $.instance_variable, $.class_variable,
        $.constant, $.identifier, $.self,
      )), '.', field('method', choice($.identifier, $.constant)),
      optional(field('arguments', $.argument_list)), field('block', $.do_block))),
      prec(11, seq(field('receiver', choice(
        $.element_reference, $.instance_variable, $.class_variable,
        $.constant, $.identifier, $.self,
      )), '.', field('method', choice($.identifier, $.constant)),
      optional(field('arguments', $.argument_list)))),
      prec.right(10, seq(field('method', choice($.identifier, $.constant)),
        field('arguments', $.argument_list), field('block', $.do_block))),
      prec(9, seq(field('method', choice($.identifier, $.constant)),
        field('arguments', $.argument_list))),
      prec.right(8, seq(field('method', choice($.identifier, $.constant)),
        field('arguments', $.bare_argument_list), field('block', $.do_block))),
      prec(7, seq(field('method', choice($.identifier, $.constant)),
        field('arguments', $.bare_argument_list))),
      prec.right(6, seq(field('method', choice($.identifier, $.constant)),
        field('block', $.do_block))),
      prec(-1, field('method', choice($.identifier, $.constant))),
    ),

    argument_list: $ => seq(
      '(', optional(seq($._expression, repeat(seq(',', $._expression)))), ')',
    ),
    bare_argument_list: $ => prec.right(1, repeat1(choice(
      $.string, $.subshell, $.regex, $.array, $.simple_symbol,
      $.integer, $.float, $.nil, $.true, $.false,
      $.instance_variable, $.class_variable, $.constant, $.identifier,
    ))),

    element_reference: $ => prec(9, seq(
      field('object', choice($.identifier, $.instance_variable, $.class_variable, $.constant)),
      '[', $._expression, ']')),

    array: $ => seq(
      '[', optional(seq($._expression, repeat(seq(',', $._expression)))), ']'),

    string: $ => seq('"', repeat(choice(
      $.interpolation, $.escape_sequence, $.string_content,
    )), '"'),
    interpolation: $ => seq('#{', repeat(choice(
      $.instance_variable, $.class_variable, $.constant, $.identifier,
      $.integer, $.float, ',', '.',
    )), '}'),
    escape_sequence: $ => token.immediate(seq('\\', /./)),
    string_content: $ => token.immediate(prec(1, /[^"\\#\r\n]+/)),

    string_array: $ => seq('%w(', repeat($.bare_string), ')'),
    bare_string: $ => /[^\s)]+/,
    symbol_array: $ => seq('%i(', repeat($.bare_symbol), ')'),
    bare_symbol: $ => /[a-zA-Z_][a-zA-Z0-9_]*/,

    simple_symbol: $ => token(seq(':', /[a-zA-Z_][a-zA-Z0-9_]*/)),
    delimited_symbol: $ => seq(':', $.string),
    hash_key_symbol: $ => seq($.identifier, ':'),

    regex: $ => seq('/', optional(token.immediate(/[^/\r\n]+/)), '/',
      optional(token.immediate(/[a-z]+/))),
    subshell: $ => seq('`', optional(token.immediate(/[^`\r\n]+/)), '`'),

    integer: $ => /[0-9]+/,
    float: $ => /[0-9]+\.[0-9]+/,
    nil: $ => 'nil',
    true: $ => 'true',
    false: $ => 'false',
    self: $ => 'self',
    super: $ => 'super',
    file: $ => '__FILE__',
    line: $ => '__LINE__',
    encoding: $ => '__ENCODING__',

    class_variable: $ => /@@[a-zA-Z_][a-zA-Z0-9_]*/,
    instance_variable: $ => /@[a-zA-Z_][a-zA-Z0-9_]*/,
    constant: $ => /[A-Z][a-zA-Z0-9_]*/,
    identifier: $ => /[a-z_][a-zA-Z0-9_]*[!?]?/,

    hash_splat_nil: $ => seq('**', $.nil),
    heredoc_beginning: $ => '<<__ROTIDE_HEREDOC__',
    heredoc_body: $ => '__ROTIDE_HEREDOC_BODY__',

    keyword: $ => choice(
      'and', 'begin', 'break', 'case', 'do', 'else', 'elsif', 'ensure',
      'for', 'if', 'in', 'next', 'or', 'rescue', 'retry', 'return', 'then',
      'unless', 'until', 'when', 'while', 'yield', 'defined?',
    ),

    compatibility_marker: $ => choice(
      seq('__rotide_block_parameter__', $.block_parameter),
      seq('__rotide_destructured_parameter__', $.destructured_parameter),
      seq('__rotide_hash_splat_nil__', $.hash_splat_nil),
      seq('__rotide_hash_key_symbol__', $.hash_key_symbol),
      seq('__rotide_heredoc__', $.heredoc_beginning, $.heredoc_body),
      seq('__rotide_left_assignment_list__', $.left_assignment_list),
      seq('__rotide_optional_parameter__', $.optional_parameter),
      seq('__rotide_rest_assignment__', $.rest_assignment),
      seq('__rotide_rocket__', '=>'),
      seq('__rotide_semicolon__', ';'),
    ),
  },
});
