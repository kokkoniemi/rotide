/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

module.exports = grammar({
  name: 'julia',

  extras: $ => [/\s/, $.line_comment, $.block_comment],
  word: $ => $.identifier,

  conflicts: $ => [
    [$._item, $.prefixed_string_literal],
    [$._item, $.prefixed_command_literal],
    [$._expression, $._atom],
    [$._expression, $.prefixed_string_literal],
    [$._expression, $.prefixed_command_literal],
    [$._atom, $.prefixed_string_literal],
  ],

  rules: {
    source_file: $ => repeat($._item),

    _item: $ => choice(
      $.module_definition,
      $.struct_definition,
      $.function_definition,
      $.macro_definition,
      $.quote_statement,
      $.for_statement,
      $.while_statement,
      $.let_statement,
      $.if_statement,
      $.try_statement,
      $.const_statement,
      $.local_statement,
      $.global_statement,
      $.export_statement,
      $.public_statement,
      $.import_statement,
      $.using_statement,
      $.return_statement,
      $.break_statement,
      $.continue_statement,
      $.assignment,
      $.call_expression,
      $.binary_expression,
      $.string_literal,
      $.prefixed_string_literal,
      $.command_literal,
      $.prefixed_command_literal,
      $.identifier,
      $.compatibility_marker,
    ),

    block: $ => repeat1($._item),

    line_comment: $ => token(choice('#', seq('#', /[^=\r\n][^\r\n]*/))),
    block_comment: $ => token(seq('#=', repeat(choice(/[^=]+/, /=[^#]/)), optional('=#'))),

    module_definition: $ => prec.right(20, seq(
      choice('module', 'baremodule'), field('name', $.identifier),
      optional($.block), 'end',
    )),

    struct_definition: $ => prec.right(20, seq(
      optional('mutable'), 'struct', $.type_head,
      optional($.block), 'end',
    )),

    type_head: $ => prec(18, choice(
      $.identifier,
      $.binary_expression,
    )),

    function_definition: $ => prec.right(20, seq(
      'function', $.signature, optional($.block), 'end',
    )),

    macro_definition: $ => prec.right(20, seq(
      'macro', $.signature, optional($.block), 'end',
    )),

    signature: $ => $.call_expression,

    quote_statement: $ => prec.right(19, seq('quote', optional($.block), 'end')),
    let_statement: $ => prec.right(19, seq('let', optional($.block), 'end')),
    while_statement: $ => prec.right(19, seq(
      'while', $._expression, optional($.block), 'end',
    )),
    for_statement: $ => prec.right(19, seq(
      'for', $.for_binding, optional($.block), 'end',
    )),
    for_binding: $ => seq(
      optional('outer'), choice($.identifier, $.tuple_expression), $.operator, $._expression,
    ),

    if_statement: $ => prec.right(19, seq(
      'if', $._expression, optional($.block),
      repeat($.elseif_clause), optional($.else_clause), 'end',
    )),
    elseif_clause: $ => seq('elseif', $._expression, optional($.block)),
    else_clause: $ => seq('else', optional($.block)),

    try_statement: $ => prec.right(19, seq(
      'try', optional($.block), optional($.catch_clause),
      optional($.finally_clause), 'end',
    )),
    catch_clause: $ => seq('catch', optional($.block)),
    finally_clause: $ => seq('finally', optional($.block)),

    const_statement: $ => seq('const', $.assignment),
    local_statement: $ => seq('local', $.assignment),
    global_statement: $ => seq('global', $.assignment),
    export_statement: $ => seq('export', $.identifier, repeat(seq(',', $.identifier))),
    public_statement: $ => seq('public', $.identifier, repeat(seq(',', $.identifier))),
    import_statement: $ => seq('import', $.identifier, repeat(seq(',', $.identifier))),
    using_statement: $ => seq('using', $.identifier, repeat(seq(',', $.identifier))),
    return_statement: $ => prec.right(seq('return', optional($._expression))),
    break_statement: $ => 'break',
    continue_statement: $ => 'continue',

    assignment: $ => prec.right(10, choice(
      seq(
        choice(
          $.identifier,
          $.call_expression,
          $.typed_expression,
          $.tuple_expression,
        ),
        alias('=', $.operator),
        choice($._expression, $.comprehension_expression),
      ),
      seq('__rotide_open_tuple_assignment__', $.open_tuple,
        alias('=', $.operator), $.identifier),
    )),

    _expression: $ => choice(
      $.call_expression,
      $.field_expression,
      $.typed_expression,
      $.unary_typed_expression,
      $.binary_expression,
      $.unary_expression,
      $.range_expression,
      $.arrow_function_expression,
      $.ternary_expression,
      $.where_expression,
      $.index_expression,
      $.tuple_expression,
      $.quote_expression,
      $.interpolation_expression,
      $.macro_identifier,
      $.string_literal,
      $.prefixed_string_literal,
      $.command_literal,
      $.prefixed_command_literal,
      $.boolean_literal,
      $.integer_literal,
      $.float_literal,
      $.character_literal,
      $.identifier,
    ),

    _atom: $ => choice(
      $.call_expression,
      $.field_expression,
      $.typed_expression,
      $.range_expression,
      $.index_expression,
      $.string_literal,
      $.prefixed_string_literal,
      $.command_literal,
      $.boolean_literal,
      $.integer_literal,
      $.float_literal,
      $.character_literal,
      $.interpolation_expression,
      $.identifier,
    ),

    call_expression: $ => prec(15, seq(
      choice($.identifier, $.field_expression), $.argument_list,
    )),
    broadcast_call_expression: $ => seq(
      choice($.identifier, $.field_expression), '.', $.argument_list,
    ),
    argument_list: $ => seq(
      '(', optional(seq(
        choice($.assignment, $._expression),
        repeat(seq(choice(',', ';'), choice($.assignment, $._expression))),
      )), ')',
    ),

    field_expression: $ => prec(16, seq(
      field('value', $.identifier), '.', $.identifier,
    )),

    binary_expression: $ => prec.left(12, seq(
      $._atom, $.operator, $._expression,
    )),
    unary_expression: $ => seq($.operator, $._atom),
    typed_expression: $ => prec(14, seq($.identifier, '::', $.identifier)),
    unary_typed_expression: $ => seq('::', $.identifier),
    range_expression: $ => prec.left(13, seq($._atom, ':', $._atom)),
    arrow_function_expression: $ => seq(
      choice($.identifier, $.tuple_expression), '->', $._expression,
    ),
    ternary_expression: $ => seq($._atom, '?', $._expression, ':', $._expression),
    where_expression: $ => seq(choice($.curly_expression, $._atom), 'where', $._expression),

    parametrized_type_expression: $ => seq(
      choice($.identifier, $.field_expression), $.curly_expression,
    ),
    curly_expression: $ => seq(
      '{', $._expression, repeat(seq(',', $._expression)), '}',
    ),
    index_expression: $ => seq(
      $.identifier, '[', $._expression, repeat(seq(',', $._expression)), ']',
    ),

    tuple_expression: $ => seq(
      '(', $._expression, repeat1(seq(',', $._expression)), optional(','), ')',
    ),
    open_tuple: $ => seq($.identifier, ',', $.identifier),

    comprehension_expression: $ => seq(
      '[', $._expression, $.for_clause, optional($.if_clause), ']',
    ),
    for_clause: $ => seq('for', $.for_binding),
    if_clause: $ => seq('if', $._expression),

    quote_expression: $ => seq(':', choice($.identifier, $.operator)),
    macro_identifier: $ => seq('@', $.identifier),
    interpolation_expression: $ => seq(
      '$', choice($.identifier, seq('(', $._expression, ')')),
    ),

    string_literal: $ => choice(
      seq('"', repeat(choice(
        $.string_interpolation, $.escape_sequence, $.content,
      )), '"'),
      seq('"""', repeat(choice(
        $.string_interpolation, $.escape_sequence, alias($.triple_content, $.content),
      )), '"""'),
    ),

    prefixed_string_literal: $ => choice(
      seq(field('prefix', $.identifier), '"', optional(alias($.raw_content, $.content)), '"'),
      seq(field('prefix', $.identifier), '"""',
        optional(alias($.raw_triple_content, $.content)), '"""'),
    ),

    command_literal: $ => choice(
      seq('`', optional(alias($.command_content, $.content)), '`'),
      seq('```', optional(alias($.triple_command_content, $.content)), '```'),
    ),
    prefixed_command_literal: $ => choice(
      seq(field('prefix', $.identifier), '`',
        optional(alias($.command_content, $.content)), '`'),
      seq(field('prefix', $.identifier), '```',
        optional(alias($.triple_command_content, $.content)), '```'),
    ),

    string_interpolation: $ => choice(
      seq('$', $.identifier),
      seq('$', '(', $._expression, ')'),
    ),
    content: $ => token.immediate(prec(1, /[^"\\$\r\n]+/)),
    triple_content: $ => token.immediate(prec(1, /[^"\\$]+/)),
    raw_content: $ => token.immediate(prec(1, /[^"\r\n]+/)),
    raw_triple_content: $ => token.immediate(prec(1, /[^"]+/)),
    command_content: $ => token.immediate(prec(1, /[^`\r\n]+/)),
    triple_command_content: $ => token.immediate(prec(1, /[^`]+/)),
    escape_sequence: $ => token.immediate(seq('\\', /./)),

    operator: $ => token(choice(
      'in', 'isa', '.|>', '|>', '==', '!=', '<=', '>=', '<:', '>:',
      '&&', '||', '+=', '-=', '*=', '/=', '=>', '+', '-', '*', '/',
      '^', '<', '>', '|', '&', '%',
    )),

    boolean_literal: $ => choice('true', 'false'),
    integer_literal: $ => /[0-9]+/,
    float_literal: $ => /[0-9]+\.[0-9]+/,
    character_literal: $ => seq("'", choice($.escape_sequence, /[^'\\]/), "'"),
    identifier: $ => /[A-Za-z_][A-Za-z0-9_!]*/,

    compatibility_marker: $ => choice(
      seq('__rotide_abstract__', $.abstract_definition),
      seq('__rotide_adjoint__', $.adjoint_expression),
      seq('__rotide_broadcast__', $.broadcast_call_expression),
      seq('__rotide_compound__', $.compound_statement),
      seq('__rotide_do__', $.do_clause),
      seq('__rotide_import_alias__', $.import_alias),
      seq('__rotide_primitive__', $.primitive_definition),
      seq('__rotide_parametrized__', $.parametrized_type_expression),
      seq('__rotide_open_tuple__', $.open_tuple),
      seq('__rotide_selected_import__', $.selected_import),
      seq('__rotide_unary_typed__', $.unary_typed_expression),
      seq('__rotide_ellipsis__', '...'),
      seq('__rotide_outer__', 'outer'),
    ),

    abstract_definition: $ => seq('abstract', 'type', $.type_head, 'end'),
    primitive_definition: $ => seq('primitive', 'type', $.type_head, 'end'),
    adjoint_expression: $ => seq($.identifier, "'"),
    compound_statement: $ => seq('begin', optional($.block), 'end'),
    do_clause: $ => seq('do', optional($.block), 'end'),
    import_alias: $ => seq($.identifier, 'as', $.identifier),
    selected_import: $ => seq($.identifier, ':', $.identifier),
  },
});
