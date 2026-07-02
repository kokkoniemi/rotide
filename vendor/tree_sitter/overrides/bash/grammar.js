/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

module.exports = grammar({
  name: 'bash',

  extras: $ => [/\s/, /\\\r?\n/, $.comment],

  word: $ => $.word,

  conflicts: $ => [
    [$._statement, $.command],
    [$._simple_statement, $.command],
  ],

  externals: $ => [
    $.heredoc_start,
    $.simple_heredoc_body,
    $._heredoc_body_beginning,
    $.heredoc_content,
    $.heredoc_end,
    $.file_descriptor,
    $._empty_value,
    $._concat,
    $._external_variable_name,
    $.test_operator,
    $.regex,
    $._regex_no_slash,
    $._regex_no_space,
    $._expansion_word,
    $.extglob_pattern,
    $._bare_dollar,
    $._brace_start,
    $._immediate_double_hash,
    $._external_expansion_sym_hash,
    $._external_expansion_sym_bang,
    $._external_expansion_sym_equal,
    '}',
    ']',
    '<<',
    '<<-',
    /\n/,
    '(',
    'esac',
    $.__error_recovery,
  ],

  rules: {
    program: $ => optional($._statements),

    _statements: $ => prec(1, seq(
      repeat(seq($._statement, $._terminator)),
      $._statement,
      optional($._terminator),
    )),
    _terminated_statements: $ => repeat1(seq($._statement, $._terminator)),
    _terminator: _ => choice(';', /\r?\n/),

    _statement: $ => choice(
      $.function_definition,
      $.if_statement,
      $.for_statement,
      $.while_statement,
      $.case_statement,
      $.compound_statement,
      $.list,
      $.pipeline,
      $.declaration_command,
      $.test_command,
      $.redirected_statement,
      $.variable_assignment,
      $.command,
    ),

    function_definition: $ => prec.right(12, seq(
      choice(
        seq('function', field('name', $.word), optional(seq('(', ')'))),
        seq(field('name', $.word), '(', ')'),
      ),
      field('body', choice($.compound_statement, $.subshell)),
    )),

    compound_statement: $ => seq(
      '{',
      optional($._statements),
      '}',
    ),

    subshell: $ => seq('(', optional($._statements), ')'),

    if_statement: $ => prec.right(11, seq(
      'if', $._terminated_statements,
      'then', optional($._statements),
      repeat(seq(
        'elif', $._terminated_statements,
        'then', optional($._statements),
      )),
      optional(seq('else', optional($._statements))),
      'fi',
    )),

    for_statement: $ => prec.right(11, seq(
      choice('for', 'select'),
      alias($.word, $.variable_name),
      optional(seq('in', repeat($._literal))),
      repeat1($._terminator),
      'do', optional($._statements), 'done',
    )),

    while_statement: $ => prec.right(11, seq(
      choice('while', 'until'),
      $._terminated_statements,
      'do', optional($._statements), 'done',
    )),

    case_statement: $ => prec.right(11, seq(
      'case', $._literal, 'in', repeat($._terminator),
      repeat($.case_item),
      'esac',
    )),

    case_item: $ => seq(
      repeat1(choice($._literal, '|')),
      ')',
      optional($._statements),
      choice(';;', ';&', ';;&'),
      repeat($._terminator),
    ),

    list: $ => prec.left(2, seq(
      choice($.pipeline, $._simple_statement),
      repeat1(seq(choice('&&', '||'), choice($.pipeline, $._simple_statement))),
    )),

    pipeline: $ => prec.left(3, seq(
      $._simple_statement,
      repeat1(seq(choice('|', '|&'), $._simple_statement)),
    )),

    _simple_statement: $ => choice(
      $.declaration_command,
      $.test_command,
      $.redirected_statement,
      $.variable_assignment,
      $.command,
      $.subshell,
    ),

    declaration_command: $ => prec.left(5, choice(
      seq(
        choice('export', 'declare', 'typeset', 'readonly', 'local'),
        repeat(choice($.variable_assignment, $._literal)),
      ),
      seq('unset', repeat(alias($.word, $.variable_name))),
    )),

    test_command: $ => seq(
      choice('[', '[['),
      repeat($._literal),
      choice(']', ']]'),
    ),

    command: $ => prec.left(4, seq(
      repeat($.variable_assignment),
      field('name', $.command_name),
      repeat(choice(
        field('argument', $._literal),
        field('redirect', $._redirect),
      )),
    )),

    command_name: $ => choice(
      seq($.word, repeat1(choice(
        alias(token.immediate(/'[^']*'/), $.raw_string),
        alias(token.immediate(/"([^"$\\]|\\.)*"/), $.string),
        alias(token.immediate(/\$'([^']|\\')*'/), $.ansi_c_string),
      ))),
      $.string,
      $.raw_string,
      $.ansi_c_string,
      $.expansion,
      $.simple_expansion,
      $.command_substitution,
      $.process_substitution,
      $.word,
    ),

    redirected_statement: $ => prec.left(6, seq(
      choice($.command, $.variable_assignment),
      repeat1(field('redirect', $._redirect)),
    )),

    variable_assignment: $ => prec.right(7, seq(
      field('name', alias($.word, $.variable_name)),
      choice('=', '+='),
      field('value', optional(choice($._literal, $.array))),
    )),

    array: $ => seq('(', repeat($._literal), ')'),

    _redirect: $ => choice(
      $.file_redirect,
      $.heredoc_redirect,
      $.incomplete_heredoc_redirect,
      $.herestring_redirect,
    ),

    file_redirect: $ => seq(
      field('descriptor', optional($.file_descriptor)),
      choice('<', '>', '>>', '&>', '&>>', '<&', '>&', '>|'),
      field('destination', $._literal),
    ),

    herestring_redirect: $ => seq(
      field('descriptor', optional($.file_descriptor)),
      '<<<', $._literal,
    ),

    heredoc_redirect: $ => seq(
      field('descriptor', optional($.file_descriptor)),
      choice('<<', '<<-'),
      $.heredoc_start,
      /\n/,
      choice($._heredoc_body, $._simple_heredoc_body),
    ),

    incomplete_heredoc_redirect: $ => prec.right(-1, seq(
      field('descriptor', optional($.file_descriptor)),
      choice('<<', '<<-'),
      $.heredoc_start,
      /\n/,
      repeat(choice(
        $._incomplete_expansion,
        $.simple_expansion,
        $.command_substitution,
        $.heredoc_content,
      )),
    )),

    _heredoc_body: $ => seq($.heredoc_body, $.heredoc_end),

    heredoc_body: $ => seq(
      $._heredoc_body_beginning,
      repeat(choice(
        $.expansion,
        $.simple_expansion,
        $.command_substitution,
        $.heredoc_content,
      )),
    ),

    _simple_heredoc_body: $ => seq(
      alias($.simple_heredoc_body, $.heredoc_body),
      $.heredoc_end,
    ),

    _literal: $ => choice(
      $.string,
      alias($.incomplete_string, $.string),
      $.raw_string,
      $.ansi_c_string,
      $.expansion,
      $._incomplete_expansion,
      $.simple_expansion,
      $.command_substitution,
      $.process_substitution,
      $.word,
    ),

    string: $ => prec.right(seq(
      '"',
      repeat(choice(
        $.string_content,
        $.expansion,
        $._incomplete_expansion,
        $.simple_expansion,
        $.command_substitution,
      )),
      '"',
    )),

    incomplete_string: $ => prec.right(-1, seq(
      '"',
      repeat1(choice(
        $.string_content,
        $.expansion,
        $._incomplete_expansion,
        $.simple_expansion,
        $.command_substitution,
      )),
    )),

    string_content: _ => token.immediate(prec(-1, /([^"`$\\\r\n]|\\(.|\r?\n))+/)),

    raw_string: _ => /'[^']*'/,
    ansi_c_string: _ => /\$'([^']|\\')*'/,

    simple_expansion: $ => seq(
      '$',
      alias(token.immediate(/[A-Za-z_][A-Za-z0-9_]*|[0-9@*#?$!_-]/), $.variable_name),
    ),

    expansion: $ => prec.right(seq(
      '${',
      repeat(choice('#', '!')),
      alias(
        token.immediate(/[A-Za-z_][A-Za-z0-9_]*|[0-9@*?$!-]/),
        $.variable_name,
      ),
      repeat(choice(
        $.string,
        $.raw_string,
        $.command_substitution,
        token.immediate(/[^}$'"`]+/),
      )),
      '}',
    )),

    _incomplete_expansion: $ => prec.right(-1, seq(
      '${',
      repeat(choice('#', '!')),
      alias(
        token.immediate(/[A-Za-z_][A-Za-z0-9_]*|[0-9@*?$!-]/),
        $.variable_name,
      ),
      repeat(choice(
        $.string,
        $.raw_string,
        $.command_substitution,
        token.immediate(/[^}$'"`]+/),
      )),
    )),

    command_substitution: $ => prec.right(choice(
      seq('$(', optional($._statements), ')'),
      seq('`', optional($._statements), '`'),
    )),

    process_substitution: $ => seq(
      choice('<(', '>('),
      optional($._statements),
      ')',
    ),

    word: _ => token(prec(-1, /([^\s'"<>\[\]{}()\\`$|&;#=]|\\.)+/)),

    comment: _ => token(prec(-10, /#.*/)),
  },
});
