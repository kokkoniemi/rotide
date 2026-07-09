/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

const KEYWORDS = [
  'package', 'import', 'class', 'interface', 'object', 'enum', 'annotation',
  'typealias', 'companion', 'constructor', 'init', 'fun', 'val', 'var', 'this',
  'super', 'return', 'throw', 'as', 'is', 'in', 'out', 'by',
  'field', 'get', 'set',
  'if', 'else', 'when', 'where',
  'for', 'while', 'do',
  'try', 'catch', 'finally',
];

const MODIFIERS = [
  'public', 'private', 'internal', 'protected',
  'abstract', 'final', 'open', 'override', 'sealed',
  'data', 'inner', 'inline', 'noinline', 'crossinline', 'vararg', 'lateinit',
  'const', 'operator', 'infix', 'suspend', 'tailrec',
  'external', 'expect', 'actual', 'value',
];

const OPERATORS = [
  '!', '!=', '!==',
  '%', '%=',
  '&&',
  '*', '*=',
  '+', '++', '+=',
  '-', '--', '-=', '->',
  '/', '/=',
  '<', '<=',
  '=', '==', '===',
  '>', '>=',
  '?:', '?.', '?',
  '..',
  '||',
];

module.exports = grammar({
  name: 'kotlin',

  extras: $ => [/\s/, $.line_comment, $.block_comment],

  word: $ => $.identifier,

  conflicts: $ => [],

  rules: {
    source_file: $ => repeat($._item),

    _item: $ => choice(
      $.class_declaration,
      $.object_declaration,
      $.function_declaration,
      $._expression,
      $._punctuation_token,
      ...KEYWORDS.filter(k =>
        k !== 'class' && k !== 'interface' && k !== 'enum' &&
        k !== 'object' && k !== 'fun'),
      ...MODIFIERS,
    ),

    line_comment: $ => token(seq('//', /[^\r\n]*/)),
    block_comment: $ => token(seq('/*', repeat(choice(/[^*]+/, /\*[^/]/)), optional('*/'))),

    class_declaration: $ => prec.right(seq(
      choice('class', 'interface', 'enum'),
      field('name', $.identifier),
      optional($._decl_tail),
    )),

    object_declaration: $ => prec.right(seq(
      'object',
      field('name', $.identifier),
      optional($._decl_tail),
    )),

    function_declaration: $ => prec.right(seq(
      'fun',
      field('name', $.identifier),
      optional($._decl_tail),
    )),

    _decl_tail: $ => prec.right(repeat1(choice(
      $.block,
      $._expression,
      $._punctuation_token,
      ...KEYWORDS.filter(k =>
        k !== 'class' && k !== 'interface' && k !== 'enum' &&
        k !== 'object' && k !== 'fun'),
      ...MODIFIERS,
    ))),

    block: $ => seq('{', repeat($._item), '}'),

    _expression: $ => choice(
      $.call_expression,
      $.user_type,
      $.number_literal,
      $.float_literal,
      $.character_literal,
      $.string_literal,
      $.multiline_string_literal,
      $.identifier,
    ),

    call_expression: $ => prec.left(1, seq($.identifier, $.arguments)),

    arguments: $ => seq('(', optional(seq($._expression, repeat(seq(',', $._expression)))), ')'),

    user_type: $ => seq('__rotide_type__', $.identifier),

    _punctuation_token: $ => choice(
      ...OPERATORS,
      '(', ')', '[', ']',
      ',', ';', '.', '::',
      '@', ':',
    ),

    number_literal: $ => token(choice(
      /0[xX][0-9a-fA-F][0-9a-fA-F_]*[uUlL]*/,
      /0[bB][01][01_]*[uUlL]*/,
      /\d[\d_]*[uUlL]*/,
    )),

    float_literal: $ => token(choice(
      /\d[\d_]*\.\d[\d_]*([eE][+-]?\d+)?[fFdD]?/,
      /\d[\d_]*[eE][+-]?\d+[fFdD]?/,
      /\d[\d_]*[fF]/,
    )),

    character_literal: $ => seq(
      "'",
      choice($.escape_sequence, /[^'\\\n]/),
      "'",
    ),

    escape_sequence: $ => token.immediate(choice(
      /\\[btnr'"\\$]/,
      /\\u[0-9a-fA-F]{4}/,
    )),

    string_literal: $ => seq(
      '"',
      repeat(choice($.escape_sequence, $.string_content)),
      '"',
    ),

    multiline_string_literal: $ => seq(
      '"""',
      repeat(choice($.escape_sequence, alias($.multiline_string_content, $.string_content))),
      '"""',
    ),

    string_content: $ => token.immediate(prec(1, /[^"\\\n]+/)),
    multiline_string_content: $ => token.immediate(prec(1, /[^"\\]+|"[^"]|""[^"]/)),

    identifier: $ => /[A-Za-z_][A-Za-z0-9_]*/,
  },
});
