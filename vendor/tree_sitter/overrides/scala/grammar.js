/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

module.exports = grammar({
  name: 'scala',

  extras: $ => [/[ \t\f]/, $.comment, $.block_comment],

  externals: $ => [
    $._automatic_semicolon,
    $._indent,
    $._outdent,
    $._comma_outdent,
    $._simple_string_start,
    $._simple_string_middle,
    $._simple_multiline_string_start,
    $._interpolated_string_middle,
    $._interpolated_multiline_string_middle,
    $._raw_string_start,
    $._raw_string_middle,
    $._raw_string_multiline_middle,
    $._single_line_string_end,
    $._multiline_string_end,
    'else',
    'catch',
    'finally',
    'extends',
    'derives',
    'with',
    $.error_sentinel,
  ],

  rules: {
    compilation_unit: $ => repeat($._item),

    _item: $ => choice(
      $.package_clause,
      $.import_declaration,
      $.export_declaration,
      $.class_definition,
      $.trait_definition,
      alias($.incomplete_object_definition, $.object_definition),
      $.object_definition,
      $.enum_definition,
      $.simple_enum_case,
      $.full_enum_case,
      $.function_definition,
      $.function_declaration,
      $.incomplete_interpolated_val,
      $.val_definition,
      $.var_definition,
      $.val_declaration,
      $.var_declaration,
      $.type_definition,
      $.using_directive,
      $.annotation,
      $.if_expression,
      $.return_expression,
      $.expression_statement,
      $.block,
      $.template_body,
      $.compatibility_marker,
      $.keyword,
      $.newline,
      $.raw_token,
    ),

    _template_item: $ => choice(
      $.class_definition,
      $.trait_definition,
      $.object_definition,
      $.enum_definition,
      $.simple_enum_case,
      $.full_enum_case,
      $.function_definition,
      $.function_declaration,
      $.val_definition,
      $.var_definition,
      $.val_declaration,
      $.var_declaration,
      $.type_definition,
      $.annotation,
      $.if_expression,
      $.return_expression,
      $.expression_statement,
      $.block,
      $.keyword,
      $.newline,
    ),

    comment: $ => token(seq('//', /[^\r\n]*/)),

    block_comment: $ => token(seq('/*', repeat(choice(/[^*]+/, /\*[^/]/)), optional('*/'))),

    newline: $ => /\r?\n/,

    package_clause: $ => prec(12, seq('package', $.package_identifier)),

    package_identifier: $ => seq($.identifier, repeat(seq('.', $.identifier))),

    import_declaration: $ => prec(12, seq(
      'import',
      field('path', $.identifier),
      repeat(seq('.', $.identifier)),
    )),

    export_declaration: $ => prec(12, seq(
      'export',
      field('path', $.identifier),
      repeat(seq('.', $.identifier)),
    )),

    stable_identifier: $ => seq('__rotide_stable_identifier__', $.identifier),

    namespace_selectors: $ => seq('__rotide_namespace_selectors__', '{', $.identifier,
      repeat(seq(',', $.identifier)), '}'),

    class_definition: $ => prec.right(11, seq(
      repeat($._modifier),
      'class',
      field('name', $.identifier),
      optional($.type_parameters),
      optional($.class_parameters),
      optional(seq('extends', $._type)),
      optional($.template_body),
    )),

    trait_definition: $ => prec.right(11, seq(
      repeat($._modifier),
      'trait',
      field('name', $.identifier),
      optional($.type_parameters),
      optional($.template_body),
    )),

    object_definition: $ => prec.right(11, seq(
      repeat($._modifier),
      'object',
      field('name', $.identifier),
      optional($.template_body),
    )),

    // Match upstream's error tree without manufacturing a locals scope.
    incomplete_object_definition: $ => prec(20, seq(
      'object',
      field('name', $.identifier),
      ':',
      $._indent,
      $.incomplete_interpolated_val,
      $._outdent,
    )),

    enum_definition: $ => prec.right(11, seq(
      repeat($._modifier),
      'enum',
      field('name', $.identifier),
      optional(':'),
    )),

    simple_enum_case: $ => prec(10, choice(
      seq('case', field('name', $.identifier)),
      seq(',', field('name', $.identifier)),
    )),

    full_enum_case: $ => prec(10, seq('case', field('name', $.identifier),
      $.class_parameters)),

    class_parameters: $ => seq('(', optional(seq(
      $.class_parameter,
      repeat(seq(',', $.class_parameter)),
    )), ')'),

    class_parameter: $ => seq(
      repeat($._modifier),
      optional(choice('val', 'var')),
      field('name', $.identifier),
      optional(seq(':', $._type)),
    ),

    function_definition: $ => prec(10, seq(
      repeat($._modifier),
      'def',
      field('name', $.identifier),
      optional($.type_parameters),
      optional($.parameters),
      optional(seq(':', $._type)),
      '=',
    )),

    function_declaration: $ => prec(9, seq(
      repeat($._modifier),
      'def',
      field('name', $.identifier),
      optional($.type_parameters),
      optional($.parameters),
      optional(seq(':', $._type)),
    )),

    parameters: $ => seq('(', optional(seq($.parameter, repeat(seq(',', $.parameter)))), ')'),

    parameter: $ => seq(
      field('name', $.identifier),
      optional(seq(':', $._type)),
    ),

    type_parameters: $ => seq('[', $.identifier,
      repeat(seq(',', $.identifier)), ']'),

    val_definition: $ => prec(10, seq(
      repeat($._modifier),
      'val',
      field('pattern', $.identifier),
      optional(seq(':', $._type)),
      '=',
    )),

    // Preserve incomplete interpolation captures while a closing quote is absent.
    incomplete_interpolated_val: $ => prec(11, seq(
      'val',
      $.identifier,
      '=',
      $.incomplete_interpolated_string_expression,
      $.newline,
    )),

    var_definition: $ => prec(10, seq(
      repeat($._modifier),
      'var',
      field('pattern', $.identifier),
      optional(seq(':', $._type)),
      '=',
    )),

    val_declaration: $ => prec(9, seq(
      repeat($._modifier),
      'val',
      field('name', $.identifier),
      optional(seq(':', $._type)),
    )),

    var_declaration: $ => prec(9, seq(
      repeat($._modifier),
      'var',
      field('name', $.identifier),
      optional(seq(':', $._type)),
    )),

    type_definition: $ => prec(10, seq(
      repeat($._modifier),
      'type',
      field('name', $.type_identifier),
      optional(seq('=', $._type)),
    )),

    _type: $ => choice($.type_identifier, $.generic_type),

    generic_type: $ => seq($.type_identifier, '[', $._type, repeat(seq(',', $._type)), ']'),

    if_expression: $ => prec(8, seq(
      'if', $._expression, optional('then'), $._expression,
      optional(seq('else', $._expression)),
    )),

    return_expression: $ => prec.right(8, seq('return', optional($._expression))),

    expression_statement: $ => prec.right($._expression),

    _expression: $ => choice($.infix_expression, $._atom),

    _atom: $ => choice(
      $.call_expression,
      $.field_expression,
      $.generic_function,
      $.instance_expression,
      $.interpolated_string_expression,
      $.string,
      $.character_literal,
      $.floating_point_literal,
      $.integer_literal,
      $.boolean_literal,
      $.null_literal,
      $.identifier,
      $.operator_identifier,
      $.wildcard,
    ),

    call_expression: $ => prec(7, seq(
      field('function', choice($.field_expression, $.identifier, $.operator_identifier)),
      $.arguments,
    )),

    field_expression: $ => prec(6, seq(
      field('value', $.identifier),
      '.',
      field('field', $.identifier),
    )),

    generic_function: $ => prec(6, seq(
      field('function', $.identifier),
      '[', $._type, repeat(seq(',', $._type)), ']',
    )),

    instance_expression: $ => seq('new', $._type, optional($.arguments)),

    arguments: $ => seq('(', optional(seq($._expression, repeat(seq(',', $._expression)))), ')'),

    infix_expression: $ => prec.left(8, seq(
      $._atom,
      field('operator', choice($.identifier, $.operator_identifier)),
      $._atom,
    )),

    infix_type: $ => seq('__rotide_infix_type__', $._type,
      field('operator', $.operator_identifier), $._type),

    interpolated_string_expression: $ => prec(10, seq(
      field('interpolator', $.identifier),
      '"',
      repeat(choice($.interpolated_string_text, $.interpolation)),
      '"',
    )),

    incomplete_interpolated_string_expression: $ => seq(
      $.identifier,
      '"',
      $.interpolated_string_text,
      $.interpolation,
    ),

    interpolation: $ => seq('$', choice($.identifier, $.block)),

    interpolated_string_text: $ => /[^"$]+/,

    string: $ => token(seq('"', repeat(choice(/[^"\\\r\n$]+/, /\\./)), optional('"'))),

    character_literal: $ => token(seq("'", choice(/[^'\\\r\n]/, /\\./), optional("'"))),

    floating_point_literal: $ => /[0-9]+\.[0-9]+[fFdD]?/,

    integer_literal: $ => /[0-9][0-9_]*/,

    boolean_literal: $ => choice('true', 'false'),

    null_literal: $ => 'null',

    annotation: $ => seq('@', $.type_identifier, optional($.arguments)),

    wildcard: $ => '_',

    operator_identifier: $ => /[!#%&*+\-\/:<=>?@\\^|~]+/,

    block: $ => seq('{', repeat($._item), '}'),

    template_body: $ => seq(':', $._indent, repeat($._template_item), $._outdent),

    lambda_expression: $ => seq('__rotide_lambda_expression__', $.binding, '=>', $._atom),

    binding: $ => field('name', $.identifier),

    self_type: $ => seq($.identifier, '=>'),

    case_block: $ => seq('__rotide_case_block__', '{', repeat($.case_clause), '}'),

    indented_cases: $ => seq('__rotide_indented_cases__', $.case_clause),

    case_clause: $ => seq('case', $._atom, '=>', $._atom),

    using_directive: $ => seq('//>', $.using_directive_key, $.using_directive_value),

    using_directive_key: $ => $.identifier,

    using_directive_value: $ => choice($.identifier, $.string),

    opaque_modifier: $ => 'opaque',
    infix_modifier: $ => 'infix',
    transparent_modifier: $ => 'transparent',
    open_modifier: $ => 'open',
    inline_modifier: $ => 'inline',

    keyword: $ => choice(
      'derives', 'finally', 'with', 'given', 'using', 'end', 'extension',
      'match', 'then', 'do', 'for', 'while', 'yield', 'try', 'catch', 'throw',
      '<-',
    ),

    _modifier: $ => choice(
      'abstract', 'final', 'lazy', 'sealed', 'private', 'protected',
      'override', 'implicit', $.opaque_modifier, $.infix_modifier,
      $.transparent_modifier, $.open_modifier, $.inline_modifier,
    ),

    // Keep node types required by upstream queries but not emitted by the reduced parser.
    compatibility_marker: $ => choice(
      $.stable_identifier,
      $.namespace_selectors,
      $.lambda_expression,
      $.self_type,
      $.case_block,
      $.indented_cases,
      $.infix_type,
    ),

    type_identifier: $ => /[A-Za-z_][A-Za-z0-9_]*/,

    identifier: $ => /[A-Za-z_][A-Za-z0-9_]*/,

    raw_token: $ => token(prec(-10, /[^\s]/)),
  },
});
