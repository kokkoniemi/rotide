/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

module.exports = grammar({
  name: 'c_sharp',

  extras: $ => [/\s/, $.comment],

  word: $ => $.identifier,

  conflicts: $ => [
    [$.method_declaration, $.local_function_statement],
    [$.interpolated_string_expression, $.incomplete_interpolated_string_expression],
  ],

  rules: {
    compilation_unit: $ => repeat($._item),

    _item: $ => choice(
      $.attribute,
      $.using_directive,
      $.namespace_declaration,
      $.interface_declaration,
      $.class_declaration,
      $.enum_declaration,
      $.struct_declaration,
      $.record_declaration,
      $.method_declaration,
      $.local_function_statement,
      $.constructor_declaration,
      $.destructor_declaration,
      $.field_declaration,
      $.return_statement,
      $.expression_statement,
      $.block,
      $.interpolation_quote_marker,
    ),

    comment: $ => token(choice(
      seq('//', /[^\r\n]*/),
      seq('/*', repeat(choice(/[^*]+/, /\*[^/]/)), '*/'),
    )),

    attribute: $ => prec(10, seq('[', field('name', $.identifier), ']')),

    using_directive: $ => prec(10, seq('using', repeat1(choice($.identifier, '.')), ';')),

    namespace_declaration: $ => prec(10, seq(
      'namespace',
      field('name', choice($.identifier, $.qualified_name)),
      choice(';', $.block),
    )),

    qualified_name: $ => seq($.identifier, repeat1(seq('.', $.identifier))),

    interface_declaration: $ => prec(10, seq(
      repeat($.modifier),
      'interface',
      field('name', $.identifier),
      optional($.type_parameter_list),
      optional($.base_list),
      $.block,
    )),

    class_declaration: $ => prec(10, seq(
      repeat($.modifier),
      'class',
      field('name', $.identifier),
      optional($.type_parameter_list),
      optional($.base_list),
      $.block,
    )),

    enum_declaration: $ => prec(10, seq(
      repeat($.modifier),
      'enum',
      field('name', $.identifier),
      '{',
      optional(seq($.enum_member_declaration,
        repeat(seq(',', $.enum_member_declaration)), optional(','))),
      '}',
    )),

    enum_member_declaration: $ => seq($.identifier, optional(seq('=', $._expression))),

    struct_declaration: $ => prec(10, seq(
      repeat($.modifier),
      'struct',
      $.identifier,
      optional($.type_parameter_list),
      optional($.base_list),
      $.block,
    )),

    record_declaration: $ => prec(10, seq(
      repeat($.modifier),
      'record',
      optional(choice('class', 'struct')),
      $.identifier,
      optional($.type_parameter_list),
      optional($.parameter_list),
      optional($.base_list),
      choice(';', $.block),
    )),

    base_list: $ => seq(':', $._type, repeat(seq(',', $._type))),

    block: $ => prec(9, seq('{', repeat($._item), '}')),

    method_declaration: $ => prec(8, seq(
      repeat($.modifier),
      field('type', $._type),
      field('name', $.identifier),
      optional($.type_parameter_list),
      $.parameter_list,
      repeat($.type_parameter_constraints_clause),
      choice(';', $.block),
    )),

    local_function_statement: $ => prec(8, seq(
      field('type', $._type),
      field('name', $.identifier),
      optional($.type_parameter_list),
      $.parameter_list,
      choice(';', $.block),
    )),

    constructor_declaration: $ => prec(8, seq(
      repeat($.modifier),
      field('name', $.identifier),
      $.parameter_list,
      $.block,
    )),

    destructor_declaration: $ => prec(8, seq(
      '~',
      field('name', $.identifier),
      $.parameter_list,
      $.block,
    )),

    field_declaration: $ => prec(8, seq(
      repeat($.modifier),
      field('type', $._type),
      $.identifier,
      optional(seq('=', $._expression)),
      ';',
    )),

    return_statement: $ => prec(7, seq('return', optional($._expression), ';')),

    expression_statement: $ => prec.right(seq($._expression, optional(';'))),

    parameter_list: $ => prec(2, seq(
      '(', optional(seq($.parameter, repeat(seq(',', $.parameter)))), ')',
    )),

    parameter: $ => seq(
      repeat($.modifier),
      field('type', $._type),
      field('name', $.identifier),
      optional(seq('=', $._expression)),
    ),

    type_parameter_list: $ => seq(
      '<',
      $.type_parameter,
      repeat(seq(',', $.type_parameter)),
      '>',
    ),

    type_parameter: $ => $.identifier,

    type_parameter_constraints_clause: $ => seq(
      'where',
      $.identifier,
      ':',
      choice($._type, 'notnull'),
      repeat(seq(',', choice($._type, 'notnull'))),
    ),

    _type: $ => prec(3, choice(
      $.predefined_type,
      $.implicit_type,
      $.generic_name,
      $.identifier,
    )),

    generic_name: $ => prec(4, seq($.identifier, $.type_argument_list)),

    type_argument_list: $ => seq('<', $._type, repeat(seq(',', $._type)), '>'),

    predefined_type: $ => choice(
      'bool', 'byte', 'char', 'decimal', 'double', 'float', 'int', 'long',
      'object', 'sbyte', 'short', 'string', 'uint', 'ulong', 'ushort', 'void',
    ),

    invocation_expression: $ => prec(6, seq(
      choice($.member_access_expression, $.identifier),
      $.argument_list,
    )),

    member_access_expression: $ => prec(5, seq(
      $.identifier,
      '.',
      field('name', $.identifier),
    )),

    argument_list: $ => prec(1, seq(
      '(', optional(seq($._expression, repeat(seq(',', $._expression)))), ')',
    )),

    as_expression: $ => prec(4, seq(
      $._expression_atom, 'as', field('right', $.identifier),
    )),

    is_expression: $ => prec.right(4, seq(
      $._expression_atom,
      'is',
      field('right', choice($.predefined_type, $.identifier)),
      optional($.identifier),
    )),

    _expression: $ => repeat1($._atom),

    _expression_atom: $ => choice(
      $.invocation_expression,
      $.member_access_expression,
      $.interpolated_string_expression,
      $.incomplete_interpolated_string_expression,
      $.raw_string_literal,
      $.verbatim_string_literal,
      $.string_literal,
      $.character_literal,
      $.real_literal,
      $.integer_literal,
      $.boolean_literal,
      $.null_literal,
      $.predefined_type,
      $.identifier,
      $.keyword,
      $.operator,
    ),

    _atom: $ => choice(
      $.as_expression,
      $.is_expression,
      $._expression_atom,
    ),

    escape_sequence: $ => token(seq('\\', /./)),

    character_literal: $ => prec.right(seq(
      "'", choice($.escape_sequence, /[^'\\\r\n]/), optional("'"),
    )),

    string_literal: $ => prec.right(seq(
      '"',
      repeat(choice($.escape_sequence, /[^"\\\r\n]+/)),
      optional('"'),
    )),

    verbatim_string_literal: $ => token(seq('@"', repeat(choice(/[^"]+/, '""')), optional('"'))),

    raw_string_literal: $ => token(seq('"""', repeat(choice(/[^"]+/, /"[^"]/)), optional('"""'))),

    interpolated_string_expression: $ => prec.right(seq(
      $.interpolation_start,
      '"',
      repeat(choice($.interpolated_string_text, $.interpolation)),
      '"',
    )),

    // Keep malformed interpolation captures aligned with the upstream error tree.
    incomplete_interpolated_string_expression: $ => seq(
      $.interpolation_start,
      '"',
      $.interpolated_string_text,
      alias('{', $.interpolation_brace),
      token.immediate(/[^"}\r\n]+/),
    ),

    interpolation_start: $ => '$',

    interpolation_quote: $ => '"',

    // Retain this upstream query node even though normal C# quotes are anonymous.
    interpolation_quote_marker: $ => seq(
      '__rotide_interpolation_quote__',
      $.interpolation_quote,
    ),

    interpolated_string_text: $ => /[^"{}]+/,

    interpolation: $ => prec.right(seq(
      alias('{', $.interpolation_brace),
      repeat1($._atom),
      alias('}', $.interpolation_brace),
    )),

    real_literal: $ => /[0-9]+\.[0-9]+[fFdDmM]?/,

    integer_literal: $ => /0[xX][0-9a-fA-F_]+|[0-9][0-9_]*/,

    boolean_literal: $ => choice('true', 'false'),

    null_literal: $ => 'null',

    identifier: $ => /@?[A-Za-z_][A-Za-z0-9_]*/,

    modifier: $ => choice(
      'public', 'private', 'protected', 'internal', 'abstract', 'async',
      'const', 'new', 'override', 'partial', 'readonly', 'required', 'sealed',
      'static', 'unsafe', 'virtual', 'volatile',
    ),

    implicit_type: $ => 'var',

    keyword: $ => choice(
      'this', 'add', 'alias', 'as', 'base', 'break', 'case', 'catch',
      'checked', 'class', 'continue', 'default', 'delegate', 'do', 'else',
      'enum', 'event', 'explicit', 'extern', 'finally', 'for', 'foreach',
      'global', 'goto', 'if', 'implicit', 'interface', 'is', 'lock',
      'namespace', 'notnull', 'operator', 'params', 'return', 'remove',
      'sizeof', 'stackalloc', 'struct', 'switch', 'throw', 'try', 'typeof',
      'unchecked', 'using', 'while', 'await', 'in', 'yield',
      'get', 'set', 'when', 'out', 'ref', 'from', 'where', 'select', 'record',
      'init', 'with', 'let',
    ),

    operator: $ => choice(
      '--', '-', '-=', '&', '&=', '&&', '+', '++', '+=', '<', '<=', '<<',
      '<<=', '=', '==', '!', '!=', '=>', '>', '>=', '>>', '>>=', '>>>',
      '>>>=', '|', '|=', '||', '?', '??', '??=', '^', '^=', '~', '*', '*=',
      '/', '/=', '%', '%=', ':', '..',
    ),
  },
});
