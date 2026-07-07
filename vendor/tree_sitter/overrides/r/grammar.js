/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

// ---------------------------------------------------------------------------------------
// RotIDE reduced R highlight grammar.
//
// This replaces the pinned r-lib/tree-sitter-r grammar before generation. RotIDE consumes
// R trees only through the vendored highlight and locals queries, so this override keeps
// exactly the nodes, fields, operators, literals, and error recovery those queries target
// while dropping machinery that only matters for a faithful R AST.
//
// The two big size reductions versus upstream:
//
//   1. No external scanner. Upstream ships a 16-token external scanner that makes newlines
//      and `;` statement separators, matches raw strings, and emits bracket tokens so the
//      parser can consume newlines inside `(`/`[`. Highlighting does not depend on any of
//      that, so here newlines and semicolons are plain whitespace/separators, brackets are
//      literals, and raw strings are a single regex token. Refresh removes the upstream
//      `scanner.c` because this grammar declares no externals.
//
//   2. The operator precedence table is preserved so real R still parses without spurious
//      ERROR nodes (which would wreck highlighting), but all the `repeat($._newline)`
//      states scattered through the upstream rules are gone.
//
// Because newlines are no longer significant, this grammar is intentionally more lenient
// than R: it happily parses across line breaks. That only ever produces a *more* connected
// tree, never a worse-highlighted one.
// ---------------------------------------------------------------------------------------

// Higher RANK binds tighter. Mirrors R's operator precedence closely enough that ordinary
// scripts parse into the same node/field shapes the queries expect.
const PREC = {
  HELP: { ASSOC: prec.left, RANK: 1 },
  FUNCTION_OR_LOOP: { ASSOC: prec.left, RANK: 2 },
  IF: { ASSOC: prec.right, RANK: 3 },
  LEFT_ASSIGN: { ASSOC: prec.right, RANK: 4 },
  EQUALS_ASSIGN: { ASSOC: prec.right, RANK: 5 },
  RIGHT_ASSIGN: { ASSOC: prec.left, RANK: 6 },
  TILDE: { ASSOC: prec.left, RANK: 7 },
  OR: { ASSOC: prec.left, RANK: 8 },
  AND: { ASSOC: prec.left, RANK: 9 },
  UNARY_NOT: { ASSOC: prec.left, RANK: 10 },
  COMPARISON: { ASSOC: prec.left, RANK: 11 },
  PLUS_MINUS: { ASSOC: prec.left, RANK: 12 },
  MULTIPLY_DIVIDE: { ASSOC: prec.left, RANK: 13 },
  SPECIAL_OR_PIPE: { ASSOC: prec.left, RANK: 14 },
  COLON: { ASSOC: prec.left, RANK: 15 },
  UNARY_PLUS_MINUS: { ASSOC: prec.left, RANK: 16 },
  EXPONENTIATE: { ASSOC: prec.right, RANK: 17 },
  EXTRACT: { ASSOC: prec.right, RANK: 18 },
  NAMESPACE: { ASSOC: prec.right, RANK: 19 },
  CALL: { ASSOC: prec.right, RANK: 20 },
};

module.exports = grammar({
  name: 'r',

  // Newlines carry no meaning in this reduced grammar, so they are ordinary whitespace.
  extras: $ => [$.comment, /\s/],

  inline: $ => [$._identifier, $._string_or_identifier],

  word: $ => $.identifier,

  rules: {
    // `;` is an accepted-but-ignored statement separator.
    program: $ => repeat(choice($._expression, ';')),

    function_definition: $ => withPrec(PREC.FUNCTION_OR_LOOP, seq(
      field('name', choice('\\', 'function')),
      field('parameters', $.parameters),
      field('body', $._expression),
    )),

    parameters: $ => seq(
      field('open', '('),
      optional(seq(
        field('parameter', $.parameter),
        repeat(seq($.comma, field('parameter', $.parameter))),
      )),
      field('close', ')'),
    ),

    parameter: $ => choice(
      $._parameter_with_default,
      $._parameter_without_default,
    ),

    _parameter_with_default: $ => seq(
      $._parameter_name,
      '=',
      field('default', $._expression),
    ),
    _parameter_without_default: $ => $._parameter_name,
    _parameter_name: $ => field('name', $._identifier),

    if_statement: $ => withPrec(PREC.IF, seq(
      'if',
      field('open', '('),
      field('condition', $._expression),
      field('close', ')'),
      field('consequence', $._expression),
      optional(seq(
        'else',
        field('alternative', $._expression),
      )),
    )),

    for_statement: $ => withPrec(PREC.FUNCTION_OR_LOOP, seq(
      'for',
      field('open', '('),
      field('variable', $._identifier),
      'in',
      field('sequence', $._expression),
      field('close', ')'),
      field('body', $._expression),
    )),

    while_statement: $ => withPrec(PREC.FUNCTION_OR_LOOP, seq(
      'while',
      field('open', '('),
      field('condition', $._expression),
      field('close', ')'),
      field('body', $._expression),
    )),

    repeat_statement: $ => withPrec(PREC.FUNCTION_OR_LOOP, seq(
      'repeat',
      field('body', $._expression),
    )),

    braced_expression: $ => prec(0, seq(
      field('open', '{'),
      repeat(choice(field('body', $._expression), ';')),
      field('close', '}'),
    )),

    parenthesized_expression: $ => prec(0, seq(
      field('open', '('),
      field('body', $._expression),
      field('close', ')'),
    )),

    call: $ => withPrec(PREC.CALL, seq(
      field('function', $._expression),
      field('arguments', alias($.call_arguments, $.arguments)),
    )),

    subset: $ => withPrec(PREC.CALL, seq(
      field('function', $._expression),
      field('arguments', alias($.subset_arguments, $.arguments)),
    )),

    subset2: $ => withPrec(PREC.CALL, seq(
      field('function', $._expression),
      field('arguments', alias($.subset2_arguments, $.arguments)),
    )),

    call_arguments: $ => seq(
      field('open', '('),
      delimSep1(optional(field('argument', $.argument)), $.comma),
      field('close', ')'),
    ),
    subset_arguments: $ => seq(
      field('open', '['),
      delimSep1(optional(field('argument', $.argument)), $.comma),
      field('close', ']'),
    ),
    subset2_arguments: $ => seq(
      field('open', '[['),
      delimSep1(optional(field('argument', $.argument)), $.comma),
      field('close', ']]'),
    ),

    argument: $ => choice(
      $._argument_named,
      $._argument_unnamed,
    ),
    _argument_named: $ => seq(
      field('name', $._argument_name_string_or_identifier_or_null),
      '=',
      optional($._argument_value),
    ),
    _argument_unnamed: $ => $._argument_value,
    _argument_value: $ => field('value', $._expression),

    unary_operator: $ => {
      const table = [
        ['?', PREC.HELP],
        ['~', PREC.TILDE],
        ['!', PREC.UNARY_NOT],
        ['+', PREC.UNARY_PLUS_MINUS],
        ['-', PREC.UNARY_PLUS_MINUS],
      ];
      return choice(...table.map(([operator, p]) => p.ASSOC(p.RANK, seq(
        field('operator', operator),
        field('rhs', $._expression),
      ))));
    },

    binary_operator: $ => {
      const table = [
        ['?', PREC.HELP],
        ['~', PREC.TILDE],
        ['<-', PREC.LEFT_ASSIGN],
        ['<<-', PREC.LEFT_ASSIGN],
        [':=', PREC.LEFT_ASSIGN],
        ['->', PREC.RIGHT_ASSIGN],
        ['->>', PREC.RIGHT_ASSIGN],
        ['=', PREC.EQUALS_ASSIGN],
        ['|', PREC.OR],
        ['&', PREC.AND],
        ['||', PREC.OR],
        ['&&', PREC.AND],
        ['<', PREC.COMPARISON],
        ['<=', PREC.COMPARISON],
        ['>', PREC.COMPARISON],
        ['>=', PREC.COMPARISON],
        ['==', PREC.COMPARISON],
        ['!=', PREC.COMPARISON],
        ['+', PREC.PLUS_MINUS],
        ['-', PREC.PLUS_MINUS],
        ['*', PREC.MULTIPLY_DIVIDE],
        ['/', PREC.MULTIPLY_DIVIDE],
        ['**', PREC.EXPONENTIATE],
        ['^', PREC.EXPONENTIATE],
        [alias(/%[^%\\\n]*%/, 'special'), PREC.SPECIAL_OR_PIPE],
        ['|>', PREC.SPECIAL_OR_PIPE],
        [':', PREC.COLON],
      ];
      return choice(...table.map(([operator, p]) => p.ASSOC(p.RANK, seq(
        field('lhs', $._expression),
        field('operator', operator),
        field('rhs', $._expression),
      ))));
    },

    extract_operator: $ => {
      const table = [
        ['$', PREC.EXTRACT],
        ['@', PREC.EXTRACT],
      ];
      return choice(...table.map(([operator, p]) => p.ASSOC(p.RANK, seq(
        field('lhs', $._expression),
        field('operator', operator),
        optional(field('rhs', $._string_or_identifier)),
      ))));
    },

    namespace_operator: $ => {
      const table = [
        ['::', PREC.NAMESPACE],
        [':::', PREC.NAMESPACE],
      ];
      return choice(...table.map(([operator, p]) => p.ASSOC(p.RANK, seq(
        field('lhs', $._string_or_identifier),
        field('operator', operator),
        optional(field('rhs', $._string_or_identifier)),
      ))));
    },

    // Numeric literals.
    integer: $ => seq($._float_literal, 'L'),
    complex: $ => seq($._float_literal, 'i'),
    float: $ => $._float_literal,
    _hex_literal: $ => /0[xX](([0-9a-fA-F]+(\.[0-9a-fA-F]*)?)|(\.[0-9a-fA-F]*))([pP][+-]?[0-9]+)?/,
    _number_literal: $ => /(?:(?:\d+(?:\.\d*)?)|(?:\.\d+))(?:[eE][+-]?\d*)?/,
    _float_literal: $ => choice($._hex_literal, $._number_literal),

    // Strings. Raw strings (`r"(...)"`) become a single-line regex token; the quoted forms
    // keep their string_content/escape_sequence structure so `@string.escape` still fires.
    string: $ => choice(
      $._raw_string,
      seq(
        field('open', alias('"', $.string_open)),
        optional(field('content', alias($._double_quoted_string_content, $.string_content))),
        field('close', alias('"', $.string_close)),
      ),
      seq(
        field('open', alias('\'', $.string_open)),
        optional(field('content', alias($._single_quoted_string_content, $.string_content))),
        field('close', alias('\'', $.string_close)),
      ),
    ),

    _raw_string: $ => token(/[rR]"[-]*[(\[{].*[)\]}][-]*"/),

    _single_quoted_string_content: $ => repeat1(choice(/[^'\\]+/, $.escape_sequence)),
    _double_quoted_string_content: $ => repeat1(choice(/[^"\\]+/, $.escape_sequence)),

    escape_sequence: $ => token.immediate(seq(
      '\\',
      choice(
        /[^0-9xuU]/,
        /[0-7]{1,3}/,
        /x[0-9a-fA-F]{1,2}/,
        /u[0-9a-fA-F]{1,4}/,
        /u\{[0-9a-fA-F]{1,4}\}/,
        /U[0-9a-fA-F]{1,8}/,
        /U\{[0-9a-fA-F]{1,8}\}/,
      ),
    )),

    dots: $ => '...',
    dot_dot_i: $ => /[.][.]\d+/,

    identifier: $ => {
      const _unquoted_identifier = /[\p{XID_Start}_][\p{XID_Continue}.]*/;
      const _unquoted_identifier_with_leading_dot = /\.(?:[\p{XID_Start}._][\p{XID_Continue}.]*)?/;
      const _quoted_identifier = /`((?:\\(.|\n))|[^`\\])*`/;
      return token(choice(
        _unquoted_identifier,
        _unquoted_identifier_with_leading_dot,
        _quoted_identifier,
      ));
    },

    _identifier: $ => choice($.dots, $.dot_dot_i, $.identifier),
    _string_or_identifier: $ => choice($.string, $._identifier),
    _argument_name_string_or_identifier_or_null: $ => prec(1, choice(
      $._string_or_identifier,
      $.null,
    )),

    // Keywords / reserved constants.
    next: $ => 'next',
    break: $ => 'break',
    true: $ => 'TRUE',
    false: $ => 'FALSE',
    null: $ => 'NULL',
    inf: $ => 'Inf',
    nan: $ => 'NaN',
    na: $ => choice('NA', 'NA_integer_', 'NA_real_', 'NA_complex_', 'NA_character_'),

    _expression: $ => choice(
      $.function_definition,
      $.if_statement,
      $.for_statement,
      $.while_statement,
      $.repeat_statement,
      $.braced_expression,
      $.parenthesized_expression,
      $.call,
      $.subset,
      $.subset2,
      $.unary_operator,
      $.binary_operator,
      $.extract_operator,
      $.namespace_operator,
      $.integer,
      $.complex,
      $.float,
      $.string,
      $.identifier,
      $.dots,
      $.dot_dot_i,
      $.next,
      $.break,
      $.true,
      $.false,
      $.null,
      $.inf,
      $.nan,
      $.na,
    ),

    comment: $ => token(prec(-1, seq('#', /[^\r\n]*/))),

    comma: $ => ',',
  },
});

function withPrec(p, rule) {
  return p.ASSOC(p.RANK, rule);
}

function delimSep1(rule, delim) {
  return seq(rule, repeat(seq(delim, rule)));
}
