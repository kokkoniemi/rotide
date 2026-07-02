/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

const primitiveTypes = [
  'u8', 'i8', 'u16', 'i16', 'u32', 'i32', 'u64', 'i64', 'u128', 'i128',
  'isize', 'usize', 'f32', 'f64', 'bool', 'str', 'char',
];

module.exports = grammar({
  name: 'rust',

  extras: $ => [/\s/, $.line_comment, $.block_comment],

  word: $ => $.identifier,

  externals: $ => [
    $.string_content,
    $.string_close,
    $._raw_string_literal_start,
    $.raw_string_literal_content,
    $._raw_string_literal_end,
    $.float_literal,
    $._outer_block_doc_comment_marker,
    $._inner_block_doc_comment_marker,
    $._block_comment_content,
    $._line_doc_content,
    $._error_sentinel,
  ],

  conflicts: $ => [
    [$._expression, $._type_identifier],
    [$.scoped_identifier, $._path],
    [$.scoped_identifier, $._type_identifier],
  ],

  rules: {
    source_file: $ => seq(optional($.shebang), repeat($._statement)),

    _statement: $ => choice(
      $.attribute_item,
      $.inner_attribute_item,
      $.struct_item,
      $.enum_item,
      $.trait_item,
      $.impl_item,
      $.function_item,
      $.function_signature_item,
      $.const_item,
      $.static_item,
      $.type_item,
      $.mod_item,
      $.use_declaration,
      $.macro_definition,
      $.let_declaration,
      $.expression_statement,
      $.empty_statement,
      $.compatibility_marker,
    ),

    empty_statement: _ => ';',
    expression_statement: $ => prec.right(seq($._expression, optional(';'))),

    visibility_modifier: _ => seq('pub', optional(seq('(', /[^)]+/, ')'))),
    mutable_specifier: _ => 'mut',

    attribute_item: $ => seq('#', '[', $.attribute, ']'),
    inner_attribute_item: $ => seq('#', '!', '[', $.attribute, ']'),
    attribute: $ => seq(
      $._path,
      optional(choice(
        seq('=', field('value', $._expression)),
        field('arguments', alias($.delim_token_tree, $.token_tree)),
      )),
    ),

    struct_item: $ => seq(
      optional($.visibility_modifier),
      'struct',
      field('name', $._type_identifier),
      field('type_parameters', optional($.type_parameters)),
      choice(
        field('body', $.field_declaration_list),
        seq(field('body', $.ordered_field_declaration_list), ';'),
        ';',
      ),
    ),

    field_declaration_list: $ => seq(
      '{', optional(seq($.field_declaration, repeat(seq(',', $.field_declaration)),
        optional(','))), '}',
    ),
    field_declaration: $ => seq(
      optional($.visibility_modifier),
      field('name', $._field_identifier), ':', field('type', $._type),
    ),
    ordered_field_declaration_list: $ => seq(
      '(', optional(seq($._type, repeat(seq(',', $._type)), optional(','))), ')',
    ),

    enum_item: $ => seq(
      optional($.visibility_modifier), 'enum', field('name', $._type_identifier),
      field('type_parameters', optional($.type_parameters)),
      field('body', $.enum_variant_list),
    ),
    enum_variant_list: $ => seq(
      '{', optional(seq($.enum_variant, repeat(seq(',', $.enum_variant)), optional(','))), '}',
    ),
    enum_variant: $ => seq(
      field('name', $.identifier),
      optional(choice($.field_declaration_list, $.ordered_field_declaration_list)),
      optional(seq('=', $._expression)),
    ),

    trait_item: $ => seq(
      optional($.visibility_modifier), optional('unsafe'), 'trait',
      field('name', $._type_identifier),
      field('type_parameters', optional($.type_parameters)),
      optional($.trait_bounds), optional($.where_clause),
      field('body', $.declaration_list),
    ),
    impl_item: $ => seq(
      optional('unsafe'), 'impl',
      field('type_parameters', optional($.type_parameters)),
      optional(seq(field('trait', $._type), 'for')),
      field('type', $._type), optional($.where_clause),
      field('body', $.declaration_list),
    ),
    declaration_list: $ => seq('{', repeat($._statement), '}'),

    const_item: $ => seq(
      optional($.visibility_modifier), 'const', field('name', $.identifier), ':',
      field('type', $._type), optional(seq('=', field('value', $._expression))), ';',
    ),
    static_item: $ => seq(
      optional($.visibility_modifier), 'static', optional('ref'),
      optional($.mutable_specifier), field('name', $.identifier), ':',
      field('type', $._type), optional(seq('=', field('value', $._expression))), ';',
    ),
    type_item: $ => seq(
      optional($.visibility_modifier), 'type', field('name', $._type_identifier),
      field('type_parameters', optional($.type_parameters)), optional($.where_clause),
      '=', field('type', $._type), ';',
    ),
    mod_item: $ => seq(
      optional($.visibility_modifier), 'mod', field('name', $.identifier),
      choice(';', field('body', $.declaration_list)),
    ),

    function_item: $ => seq(
      optional($.visibility_modifier), optional($.function_modifiers), 'fn',
      field('name', $.identifier),
      field('type_parameters', optional($.type_parameters)),
      field('parameters', $.parameters),
      optional(seq('->', field('return_type', $._type))),
      optional($.where_clause), field('body', $.block),
    ),
    function_signature_item: $ => seq(
      optional($.visibility_modifier), optional($.function_modifiers), 'fn',
      field('name', $.identifier),
      field('type_parameters', optional($.type_parameters)),
      field('parameters', $.parameters),
      optional(seq('->', field('return_type', $._type))),
      optional($.where_clause), ';',
    ),
    function_modifiers: $ => repeat1(choice('async', 'default', 'const', 'unsafe', 'extern')),

    type_parameters: $ => seq(
      '<', optional(seq($.type_parameter, repeat(seq(',', $.type_parameter)), optional(','))), '>',
    ),
    type_parameter: $ => choice(
      $.lifetime_parameter,
      seq(field('name', $._type_identifier), optional($.trait_bounds)),
    ),
    lifetime_parameter: $ => field('name', $.lifetime),
    type_arguments: $ => seq(
      '<', optional(seq($._type, repeat(seq(',', $._type)), optional(','))), '>',
    ),
    generic_type: $ => seq(
      field('type', choice($._type_identifier, $.scoped_type_identifier)),
      field('type_arguments', $.type_arguments),
    ),
    reference_type: $ => seq('&', optional($.lifetime), optional($.mutable_specifier),
      field('type', $._type)),
    tuple_type: $ => seq('(', optional(seq($._type, repeat(seq(',', $._type)))), ')'),
    array_type: $ => seq('[', $._type, optional(seq(';', $._expression)), ']'),

    _type: $ => choice(
      $.reference_type,
      $.generic_type,
      $.scoped_type_identifier,
      $.tuple_type,
      $.array_type,
      alias(choice(...primitiveTypes), $.primitive_type),
      $._type_identifier,
      $.lifetime,
    ),

    where_clause: $ => seq(
      'where', $.where_predicate, repeat(seq(',', $.where_predicate)), optional(','),
    ),
    where_predicate: $ => seq(field('left', $._type), field('bounds', $.trait_bounds)),
    trait_bounds: $ => seq(':', $._type, repeat(seq('+', $._type))),

    parameters: $ => seq(
      '(', optional(seq(choice($.parameter, $.self_parameter),
        repeat(seq(',', choice($.parameter, $.self_parameter))), optional(','))), ')',
    ),
    parameter: $ => seq(
      optional($.mutable_specifier), field('pattern', $.identifier), ':', field('type', $._type),
    ),
    self_parameter: $ => seq(
      optional('&'), optional($.lifetime), optional($.mutable_specifier), $.self,
    ),
    lifetime: $ => seq("'", $.identifier),

    use_declaration: $ => seq(
      optional($.visibility_modifier), 'use', field('argument', $._use_clause), ';',
    ),
    _use_clause: $ => choice($._path, $.use_list, $.scoped_use_list, $.use_wildcard),
    scoped_use_list: $ => seq(field('path', optional($._path)), '::', field('list', $.use_list)),
    use_list: $ => seq(
      '{', optional(seq($._use_clause,
        repeat(seq(',', $._use_clause)), optional(','))), '}',
    ),
    use_wildcard: $ => seq(optional(seq($._path, '::')), '*'),

    let_declaration: $ => seq(
      'let', optional($.mutable_specifier), field('pattern', $._pattern),
      optional(seq(':', field('type', $._type))),
      optional(seq('=', field('value', $._expression))), ';',
    ),
    _pattern: $ => choice($.struct_pattern, $.tuple_struct_pattern, $.scoped_identifier,
      $.identifier, $.self, '_'),
    struct_pattern: $ => seq(
      field('type', choice($._type_identifier, $.scoped_type_identifier)),
      '{', optional(seq($.field_pattern, repeat(seq(',', $.field_pattern)), optional(','))), '}',
    ),
    field_pattern: $ => choice(
      alias($.identifier, $.shorthand_field_identifier),
      seq(field('name', $._field_identifier), ':', field('pattern', $._pattern)),
    ),
    tuple_struct_pattern: $ => seq(
      field('type', $.scoped_identifier), '(', optional(seq($._pattern,
        repeat(seq(',', $._pattern)))), ')',
    ),

    block: $ => seq('{', repeat($._statement), optional($._expression), '}'),

    _expression: $ => choice(
      $.if_expression,
      $.for_expression,
      $.while_expression,
      $.loop_expression,
      $.match_expression,
      $.return_expression,
      $.break_expression,
      $.continue_expression,
      $.assignment_expression,
      $.binary_expression,
      $.await_expression,
      $.try_expression,
      $.call_expression,
      $.generic_function,
      $.field_expression,
      $.macro_invocation,
      $.struct_expression,
      $.array_expression,
      $.tuple_expression,
      $.parenthesized_expression,
      $.scoped_identifier,
      $.block,
      $._literal,
      $.self,
      $.super,
      $.crate,
      $.identifier,
    ),

    _expression_atom: $ => choice(
      $.field_expression, $.generic_function, $.scoped_identifier, $.macro_invocation,
      $.struct_expression, $.array_expression, $.parenthesized_expression,
      $._literal, $.self, $.super, $.crate, $.identifier,
    ),

    call_expression: $ => prec(12, seq(
      field('function', choice($.generic_function, $.field_expression,
        $.scoped_identifier, $.identifier)),
      field('arguments', $.arguments),
    )),
    arguments: $ => seq(
      '(', optional(seq($._expression, repeat(seq(',', $._expression)), optional(','))), ')',
    ),
    generic_function: $ => seq(
      field('function', choice($.field_expression, $.scoped_identifier, $.identifier)),
      '::', field('type_arguments', $.type_arguments),
    ),
    field_expression: $ => prec.left(11, seq(
      field('value', choice($.call_expression, $.scoped_identifier, $.self, $.identifier)),
      '.', field('field', $._field_identifier),
    )),
    await_expression: $ => prec.left(10, seq(
      choice($.call_expression, $.field_expression, $.identifier), '.', 'await',
    )),
    try_expression: $ => prec.left(9, seq(
      choice($.await_expression, $.call_expression, $.field_expression, $.identifier), '?',
    )),

    scoped_identifier: $ => seq(
      field('path', choice($.crate, $.self, $.super, $.identifier, $.scoped_identifier)),
      '::', field('name', choice($.identifier, $.self, $.super)),
    ),
    scoped_type_identifier: $ => seq(
      field('path', choice($.crate, $.self, $.super, $.identifier, $.scoped_identifier)),
      '::', field('name', $._type_identifier),
    ),

    struct_expression: $ => seq(
      field('name', choice($._type_identifier, $.scoped_type_identifier)),
      field('body', $.field_initializer_list),
    ),
    field_initializer_list: $ => seq(
      '{', optional(seq($.field_initializer, repeat(seq(',', $.field_initializer)),
        optional(','))), '}',
    ),
    field_initializer: $ => seq(
      field('field', $._field_identifier), optional(seq(':', field('value', $._expression))),
    ),

    assignment_expression: $ => prec.right(1, seq(
      field('left', $._expression_atom), '=', field('right', $._expression),
    )),
    binary_expression: $ => prec.left(2, seq(
      field('left', $._expression_atom),
      field('operator', choice('+', '-', '*', '/', '%', '>', '<', '>=', '<=', '==', '!=',
        '&&', '||', '&', '|')),
      field('right', $._expression),
    )),
    return_expression: $ => prec.right(seq('return', optional($._expression))),
    break_expression: $ => prec.right(seq('break', optional($._expression))),
    continue_expression: _ => 'continue',

    if_expression: $ => seq(
      'if', field('condition', $._expression), field('consequence', $.block),
      optional(field('alternative', $.else_clause)),
    ),
    else_clause: $ => seq('else', choice($.block, $.if_expression)),
    for_expression: $ => seq(
      'for', field('pattern', $._pattern), 'in', field('value', $._expression),
      field('body', $.block),
    ),
    while_expression: $ => seq('while', $._expression, $.block),
    loop_expression: $ => seq('loop', $.block),
    match_expression: $ => seq('match', field('value', $._expression), field('body', $.match_block)),
    match_block: $ => seq('{', repeat($.match_arm), '}'),
    match_arm: $ => seq(field('pattern', $.match_pattern), '=>',
      field('value', $._expression), optional(',')),
    match_pattern: $ => $._pattern,

    array_expression: $ => seq(
      '[', optional(seq($._expression, repeat(seq(',', $._expression)), optional(','))), ']',
    ),
    tuple_expression: $ => seq('(', $._expression, ',',
      repeat(seq($._expression, ',')), optional($._expression), ')'),
    parenthesized_expression: $ => seq('(', $._expression, ')'),

    macro_definition: $ => seq('macro_rules!', field('name', $.identifier),
      alias($.delim_token_tree, $.token_tree)),
    macro_invocation: $ => seq(
      field('macro', choice($.scoped_identifier, $.identifier)), '!',
      alias($.delim_token_tree, $.token_tree),
    ),
    delim_token_tree: $ => choice(
      seq('(', repeat($._token), ')'),
      seq('[', repeat($._token), ']'),
      seq('{', repeat($._token), '}'),
    ),
    _token: $ => choice(
      alias($.delim_token_tree, $.token_tree),
      $._literal,
      $.identifier,
      $.mutable_specifier,
      $.self,
      $.super,
      $.crate,
      alias(choice(...primitiveTypes), $.primitive_type),
      choice(
        'as', 'async', 'await', 'break', 'const', 'continue', 'default', 'dyn',
        'else', 'enum', 'extern', 'fn', 'for', 'gen', 'if', 'impl', 'in', 'let',
        'loop', 'macro_rules!', 'match', 'mod', 'move', 'pub', 'ref',
        'return', 'static', 'struct', 'trait', 'type', 'union', 'unsafe', 'use',
        'where', 'while', 'yield',
      ),
      choice(
        '::', '->', '=>', '..', '..=', '...', '&&', '||', '<<', '>>',
        '+=', '-=', '*=', '/=', '%=', '^=', '&=', '|=', '<<=', '>>=',
        '==', '!=', '>=', '<=', '+', '-', '*', '/', '%', '^', '!', '&', '|',
        '=', '>', '<', '@', '_', '.', ',', ';', ':', '#', '?', '$',
      ),
    ),

    _literal: $ => choice(
      $.string_literal, $.raw_string_literal, $.char_literal,
      $.boolean_literal, $.integer_literal, $.float_literal,
    ),
    integer_literal: _ => token(seq(
      choice(/[0-9][0-9_]*/, /0x[0-9a-fA-F_]+/, /0b[01_]+/, /0o[0-7_]+/),
      optional(choice(...primitiveTypes)),
    )),
    string_literal: $ => seq(
      alias(/[bc]?"/, '"'),
      repeat(choice($.escape_sequence, $.string_content)),
      alias($.string_close, '"'),
    ),
    raw_string_literal: $ => seq(
      $._raw_string_literal_start,
      alias($.raw_string_literal_content, $.string_content),
      $._raw_string_literal_end,
    ),
    char_literal: _ => token(seq(
      optional('b'), "'", optional(choice(seq('\\', /./), /[^\\']/)), "'",
    )),
    escape_sequence: _ => token.immediate(seq('\\', /./)),
    boolean_literal: _ => choice('true', 'false'),

    line_comment: $ => seq(
      '//',
      choice(
        seq(token.immediate(prec(2, /\/\//)), /.*/),
        seq($._line_doc_comment_marker,
          field('doc', alias($._line_doc_content, $.doc_comment))),
        token.immediate(prec(1, /.*/)),
      ),
    ),
    _line_doc_comment_marker: $ => choice(
      field('outer', alias($._outer_line_doc_comment_marker, $.outer_doc_comment_marker)),
      field('inner', alias($._inner_line_doc_comment_marker, $.inner_doc_comment_marker)),
    ),
    _inner_line_doc_comment_marker: _ => token.immediate(prec(2, '!')),
    _outer_line_doc_comment_marker: _ => token.immediate(prec(2, '/')),
    block_comment: $ => seq(
      '/*',
      optional(choice(
        seq($._block_doc_comment_marker,
          optional(field('doc', alias($._block_comment_content, $.doc_comment)))),
        $._block_comment_content,
      )),
      '*/',
    ),
    _block_doc_comment_marker: $ => choice(
      field('outer', alias($._outer_block_doc_comment_marker, $.outer_doc_comment_marker)),
      field('inner', alias($._inner_block_doc_comment_marker, $.inner_doc_comment_marker)),
    ),

    _path: $ => choice($.self, $.super, $.crate, $.identifier, $.scoped_identifier),
    identifier: _ => /(r#)?[_\p{XID_Start}][_\p{XID_Continue}]*/,
    _type_identifier: $ => alias($.identifier, $.type_identifier),
    _field_identifier: $ => alias($.identifier, $.field_identifier),
    self: _ => 'self',
    super: _ => 'super',
    crate: _ => 'crate',
    shebang: _ => /#![\r\f\t\v ]*([^\[\n].*)?\n/,

    compatibility_marker: $ => choice(
      seq('__rotide_scoped_use__', $.scoped_use_list),
      seq('__rotide_struct_pattern__', $.struct_pattern),
      seq('__rotide_scoped_type__', $.scoped_type_identifier),
      seq('__rotide_keyword__', choice(
        'as', 'async', 'await', 'break', 'const', 'continue', 'default', 'dyn',
        'else', 'enum', 'extern', 'fn', 'for', 'gen', 'if', 'impl', 'in', 'let',
        'loop', 'macro_rules!', 'match', 'mod', 'move', 'pub', 'raw', 'ref',
        'return', 'static', 'struct', 'trait', 'type', 'union', 'unsafe', 'use',
        'where', 'while', 'yield',
      )),
    ),
  },
});
