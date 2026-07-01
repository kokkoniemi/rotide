/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

module.exports = grammar({
  name: 'ocaml',

  extras: $ => [/[ \t\f]/, $.comment],

  rules: {
    compilation_unit: $ => repeat($._item),

    _item: $ => choice(
      $.shebang,
      $.line_number_directive,
      $.directive,
      $.module_type_definition,
      $.module_definition,
      $.type_definition,
      $.value_definition,
      $.class_definition,
      $.external,
      $.value_specification,
      $.floating_attribute,
      $.expression_item,
      $.newline,
      $.compatibility_marker,
      $.keyword,
      $.raw_token,
    ),

    newline: $ => /\r?\n/,

    comment: $ => token(seq('(*', repeat(choice(/[^*]+/, /\*[^)]/)), optional('*)'))),

    shebang: $ => /#![^\r\n]*/,
    line_number_directive: $ => token(seq('#', /[0-9]+/, /[^\r\n]*/)),
    directive: $ => token(seq('#', /[A-Za-z_][A-Za-z0-9_']*/)),

    module_type_definition: $ => prec(12, seq(
      'module',
      'type',
      $.module_type_name,
      '=',
      'sig',
      repeat(choice($.value_specification, $.type_definition, $.newline)),
      'end',
    )),

    module_definition: $ => prec(12, seq(
      'module',
      $.module_binding,
    )),

    module_binding: $ => seq(
      $.module_name,
      '=',
      field('body', $.structure),
    ),

    structure: $ => seq('struct', repeat($._item), 'end'),

    value_specification: $ => seq(
      'val',
      $.value_name,
      ':',
      $._type,
    ),

    type_definition: $ => prec(11, seq(
      'type',
      $.type_binding,
    )),

    type_binding: $ => seq(
      field('name', $.type_constructor),
      '=',
      field('body', choice($.variant_declaration, $.record_declaration, $._type)),
    ),

    variant_declaration: $ => prec.right(seq(
      $.constructor_declaration,
      repeat(seq('|', $.constructor_declaration)),
    )),

    constructor_declaration: $ => seq(
      $.constructor_name,
      optional(seq('of', $._type)),
    ),

    record_declaration: $ => seq(
      '{',
      $.field_declaration,
      repeat(seq(';', $.field_declaration)),
      optional(';'),
      '}',
    ),

    field_declaration: $ => seq(
      optional('mutable'),
      $.field_name,
      ':',
      field('type', $._type),
    ),

    _type: $ => choice(
      $.function_type,
      $.type_variable,
      $.type_constructor_path,
      $.object_type,
    ),

    function_type: $ => prec.right(seq(
      field('domain', choice($.type_variable, $.type_constructor_path)),
      '->',
      field('codomain', $._type),
    )),

    object_type: $ => seq('<', optional($.type_constructor_path), '>'),

    value_definition: $ => prec(10, seq(
      choice('let', $.let_operator),
      optional('rec'),
      $.let_binding,
      repeat(seq(choice('and', $.let_and_operator), $.let_binding)),
    )),

    let_binding: $ => prec.right(seq(
      field('pattern', $.value_name),
      repeat($.parameter),
      '=',
      optional($.newline),
      field('body', $._expression),
    )),

    parameter: $ => field('pattern', $.value_pattern),

    let_expression: $ => prec.right(seq(
      $.value_definition,
      'in',
      optional($.newline),
      field('body', $._expression),
    )),

    _expression: $ => choice($.sequence_expression, $._simple_expression),

    sequence_expression: $ => prec.right(2, seq(
      $._simple_expression,
      repeat1(seq(';', optional($.newline), $._simple_expression)),
    )),

    _simple_expression: $ => choice(
      $.let_expression,
      $.match_expression,
      $.fun_expression,
      $.function_expression,
      $.for_expression,
      $.object_expression,
      $.application_expression,
      $.infix_expression,
      $.value_path,
      $.constructor_path,
      $.string,
      $.character,
      $.signed_number,
      $.number,
      $.boolean,
    ),

    application_expression: $ => prec.right(8, seq(
      field('function', $.value_path),
      repeat1(field('argument', $._argument)),
    )),

    _argument: $ => choice(
      $.string_argument_pair,
      $.value_path,
      $.constructor_path,
      $.string,
      $.character,
      $.signed_number,
      $.number,
      $.boolean,
    ),

    // Keep adjacent string/value arguments together so application ranges match upstream.
    string_argument_pair: $ => prec(10, seq($.string, $.value_path)),

    infix_expression: $ => prec.left(7, seq(
      field('left', choice($.value_path, $.number)),
      field('operator', choice(
        $.concat_operator,
        $.rel_operator,
        $.add_operator,
        $.mult_operator,
      )),
      field('right', choice($.value_path, $.number)),
    )),

    match_expression: $ => prec.right(seq(
      choice('match', $.match_operator),
      field('expression', $._simple_expression),
      'with',
      optional($.newline),
      optional('|'),
      $.match_case,
      repeat(seq($.newline, '|', $.match_case)),
    )),

    match_case: $ => seq(
      field('pattern', choice($.value_pattern, $.number, $.constructor_name)),
      '->',
      field('body', choice($.constructor_path, $.value_path, $.number)),
    ),

    fun_expression: $ => seq(
      'fun',
      repeat1($.parameter),
      '->',
      field('body', $._expression),
    ),

    function_expression: $ => prec.right(seq(
      'function',
      optional('|'),
      $.match_case,
      repeat(seq($.newline, '|', $.match_case)),
    )),

    for_expression: $ => seq(
      'for',
      field('name', $.value_pattern),
      '=',
      field('from', $._simple_expression),
      choice('to', 'downto'),
      field('to', $._simple_expression),
      'do',
      $._expression,
      'done',
    ),

    class_definition: $ => prec(11, seq('class', $.class_binding)),

    class_binding: $ => seq(
      $.class_name,
      repeat($.parameter),
      '=',
      field('body', $.object_expression),
    ),

    object_expression: $ => seq(
      'object',
      repeat(choice(
        $.instance_variable_definition,
        $.method_definition,
        $.newline,
      )),
      'end',
    ),

    instance_variable_definition: $ => seq(
      'val',
      optional('mutable'),
      $.instance_variable_name,
      '=',
      field('body', $._expression),
    ),

    method_definition: $ => seq(
      'method',
      $.method_name,
      '=',
      field('body', $._expression),
    ),

    class_function: $ => seq('__rotide_class_function__', $.class_binding),

    external: $ => prec.right(seq(
      'external', $.value_name, ':', $._type, '=', repeat1($.string),
    )),

    expression_item: $ => $._expression,

    value_path: $ => seq(
      optional(seq($.module_path, '.')),
      $.value_name,
    ),

    module_path: $ => $.module_name,

    type_constructor_path: $ => seq(
      optional(seq($.module_path, '.')),
      $.type_constructor,
    ),

    constructor_path: $ => seq(
      optional(seq($.module_path, '.')),
      $.constructor_name,
    ),

    field_path: $ => seq(optional(seq($.module_path, '.')), $.field_name),

    string: $ => choice(
      prec(2, seq(
        '"',
        repeat1(choice($.escape_sequence, $.conversion_specification, $.string_content)),
        '"',
      )),
      '""',
      // Upstream recovery includes the terminating newline in an unfinished string.
      prec(1, seq(
        '"',
        repeat1(choice($.escape_sequence, $.conversion_specification, $.string_content)),
        $.newline,
      )),
    ),

    string_content: $ => /[^"\\%\r\n]+/,

    escape_sequence: $ => token(seq('\\', /./)),

    conversion_specification: $ => /%[-+ #0-9.*]*[a-zA-Z%]/,

    character: $ => token(seq("'", choice(/[^'\\\r\n]/, /\\./), optional("'"))),

    quoted_string: $ => seq('{', optional($.string_content), '}'),

    number: $ => /[0-9][0-9_]*/,

    signed_number: $ => seq($.sign_operator, $.number),

    boolean: $ => choice('true', 'false'),

    value_name: $ => /[a-z_][A-Za-z0-9_']*/,
    value_pattern: $ => /[a-z_][A-Za-z0-9_']*/,
    type_variable: $ => /'[a-zA-Z_][A-Za-z0-9_']*/,
    label_name: $ => /[a-z_][A-Za-z0-9_']*/,
    field_name: $ => /[a-z_][A-Za-z0-9_']*/,
    instance_variable_name: $ => /[a-z_][A-Za-z0-9_']*/,
    method_name: $ => /[a-z_][A-Za-z0-9_']*/,
    class_name: $ => /[a-z_][A-Za-z0-9_']*/,
    class_type_name: $ => /[a-z_][A-Za-z0-9_']*/,
    type_constructor: $ => /[a-z_][A-Za-z0-9_']*/,
    module_name: $ => /[A-Z][A-Za-z0-9_']*/,
    module_type_name: $ => /[A-Za-z_][A-Za-z0-9_']*/,
    constructor_name: $ => /[A-Z][A-Za-z0-9_']*/,
    tag: $ => /`[A-Za-z_][A-Za-z0-9_']*/,

    prefix_operator: $ => /[!?~][!$%&*+\-./:<=>?@^|~]*/,
    sign_operator: $ => choice('+', '-'),
    pow_operator: $ => /\*\*[!$%&*+\-./:<=>?@^|~]*/,
    mult_operator: $ => /[*\/%][!$%&*+\-./:<=>?@^|~]*/,
    add_operator: $ => /[+-][!$%&*+\-./:<=>?@^|~]+/,
    concat_operator: $ => /[@^][!$%&*+\-./:<=>?@^|~]*/,
    rel_operator: $ => /[=<>|&$][!$%&*+\-./:<=>?@^|~]*/,
    and_operator: $ => /&[!$%&*+\-./:<=>?@^|~]*/,
    or_operator: $ => /[|][!$%&*+\-./:<=>?@^|~]*/,
    assign_operator: $ => /<-[!$%&*+\-./:<=>?@^|~]*/,
    hash_operator: $ => /#[!$%&*+\-./:<=>?@^|~]*/,
    indexing_operator: $ => /\.[!$%&*+\-./:<=>?@^|~]+/,
    let_operator: $ => /let[!$%&*+\-./:<=>?@^|~]+/,
    let_and_operator: $ => /and[!$%&*+\-./:<=>?@^|~]+/,
    match_operator: $ => /match[!$%&*+\-./:<=>?@^|~]+/,

    attribute: $ => seq('[@', $.attribute_id, optional($.attribute_payload), ']'),
    item_attribute: $ => seq('[@@', $.attribute_id, optional($.attribute_payload), ']'),
    floating_attribute: $ => seq('[@@@', $.attribute_id, optional($.attribute_payload), ']'),
    extension: $ => seq('[%', $.attribute_id, optional($.attribute_payload), ']'),
    item_extension: $ => seq('[%%', $.attribute_id, optional($.attribute_payload), ']'),
    quoted_extension: $ => seq('{%', $.attribute_id, optional($.attribute_payload), '}'),
    quoted_item_extension: $ => seq('{%%', $.attribute_id, optional($.attribute_payload), '}'),
    attribute_id: $ => /[A-Za-z_][A-Za-z0-9_.']*/,
    attribute_payload: $ => repeat1(choice($.value_path, $.string, $.number)),

    // Retain node types and literals required by the active upstream queries.
    compatibility_marker: $ => choice(
      $.class_function,
      $.quoted_string,
      $.attribute,
      $.item_attribute,
      $.extension,
      $.item_extension,
      $.quoted_extension,
      $.quoted_item_extension,
      seq('__rotide_label_name__', $.label_name),
      seq('__rotide_class_type_name__', $.class_type_name),
      $.tag,
      $.prefix_operator,
      $.pow_operator,
      $.and_operator,
      $.or_operator,
      $.assign_operator,
      $.hash_operator,
      $.indexing_operator,
      seq('__rotide_punctuation__', $.punctuation),
    ),

    keyword: $ => choice(
      'as', 'assert', 'begin', 'constraint', 'effect', 'else', 'exception',
      'functor', 'if', 'include', 'inherit', 'initializer', 'lazy', 'new',
      'nonrec', 'open', 'private', 'then', 'try', 'virtual', 'when', 'while',
    ),

    punctuation: $ => choice(
      ',', '.', ';', ':', '|', '~', '?', '!', '>', '&', ';;', ':>', '+=',
      ':=', '..', '(', ')', '[', ']', '[|', '|]', '[<', '[>', '%', '*', '#',
      '::', '<-',
    ),

    raw_token: $ => token(prec(-10, /[^\s]/)),
  },
});

