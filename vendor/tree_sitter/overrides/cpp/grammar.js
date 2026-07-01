/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

const PREC = {
  ASSIGN: 1,
  BINARY: 2,
  UNARY: 3,
  CALL: 4,
};

module.exports = grammar({
  name: 'cpp',

  extras: $ => [/[ \t\f\r]/, $.comment],
  externals: $ => [
    $.raw_string_delimiter,
    $.raw_string_content,
  ],

  conflicts: $ => [
    [$.type_identifier, $.identifier],
    [$.field_identifier, $.identifier],
    [$.namespace_identifier, $.type_identifier],
    [$.namespace_identifier, $.type_identifier, $.field_identifier, $.identifier],
    [$.class_specifier, $.function_definition, $.function_declaration, $.declaration],
    [$.class_specifier, $.function_definition, $.function_declaration,
      $.method_definition, $.field_declaration],
    [$.function_definition, $.declaration],
    [$.function_definition, $.function_declaration, $.declaration],
    [$._expression, $.template_function],
    [$.compound_statement, $.initializer_list],
    [$.qualified_type_identifier, $.qualified_identifier],
    [$.parameter_list, $.argument_list],
  ],

  rules: {
    translation_unit: $ => repeat($._item),

    _item: $ => choice(
      $.preproc_include,
      $.preproc_def,
      $.preproc_object_def,
      $.preproc_if,
      $.preproc_directive,
      $.namespace_definition,
      $.template_declaration,
      $.class_specifier,
      $.function_definition,
      $.function_declaration,
      $.declaration,
      $.return_statement,
      $.control_statement,
      $.expression_statement,
      $.compound_statement,
      $.access_specifier,
      $.labeled_statement,
      $._query_tokens,
      '\n',
      ';',
    ),

    comment: $ => token(choice(
      seq('//', /[^\r\n]*/),
      seq('/*', repeat(choice(/[^*]+/, /\*[^/]/)), optional('*/')),
    )),

    preproc_include: $ => prec.right(seq(
      '#include',
      choice($.system_lib_string, $.string_literal),
      optional('\n'),
    )),
    system_lib_string: $ => token(seq('<', /[^>\r\n]*/, optional('>'))),

    preproc_def: $ => prec.dynamic(2, prec.right(seq(
      '#define',
      $.preproc_function_def,
      optional($.preproc_arg),
      optional('\n'),
    ))),
    preproc_object_def: $ => prec.dynamic(-1, prec.right(seq(
      '#define',
      $.identifier,
      optional($.preproc_object_arg),
      optional('\n'),
    ))),
    preproc_function_def: $ => prec.dynamic(1, seq(
      field('name', $.identifier),
      '(', optional(seq($.identifier, repeat(seq(',', $.identifier)))), ')',
    )),
    preproc_if: $ => prec.right(seq(
      choice('#if', '#ifdef', '#ifndef', '#elif', '#else', '#endif'),
      repeat($._preproc_token),
      optional('\n'),
    )),
    preproc_directive: $ => seq('#pragma', optional(/[^\r\n]+/)),
    preproc_arg: $ => token(/[^\r\n]+/),
    preproc_object_arg: $ => token(/[^\r\n(][^\r\n]*/),
    _preproc_token: $ => choice(
      $.identifier, $.number_literal, $.string_literal, $.char_literal,
      '(', ')', ',', '.', '+', '-', '*', '/', '=', '==', '!=', '&&', '||',
    ),

    namespace_definition: $ => prec.right(20, seq(
      optional('inline'),
      'namespace',
      optional($.namespace_identifier),
      $.compound_statement,
    )),

    template_declaration: $ => prec.right(19, seq(
      'template',
      $.template_parameter_list,
      choice(
        $.concept_definition,
        $.class_specifier,
        $.function_definition,
        $.declaration,
      ),
    )),
    template_parameter_list: $ => seq(
      '<', optional(seq($.template_parameter, repeat(seq(',', $.template_parameter)))), '>',
    ),
    template_parameter: $ => seq(
      optional(choice('typename', 'class')),
      choice($.type_identifier, $.identifier),
      optional(seq('=', $._type)),
    ),
    concept_definition: $ => prec.right(seq(
      'concept', $.identifier, '=', optional($.requires_expression), ';',
    )),
    requires_expression: $ => prec.right(seq(
      'requires', optional($.parameter_list), optional($.compound_statement),
    )),

    class_specifier: $ => prec.right(18, seq(
      repeat($.declaration_modifier),
      choice('class', 'struct', 'union', 'enum'),
      optional($.type_identifier),
      optional('final'),
      optional(seq(':', repeat1(choice($.type_identifier, $.namespace_identifier,
        $.access_specifier, ',', 'virtual')))),
      optional($.class_body),
      optional(';'),
    )),

    class_body: $ => seq('{', repeat($._class_item), '}'),
    _class_item: $ => choice(
      $.access_specifier,
      $.template_method_definition,
      $.method_definition,
      $.field_declaration,
      $.function_definition,
      $.function_declaration,
      $.class_specifier,
      $.preproc_include,
      $.preproc_def,
      $.preproc_object_def,
      $.preproc_if,
      $.preproc_directive,
      '\n',
      ';',
    ),

    access_specifier: $ => seq(choice('public', 'private', 'protected'), ':'),
    labeled_statement: $ => seq($.statement_identifier, ':'),

    function_definition: $ => prec.right(17, seq(
      repeat(choice($.declaration_modifier, $._type)),
      $.function_declarator,
      optional($.noexcept_specifier),
      optional($.field_initializer_list),
      $.compound_statement,
    )),
    function_declaration: $ => prec.right(16, seq(
      repeat(choice($.declaration_modifier, $._type)),
      $.function_declarator,
      optional($.noexcept_specifier),
      ';',
    )),
    function_declarator: $ => prec.right(seq(
      field('declarator', choice(
        $.identifier,
        $.qualified_identifier,
      )),
      $.parameter_list,
      repeat(choice('const', 'override', 'final', $.noexcept_specifier,
        $.requires_clause)),
    )),
    method_definition: $ => prec.dynamic(1, prec.right(18, seq(
      repeat1(choice($.declaration_modifier, $._type)),
      alias($.method_declarator, $.function_declarator),
      optional($.noexcept_specifier),
      $.compound_statement,
    ))),
    template_method_definition: $ => prec.dynamic(2, prec.right(19, seq(
      'template',
      $.template_parameter_list,
      repeat1(choice($.declaration_modifier, $._type)),
      alias($.identifier_function_declarator, $.function_declarator),
      optional($.noexcept_specifier),
      $.compound_statement,
    ))),
    identifier_function_declarator: $ => prec.right(seq(
      field('declarator', $.identifier),
      $.parameter_list,
      repeat(choice('const', 'override', 'final', $.noexcept_specifier,
        $.requires_clause)),
    )),
    method_declarator: $ => prec.right(seq(
      field('declarator', $.field_identifier),
      $.parameter_list,
      repeat(choice('const', 'override', 'final', $.noexcept_specifier,
        $.requires_clause)),
    )),
    field_declaration: $ => prec.right(17, seq(
      repeat1(choice($.declaration_modifier, $._type)),
      repeat(choice('*', '&', '&&')),
      $.field_identifier,
      optional(seq('=', $._expression)),
      ';',
    )),
    noexcept_specifier: $ => prec.right(seq(
      'noexcept', optional(seq('(', $._expression, ')')),
    )),
    requires_clause: $ => seq('requires', $._expression),
    field_initializer_list: $ => seq(
      ':', $.field_initializer, repeat(seq(',', $.field_initializer)),
    ),
    field_initializer: $ => seq(
      choice($.field_identifier, $.identifier),
      choice($.argument_list, $.initializer_list),
    ),

    declaration: $ => prec.right(10, seq(
      repeat1(choice($.declaration_modifier, $._type)),
      optional($.init_declarator),
      repeat(seq(',', $.init_declarator)),
      optional(';'),
    )),
    declaration_modifier: $ => choice(
      'const', 'constexpr', 'consteval', 'constinit', 'extern', 'friend', 'inline',
      'mutable', 'static', 'typedef', 'virtual', 'explicit', 'volatile',
    ),
    init_declarator: $ => prec.right(seq(
      repeat(choice('*', '&', '&&')),
      $.identifier,
      optional(choice(
        seq('=', $._expression),
        $.initializer_list,
      )),
    )),

    _type: $ => choice(
      $.primitive_type,
      $.sized_type_specifier,
      $.type_identifier,
      $.qualified_type_identifier,
      $.template_type,
      $.auto,
    ),
    primitive_type: $ => choice(
      'bool', 'char', 'char8_t', 'char16_t', 'char32_t', 'double', 'float',
      'int', 'void', 'wchar_t',
    ),
    sized_type_specifier: $ => prec.right(seq(
      choice('long', 'short', 'signed', 'unsigned'),
      optional($.primitive_type),
    )),
    auto: $ => 'auto',
    template_type: $ => seq(
      choice($.type_identifier, $.namespace_identifier),
      '<', optional(seq($._type, repeat(seq(',', $._type)))), '>',
    ),
    qualified_type_identifier: $ => seq(
      repeat1(seq($.namespace_identifier, '::')),
      $.type_identifier,
    ),

    compound_statement: $ => seq('{', repeat($._item), '}'),
    parameter_list: $ => seq(
      '(', optional(seq($.parameter_declaration,
        repeat(seq(',', $.parameter_declaration)))), ')',
    ),
    parameter_declaration: $ => seq(
      repeat($.declaration_modifier),
      $._type,
      repeat(choice('*', '&', '&&')),
      optional($.identifier),
      optional(seq('=', $._expression)),
    ),

    return_statement: $ => prec.right(seq(
      choice('return', 'co_return', 'co_yield'), optional($._expression), optional(';'),
    )),
    control_statement: $ => prec.right(seq(
      choice('if', 'else', 'for', 'while', 'do', 'switch', 'case', 'default',
        'try', 'catch', 'throw'),
      optional(seq('(', optional($._expression), ')')),
      optional($.compound_statement),
    )),
    expression_statement: $ => seq($._expression, ';'),

    _expression: $ => choice(
      $.assignment_expression,
      $.binary_expression,
      $.unary_expression,
      $.call_expression,
      $.field_expression,
      $.qualified_identifier,
      $.template_function,
      $.template_method,
      $.identifier,
      $.this,
      $.null,
      $.number_literal,
      $.char_literal,
      $.string_literal,
      $.incomplete_string_literal,
      $.raw_string_literal,
      $.incomplete_raw_string_literal,
      $.initializer_list,
      $.parenthesized_expression,
    ),
    assignment_expression: $ => prec.right(PREC.ASSIGN, seq(
      choice($.identifier, $.field_expression),
      choice('=', '+=', '-='),
      $._expression,
    )),
    binary_expression: $ => prec.left(PREC.BINARY, seq(
      $._expression,
      choice('+', '-', '*', '/', '<', '>', '==', '!=', '&&', '||'),
      $._expression,
    )),
    unary_expression: $ => prec(PREC.UNARY, seq(
      choice('!', '~', '+', '-', '*', '&', '++', '--', 'sizeof', 'new', 'delete',
        'co_await'),
      $._expression,
    )),
    call_expression: $ => prec(PREC.CALL, seq(
      field('function', choice(
        $.identifier,
        $.field_expression,
        $.qualified_identifier,
        $.template_function,
        $.template_method,
      )),
      $.argument_list,
    )),
    field_expression: $ => prec(PREC.CALL, seq(
      field('argument', choice($.identifier, $.this, $.call_expression)),
      choice('.', '->'),
      field('field', $.field_identifier),
    )),
    qualified_identifier: $ => seq(
      repeat1(seq(choice($.namespace_identifier, $.type_identifier), '::')),
      field('name', $.identifier),
    ),
    template_function: $ => seq(
      field('name', $.identifier), $.template_argument_list,
    ),
    template_method: $ => seq(
      field('name', $.field_identifier), $.template_argument_list,
    ),
    template_argument_list: $ => seq(
      '<', optional(seq(choice($._type, $._expression),
        repeat(seq(',', choice($._type, $._expression))))), '>',
    ),
    argument_list: $ => seq(
      '(', optional(seq($._expression, repeat(seq(',', $._expression)))), ')',
    ),
    initializer_list: $ => seq(
      '{', optional(seq($._expression, repeat(seq(',', $._expression)))), '}',
    ),
    parenthesized_expression: $ => seq('(', $._expression, ')'),

    raw_string_literal: $ => prec.right(seq(
      'R"',
      optional(field('delimiter', $.raw_string_delimiter)),
      '(',
      optional($.raw_string_content),
      ')', optional($.raw_string_delimiter), '"',
    )),
    string_literal: $ => token(seq(
      optional(choice('u8', 'u', 'U', 'L')),
      '"', repeat(choice(/[^"\\\r\n]+/, /\\./)), '"',
    )),
    incomplete_string_literal: $ => token(seq(
      '"', repeat(choice(/[^"\\\r\n]+/, /\\./)),
    )),
    incomplete_raw_string_literal: $ => token(seq(
      'R"', repeat(choice(/[^)"\r\n]+/, /\)[^"\r\n]/)), /\r?\n/,
    )),
    char_literal: $ => token(seq(
      optional(choice('u8', 'u', 'U', 'L')),
      "'", repeat(choice(/[^'\\\r\n]+/, /\\./)), optional("'"),
    )),
    number_literal: $ => token(choice(
      /0[xX][0-9a-fA-F']+([uUlL]*)/,
      /[0-9][0-9']*(\.[0-9']*)?([eEpP][+-]?[0-9']+)?[fFuUlL]*/,
    )),
    null: $ => choice('nullptr', 'NULL'),
    this: $ => 'this',

    namespace_identifier: $ => /[A-Za-z_][A-Za-z0-9_]*/,
    type_identifier: $ => /[A-Za-z_][A-Za-z0-9_]*/,
    field_identifier: $ => /[A-Za-z_][A-Za-z0-9_]*/,
    statement_identifier: $ => /[A-Za-z_][A-Za-z0-9_]*/,
    identifier: $ => /[A-Za-z_][A-Za-z0-9_]*/,

    _query_tokens: $ => choice(
      'break', 'continue', 'using',
    ),
  },
});
