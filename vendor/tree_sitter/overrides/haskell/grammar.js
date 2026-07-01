/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

const sep = /[;\r\n]+/;

module.exports = grammar({
  name: 'haskell',

  extras: $ => [/[ \t\f]/, $.comment, $.pragma],
  word: $ => $.variable,

  supertypes: $ => [
    $.expression,
    $.pattern,
  ],

  rules: {
    haskell: $ => repeat($._item),

    _item: $ => choice(
      $.module_header,
      $.import_declaration,
      $.data_declaration,
      $.newtype_declaration,
      $.type_declaration,
      $.class_declaration,
      $.instance_declaration,
      $.signature,
      $.function,
      $.assignment,
      $.expression,
      $.keyword,
      sep,
    ),

    comment: $ => token(choice(
      seq('--', /[^\r\n]*/),
      seq('{-', repeat(choice(/[^-]+/, /-[^}]/)), optional('-}')),
    )),

    pragma: $ => token(prec(1, seq(
      '{-#',
      repeat(choice(/[^#]+/, /#[^-]/, /#-[^}]/)),
      optional('#-}'),
    ))),

    module_header: $ => prec(20, seq(
      'module',
      $.module_name,
      optional($.export_list),
      'where',
    )),

    module_name: $ => seq($.module_part, repeat(seq('.', $.module_part))),
    module_part: $ => token(prec(3, /[A-Z][A-Za-z0-9_']*/)),
    export_list: $ => seq('(', optional(seq($.variable, repeat(seq(',', $.variable)))), ')'),

    import_declaration: $ => prec.right(20, seq(
      'import',
      optional('qualified'),
      $.module_name,
      optional(seq('as', $.module_name)),
      optional(seq('hiding', $.export_list)),
    )),

    data_declaration: $ => prec.right(18, seq(
      'data',
      $.name,
      repeat($.type_variable),
      optional(seq('=', choice($.name, $.constructor), optional($.record_fields),
        repeat(seq(choice('|', ','), choice($.name, $.constructor),
          optional($.record_fields))))),
      optional($.deriving_clause),
    )),

    newtype_declaration: $ => prec.right(18, seq(
      'newtype',
      $.name,
      repeat($.type_variable),
      optional(seq('=', choice($.name, $.constructor), optional($.record_fields))),
      optional($.deriving_clause),
    )),

    type_declaration: $ => prec.right(18, seq(
      'type',
      $.name,
      repeat($.type_variable),
      optional(seq('=', $._type)),
    )),

    class_declaration: $ => prec.right(17, seq(
      'class',
      $.name,
      optional(choice($.type_variable, $.name)),
      optional(seq('where', repeat($._item))),
    )),

    instance_declaration: $ => prec.right(17, seq(
      'instance',
      repeat(choice($.name, $.constructor, $.type_variable)),
      optional(seq('where', repeat($._item))),
    )),

    deriving_clause: $ => prec.right(seq(
      'deriving',
      repeat1(choice('stock', 'anyclass', 'via', $.name, $.constructor, $.type_variable,
        '(', ')', ',')),
    )),

    record_fields: $ => seq(
      '{',
      optional(seq($.field_declaration, repeat(seq(',', $.field_declaration)))),
      '}',
    ),

    field_declaration: $ => seq($.variable, '::', $._type),

    signature: $ => prec(16, seq(
      field('name', $.variable),
      '::',
      $._type,
    )),

    function: $ => prec.right(15, seq(
      field('name', $.variable),
      repeat($.pattern),
      '=',
      optional($.expression),
    )),

    assignment: $ => prec.right(14, seq(
      $.variable,
      '=',
      optional($.expression),
    )),

    expression: $ => choice(
      $.if_expression,
      $.let_expression,
      $.case_expression,
      $.do_expression,
      $.quasiquote,
      $.string,
      $.char,
      $.float,
      $.integer,
      $.constructor,
      $.name,
      $.variable,
    ),

    if_expression: $ => prec.right(12, seq(
      'if', $.expression, 'then', $.expression, 'else', $.expression,
    )),

    let_expression: $ => prec.right(12, seq(
      'let', repeat1(choice($.function, $.assignment, $.signature, sep)), 'in',
      optional($.expression),
    )),

    case_expression: $ => prec.right(12, seq(
      'case', $.expression, 'of', repeat1($.case_alternative),
    )),

    case_alternative: $ => seq(
      $.pattern,
      optional(seq('->', $.expression)),
    ),

    do_expression: $ => prec.right(12,
      seq('do', repeat1(choice($.function, $.assignment, $.expression, sep)))),

    pattern: $ => choice(
      $.constructor,
      $.name,
      $.variable,
      $.integer,
      $.char,
      $.tuple_pattern,
    ),

    tuple_pattern: $ => seq(
      '(',
      optional(seq($.pattern, repeat(seq(',', $.pattern)))),
      ')',
    ),

    _type: $ => repeat1(choice(
      $.name,
      $.constructor,
      $.type_variable,
      $.variable,
      'forall',
      'family',
      '->',
      '=>',
      '::',
      '.',
      '(',
      ')',
      '[',
      ']',
    )),

    quasiquote: $ => seq(
      '[',
      $.quoter,
      '|',
      $.quasiquote_body,
      '|]',
    ),

    quoter: $ => /[A-Za-z_][A-Za-z0-9_']*/,
    quasiquote_body: $ => token.immediate(/([^|]|\|[^\]])*/),

    keyword: $ => prec(-10, choice(
      'if', 'then', 'else', 'case', 'of', 'let', 'in', 'do', 'where',
      'module', 'import', 'qualified', 'as', 'hiding', 'class', 'instance',
      'data', 'newtype', 'type', 'family', 'deriving', 'via', 'stock',
      'anyclass', 'forall', 'infix', 'infixl', 'infixr', 'pattern', 'mdo',
      'rec',
    )),

    string: $ => token(seq('"', repeat(choice(/[^"\\\r\n]+/, /\\./)), '"')),
    char: $ => token(seq("'", choice(/[^'\\\r\n]/, /\\./), optional("'"))),
    float: $ => token(/[0-9]+\.[0-9]+([eE][+-]?[0-9]+)?/),
    integer: $ => token(/0[xX][0-9a-fA-F]+|[0-9]+/),
    constructor: $ => token(prec(2, choice('True', 'False'))),
    name: $ => token(prec(1, /[A-Z][A-Za-z0-9_']*/)),
    type_variable: $ => /[a-z][A-Za-z0-9_']*/,
    variable: $ => /[a-z_][A-Za-z0-9_']*/,
  },
});
