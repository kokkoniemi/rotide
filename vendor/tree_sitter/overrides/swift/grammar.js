/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

const DEC_DIGITS = token(sep1(/[0-9]+/, /_+/));
const HEX_DIGITS = token(sep1(/[0-9a-fA-F]+/, /_+/));
const OCT_DIGITS = token(sep1(/[0-7]+/, /_+/));
const BIN_DIGITS = token(sep1(/[01]+/, /_+/));
const REAL_EXPONENT = token(seq(/[eE]/, optional(/[+-]/), DEC_DIGITS));
const HEX_REAL_EXPONENT = token(seq(/[pP]/, optional(/[+-]/), DEC_DIGITS));
const LEXICAL_IDENTIFIER = /[_\p{XID_Start}][_\p{XID_Continue}]*/;
const RAW_STR_CONTENT_CHAR = choice(/[^"\\\r\n]/, /\\[^#\r\n]/, /"[^#"\\\r\n]/);
const RAW_STR_CONTENT = repeat(RAW_STR_CONTENT_CHAR);
const RAW_STR_CONTENT_NONEMPTY = repeat1(RAW_STR_CONTENT_CHAR);

const TOKEN_PRECS = {
  comment: -3,
  regex: -4,
};

const PRECS = {
  prefix: 16,
  navigation: 15,
  postfix: 13,
  call: 0,
  binary: 3,
  expr: -1,
  loop: 1,
  if: -1,
  switch: -1,
  do: -1,
  block: 2,
  lambda: 0,
};

module.exports = grammar({
  name: 'swift',

  extras: $ => [$.comment, $.multiline_comment, /\s/],

  conflicts: $ => [
    [$.call_suffix],
    [$._expression, $.call_expression],
    [$.value_argument],
    [$.pattern, $._primary_expression],
    [$._case_dot_pattern, $._primary_expression],
    [$.pattern, $._simple_user_type],
    [$._primary_expression, $._simple_user_type],
    [$.capture_list_item, $._primary_expression],
    [$._lambda_parameters, $._primary_expression],
    [$._lambda_parameters, $.value_arguments],
    [$.modifiers],
    [$._primary_expression],
    [$.throws, $._throws_clause],
    [$.user_type],
  ],

  rules: {
    source_file: $ => seq(optional($.shebang_line), optional($.statements)),

    shebang_line: $ => token(/#![^\r\n]*/),

    comment: $ => token(prec(TOKEN_PRECS.comment, seq('//', /.*/))),
    multiline_comment: $ =>
      token(prec(TOKEN_PRECS.comment, seq('/*', /[^*]*\*+([^/*][^*]*\*+)*/, '/'))),

    statements: $ => prec.right(seq(
      $._statement,
      repeat(seq($._semis, $._statement)),
      optional($._semis),
    )),

    _semis: $ => repeat1(choice('\n', ';')),

    _statement: $ => choice(
      seq(optional($.modifiers), $._declaration),
      $._labeled_statement,
      $._control_transfer_statement,
      $._throw_statement,
      $._expression,
      $.directive,
      $.diagnostic,
    ),

    _labeled_statement: $ => seq(
      optional($.statement_label),
      choice(
        $.for_statement,
        $.while_statement,
        $.repeat_while_statement,
        $.do_statement,
        $.if_statement,
        $.guard_statement,
        $.switch_statement,
      ),
    ),

    statement_label: $ => seq($.simple_identifier, token.immediate(':')),

    _control_transfer_statement: $ => prec.right(seq(
      choice('return', 'continue', 'break', 'yield'),
      optional($._expression),
    )),

    _throw_statement: $ => prec.right(seq($.throw_keyword, $._expression)),
    throw_keyword: $ => 'throw',

    _block: $ => prec(PRECS.block, seq('{', optional($.statements), '}')),

    if_statement: $ => prec.right(PRECS.if, seq(
      'if',
      $._condition_clause,
      $._block,
      optional(seq($.else, choice($._block, $.if_statement))),
    )),

    else: $ => 'else',

    guard_statement: $ => prec.right(PRECS.if, seq(
      'guard',
      $._condition_clause,
      $.else,
      $._block,
    )),

    _condition_clause: $ => sep1(field('condition', $._condition), ','),

    _condition: $ => choice(
      $._binding_condition,
      $._expression,
      $.availability_condition,
    ),

    _binding_condition: $ => seq(
      choice('case', 'let', 'var'),
      $.pattern,
      optional($._type_annotation),
      optional(seq('=', $._expression)),
    ),

    while_statement: $ => prec.right(PRECS.loop, seq(
      'while',
      $._condition_clause,
      $._block,
    )),

    repeat_while_statement: $ => prec.right(PRECS.loop, seq(
      'repeat',
      $._block,
      'while',
      $._condition_clause,
    )),

    for_statement: $ => prec.right(PRECS.loop, seq(
      'for',
      optional($.try_operator),
      optional('await'),
      field('item', $.pattern),
      optional($._type_annotation),
      'in',
      field('collection', $._expression),
      optional($.where_clause),
      $._block,
    )),

    where_clause: $ => prec.left(seq($.where_keyword, $._expression)),
    where_keyword: $ => 'where',

    do_statement: $ => prec.right(PRECS.do, seq(
      'do',
      $._block,
      repeat($.catch_block),
    )),

    catch_block: $ => seq(
      $.catch_keyword,
      optional($.pattern),
      optional($.where_clause),
      $._block,
    ),
    catch_keyword: $ => 'catch',

    switch_statement: $ => prec.right(PRECS.switch, seq(
      'switch',
      field('expr', $._expression),
      '{',
      repeat($.switch_entry),
      '}',
    )),

    switch_entry: $ => seq(
      choice(
        seq('case', sep1($._switch_pattern, ','),
          optional(seq($.where_keyword, $._expression))),
        $.default_keyword,
      ),
      ':',
      $.statements,
      optional('fallthrough'),
    ),
    default_keyword: $ => 'default',

    _switch_pattern: $ => choice(prec.dynamic(1, $.pattern), $._expression),

    availability_condition: $ => seq(
      token(choice('#available', '#unavailable')),
      '(',
      repeat(choice($.simple_identifier, $.integer_literal, $.real_literal, ',', '.', '*')),
      ')',
    ),

    directive: $ => token(seq(choice('#if', '#elseif', '#else', '#endif'), /[^\r\n]*/)),
    diagnostic: $ => token(seq('#', choice(
      seq('error', /[^\r\n]*/),
      seq('warning', /[^\r\n]*/),
      seq('sourceLocation', /[^\r\n]*/),
    ))),

    pattern: $ => prec.right(choice(
      $.wildcard_pattern,
      seq(choice('let', 'var'), $.pattern),
      $._case_dot_pattern,
      $.simple_identifier,
      $._tuple_pattern,
    )),

    wildcard_pattern: $ => '_',

    _case_dot_pattern: $ => prec.right(seq(
      optional($.user_type),
      '.',
      $.simple_identifier,
      optional($._tuple_pattern),
    )),

    _tuple_pattern: $ => seq(
      '(',
      sep1Opt(seq(optional(seq($.simple_identifier, ':')), $.pattern), ','),
      ')',
    ),

    _declaration: $ => choice(
      $.property_declaration,
      $.function_declaration,
      $.class_declaration,
      $.init_declaration,
      $.deinit_declaration,
      $.subscript_declaration,
      $.typealias_declaration,
      $.associatedtype_declaration,
      $.import_declaration,
      $.operator_declaration,
      $.precedence_group_declaration,
    ),

    import_declaration: $ => prec.right(seq(
      'import',
      optional(choice('typealias', 'struct', 'class', 'enum', 'protocol', 'let', 'var', 'func')),
      $.identifier,
    )),

    identifier: $ => prec.right(sep1($.simple_identifier, '.')),

    property_declaration: $ => prec.right(seq(
      optional('async'),
      choice('let', 'var'),
      sep1(seq(
        field('name', $.pattern),
        optional($._type_annotation),
        optional(choice(
          seq('=', $._expression, optional($.willset_didset_block)),
          $.willset_didset_block,
          $.computed_property,
        )),
      ), ','),
    )),

    willset_didset_block: $ => seq(
      '{',
      repeat1(choice($.willset_clause, $.didset_clause)),
      '}',
    ),
    willset_clause: $ => seq(
      'willSet',
      optional(seq('(', $.simple_identifier, ')')),
      $._block,
    ),
    didset_clause: $ => seq(
      'didSet',
      optional(seq('(', $.simple_identifier, ')')),
      $._block,
    ),

    computed_property: $ => seq(
      '{',
      optional(choice(
        repeat1(choice($.computed_getter, $.computed_setter, $.computed_modify)),
        $.statements,
      )),
      '}',
    ),
    computed_getter: $ => seq($.getter_specifier, optional($._block)),
    computed_setter: $ => seq(
      $.setter_specifier,
      optional(seq('(', $.simple_identifier, ')')),
      optional($._block),
    ),
    computed_modify: $ => seq($.modify_specifier, optional($._block)),

    getter_specifier: $ => prec.right(seq(
      optional($.mutation_modifier),
      'get',
      optional('async'),
      optional(choice($._throws_clause, $.throws)),
    )),
    setter_specifier: $ => seq(optional($.mutation_modifier), 'set'),
    modify_specifier: $ => seq(optional($.mutation_modifier), '_modify'),

    typealias_declaration: $ => prec.right(seq(
      'typealias',
      field('name', alias($.simple_identifier, $.type_identifier)),
      optional($.type_parameters),
      '=',
      $._type,
    )),

    associatedtype_declaration: $ => prec.right(seq(
      'associatedtype',
      field('name', alias($.simple_identifier, $.type_identifier)),
      optional(seq(':', $._type)),
      optional($.type_constraints),
      optional(seq('=', $._type)),
    )),

    _function_signature: $ => repeat1(choice(
      $.type_parameters,
      $._parameter_clause,
      'async',
      $._throws_clause,
      $.throws,
      $._return_type,
      $.type_constraints,
    )),

    function_declaration: $ => prec.right(seq(
      'func',
      field('name', choice($.simple_identifier, $._referenceable_operator)),
      optional($._function_signature),
      optional(field('body', $.function_body)),
    )),

    function_body: $ => $._block,

    init_declaration: $ => prec.right(seq(
      'init',
      optional(choice('?', $.bang)),
      optional($._function_signature),
      optional(field('body', $.function_body)),
    )),

    deinit_declaration: $ => prec.right(seq(
      'deinit',
      optional(field('body', $.function_body)),
    )),

    subscript_declaration: $ => prec.right(seq(
      'subscript',
      optional($._function_signature),
      $.computed_property,
    )),

    _parameter_clause: $ => seq(
      '(',
      optional(sep1Opt(seq(
        optional($.attribute),
        $.parameter,
        optional(seq('=', $._expression)),
      ), ',')),
      ')',
    ),

    parameter: $ => seq(
      optional(field('external_name', $.simple_identifier)),
      field('name', $.simple_identifier),
      $._type_annotation,
      optional('...'),
    ),

    _referenceable_operator: $ => choice(
      $.custom_operator,
      '==', '!=', '===', '!==', '<', '>', '<=', '>=',
      '+', '-', '*', '/', '%', '=', '+=', '-=', '*=', '/=', '%=',
      '++', '--', $.bang, '~', '|', '^', '<<', '>>', '&', '??',
    ),

    _type_declaration_tail: $ => repeat1(choice(
      $.type_parameters,
      seq(':', $._inheritance_specifiers),
      $.type_constraints,
    )),

    class_declaration: $ => prec.right(seq(
      optional('indirect'),
      field('declaration_kind',
        choice('class', 'struct', 'actor', 'enum', 'extension', 'protocol')),
      field('name', $._declaration_name),
      optional($._type_declaration_tail),
      field('body', $.class_body),
    )),

    _declaration_name: $ => sep1(alias($.simple_identifier, $.type_identifier), '.'),

    _inheritance_specifiers: $ => prec.left(sep1($.inheritance_specifier, choice(',', '&'))),
    inheritance_specifier: $ => prec.left(field('inherits_from', $.user_type)),

    class_body: $ => seq(
      '{',
      optional(seq(
        $._class_member,
        repeat(seq($._semis, $._class_member)),
        optional($._semis),
      )),
      '}',
    ),
    _class_member: $ => choice(
      seq(optional($.modifiers), choice($._declaration, $.enum_entry)),
      $.directive,
      $.diagnostic,
      $.protocol_function_declaration,
      $.protocol_property_declaration,
    ),

    protocol_function_declaration: $ => seq(
      '__rotide_protocol_function__',
      field('name', $.simple_identifier),
    ),
    protocol_property_declaration: $ => seq(
      '__rotide_protocol_property__',
      field('name', $.pattern),
    ),

    enum_entry: $ => prec.right(seq(
      optional('indirect'),
      'case',
      sep1(seq(
        field('name', $.simple_identifier),
        optional(choice(
          field('data_contents', $.tuple_type),
          seq('=', $._expression),
        )),
      ), ','),
    )),

    operator_declaration: $ => prec.right(seq(
      'operator',
      $._referenceable_operator,
      optional(seq(':', $.simple_identifier)),
    )),

    precedence_group_declaration: $ => seq(
      'precedencegroup',
      $.simple_identifier,
      '{',
      repeat(choice($.simple_identifier, ':', $.boolean_literal)),
      '}',
    ),

    type_parameters: $ => seq(
      '<',
      sep1Opt($.type_parameter, ','),
      optional($.type_constraints),
      '>',
    ),

    type_parameter: $ => seq(
      alias($.simple_identifier, $.type_identifier),
      optional($._type_annotation),
    ),

    type_constraints: $ => prec.right(seq($.where_keyword, sep1Opt($.type_constraint, ','))),
    type_constraint: $ => choice($.inheritance_constraint, $.equality_constraint),
    inheritance_constraint: $ => seq(
      field('constrained_type', $.identifier),
      $._type_annotation,
    ),
    equality_constraint: $ => seq(
      field('constrained_type', $.identifier),
      choice('==', '='),
      field('must_equal', $._type),
    ),

    modifiers: $ => repeat1(choice(
      $.attribute,
      $.visibility_modifier,
      $.member_modifier,
      $.function_modifier,
      $.mutation_modifier,
      $.property_modifier,
      $.inheritance_modifier,
      $.parameter_modifier,
      $.ownership_modifier,
      $.property_behavior_modifier,
    )),

    attribute: $ => prec.right(seq(
      '@',
      $.user_type,
      optional($._attribute_arguments),
    )),
    _attribute_arguments: $ => seq('(', repeat($._attribute_argument_token), ')'),
    _attribute_argument_token: $ => choice(
      $.simple_identifier,
      $.line_string_literal,
      $.integer_literal,
      $.real_literal,
      $.boolean_literal,
      ':', ',', '*', '.', '-', '<', '>',
      $._attribute_arguments,
    ),

    visibility_modifier: $ => token(seq(
      choice('public', 'private', 'internal', 'fileprivate', 'open', 'package'),
      optional(seq(/\s*/, '(', /\s*/, 'set', /\s*/, ')')),
    )),
    member_modifier: $ => prec.right(choice(
      'override', 'convenience', 'required',
      seq('nonisolated', optional(token.immediate(/\((unsafe|nonsending)\)/))),
    )),
    function_modifier: $ => token(choice('infix', 'postfix', 'prefix')),
    mutation_modifier: $ => token(choice('mutating', 'nonmutating')),
    property_modifier: $ => choice(
      token(choice('static', 'dynamic', 'optional', 'distributed')),
      prec(-1, 'class'),
    ),
    inheritance_modifier: $ => 'final',
    parameter_modifier: $ => token(choice('inout', '@escaping', '@autoclosure', 'borrowing', 'consuming')),
    ownership_modifier: $ => choice('weak', 'unowned', token(/unowned\((safe|unsafe)\)/)),
    property_behavior_modifier: $ => 'lazy',

    _type_annotation: $ => seq(':', field('type', $._type)),

    _return_type: $ => seq('->', $._type),

    _type: $ => prec.right(seq(
      repeat(choice('some', 'any', $.parameter_modifier, $.attribute)),
      $._core_type,
      repeat($._type_suffix),
    )),

    _core_type: $ => choice(
      $.user_type,
      $.tuple_type,
      $._bracket_type,
    ),

    _bracket_type: $ => seq('[', $._type, optional(seq(':', $._type)), ']'),

    _type_suffix: $ => prec.right(2, choice(
      '?',
      token.immediate('!'),
      seq('&', $._type),
      $._return_type,
      'async',
      $.throws,
    )),

    user_type: $ => sep1($._simple_user_type, '.'),
    _simple_user_type: $ => prec.right(seq(
      alias($.simple_identifier, $.type_identifier),
      optional($.type_arguments),
    )),

    type_arguments: $ => prec.left(seq('<', sep1Opt($._type, ','), '>')),

    tuple_type: $ => seq(
      '(',
      optional(sep1Opt(seq(
        optional(seq(choice($.simple_identifier, $.wildcard_pattern), ':')),
        $._type,
      ), ',')),
      ')',
    ),

    throws: $ => choice('throws', 'rethrows'),
    _throws_clause: $ => seq('throws', '(', $._type, ')'),

    _expression: $ => choice(
      $._postfix_expression,
      $.prefix_expression,
      $.binary_expression,
    ),

    _postfix_expression: $ => choice(
      $._primary_expression,
      $.navigation_expression,
      $.call_expression,
      $.postfix_expression,
    ),

    navigation_expression: $ => prec.left(PRECS.navigation, seq(
      field('target', choice($._postfix_expression, $.prefix_expression)),
      field('suffix', $.navigation_suffix),
    )),

    navigation_suffix: $ => seq(
      '.',
      field('suffix', choice($.simple_identifier, $.integer_literal)),
    ),

    call_expression: $ => prec.left(PRECS.call, prec.dynamic(1, seq(
      choice($._postfix_expression, $.prefix_expression),
      $.call_suffix,
    ))),

    call_suffix: $ => prec(PRECS.call, choice(
      $.value_arguments,
      prec.dynamic(-1, $._trailing_lambdas),
      seq($.value_arguments, $._trailing_lambdas),
    )),

    _trailing_lambdas: $ => prec.right(seq(
      $.lambda_literal,
      repeat(seq(field('name', $.simple_identifier), ':', $.lambda_literal)),
    )),

    value_arguments: $ => choice(
      seq('(', optional($._value_argument_list), ')'),
      seq('[', optional(choice(':', $._value_argument_list)), ']'),
    ),

    _value_argument_list: $ => sep1Opt($.value_argument, ','),

    value_argument: $ => prec.left(choice(
      repeat1(seq(field('reference_specifier', $.value_argument_label), ':')),
      seq(
        optional(seq(field('name', $.value_argument_label), ':')),
        field('value', $._expression),
        optional(seq(':', $._expression)),
      ),
    )),

    value_argument_label: $ => prec(1, $.simple_identifier),

    postfix_expression: $ => prec.left(PRECS.postfix, seq(
      $._postfix_expression,
      choice($.bang, alias($._immediate_quest, '?'), '++', '--'),
    )),

    _immediate_quest: $ => token.immediate('?'),
    bang: $ => '!',

    prefix_expression: $ => prec.left(PRECS.prefix, seq(
      field('operation', choice(
        '++', '--', '-', '+', $.bang, '&', '~', '.', $.custom_operator,
        $.try_operator, 'await',
      )),
      field('target', $._postfix_expression),
    )),

    binary_expression: $ => prec.left(PRECS.binary, seq(
      field('lhs', $._expression),
      choice(
        seq(
          field('op', choice(
            '*', alias(token(prec(TOKEN_PRECS.regex, '/')), '/'), '%',
            '+', '-',
            '<', '>', '<=', '>=', '<<', '>>',
            '==', '!=', '===', '!==',
            '&', '|', '^',
            '&&', '||', '??',
            '...', '..<',
            '=', '+=', '-=', '*=', '/=', '%=',
            $.custom_operator,
          )),
          field('rhs', $._expression),
        ),
        seq(choice($.as_operator, 'is'), field('type', $._type)),
        alias($._ternary_tail, $.ternary_expression),
      ),
    )),

    _ternary_tail: $ => seq(
      '?',
      field('if_true', $._expression),
      ':',
      field('if_false', $._expression),
    ),

    as_operator: $ => seq('as', optional(choice(token.immediate('?'), token.immediate('!')))),
    try_operator: $ => seq('try', optional(choice(token.immediate('!'), token.immediate('?')))),

    _primary_expression: $ => choice(
      $.simple_identifier,
     prec.dynamic(1, seq(alias($.simple_identifier, $.type_identifier), $.type_arguments)),
      $.self_expression,
      $.super_expression,
      $._basic_literal,
      $.value_arguments,
      $.lambda_literal,
      $.special_literal,
      $.playground_literal,
      $.selector_expression,
      $.key_path_string_expression,
      $.key_path_expression,
      $.external_macro_definition,
    ),

    self_expression: $ => 'self',
    super_expression: $ => 'super',

    lambda_literal: $ => prec(PRECS.lambda, seq(
      '{',
      optional($._lambda_signature),
      optional($.statements),
      '}',
    )),

    _lambda_signature: $ => seq(
      optional($.capture_list),
      optional($._lambda_parameters),
      optional('async'),
      optional(choice($._throws_clause, $.throws)),
      optional($._return_type),
      'in',
    ),

    capture_list: $ => seq('[', sep1Opt($.capture_list_item, ','), ']'),
    capture_list_item: $ => choice(
      field('name', $.self_expression),
      prec(PRECS.expr, seq(
        optional($.ownership_modifier),
        field('name', $.simple_identifier),
        optional(seq('=', $._expression)),
      )),
    ),

    _lambda_parameters: $ => choice(
      sep1($.simple_identifier, ','),
      $.value_arguments,
    ),

    key_path_expression: $ => prec.right(seq(
      '\\',
      optional($.user_type),
      repeat(seq('.', $.simple_identifier)),
    )),

    key_path_string_expression: $ => seq('#keyPath', $.value_arguments),

    // `getter: foo` / `setter: foo` fit value_argument's labeled form.
    selector_expression: $ => seq('#selector', $.value_arguments),

    external_macro_definition: $ => seq('#externalMacro', $.value_arguments),

    special_literal: $ => token(choice(
      '#file', '#fileID', '#filePath', '#line', '#column', '#function', '#dsohandle',
    )),

    playground_literal: $ => seq(
      token(choice('#colorLiteral', '#fileLiteral', '#imageLiteral')),
      $.value_arguments,
    ),

    _basic_literal: $ => choice(
      $.integer_literal,
      $.hex_literal,
      $.oct_literal,
      $.bin_literal,
      $.real_literal,
      $.boolean_literal,
      $._string_literal,
      $.regex_literal,
      'nil',
    ),

    integer_literal: $ => token(seq(optional(/[1-9]/), DEC_DIGITS)),
    hex_literal: $ => token(seq('0', /[xX]/, HEX_DIGITS)),
    oct_literal: $ => token(seq('0', /[oO]/, OCT_DIGITS)),
    bin_literal: $ => token(seq('0', /[bB]/, BIN_DIGITS)),
    real_literal: $ => token(choice(
      seq(DEC_DIGITS, REAL_EXPONENT),
      seq(optional(DEC_DIGITS), '.', DEC_DIGITS, optional(REAL_EXPONENT)),
      seq('0x', HEX_DIGITS, optional(seq('.', HEX_DIGITS)), HEX_REAL_EXPONENT),
    )),
    boolean_literal: $ => choice('true', 'false'),

    _string_literal: $ => choice(
      $.line_string_literal,
      $.multi_line_string_literal,
      $._raw_string_literal,
    ),

    line_string_literal: $ => seq(
      '"',
      repeat(choice(field('text', $.line_str_text), field('text', $.str_escaped_char), $._interpolation)),
      '"',
    ),
    line_str_text: $ => /[^\\"]+/,
    str_escaped_char: $ => token(choice(
      /\\[0\\tnr"'\n]/,
      /\\u\{[0-9a-fA-F]+\}/,
    )),

    multi_line_string_literal: $ => seq(
      '"""',
      repeat(choice(
        field('text', $.multi_line_str_text),
        field('text', $.str_escaped_char),
        '"',
        $._interpolation,
      )),
      '"""',
    ),
    multi_line_str_text: $ => /[^\\"]+/,

    _interpolation: $ => seq(
      '\\(',
      optional(alias($._value_argument_list, $.interpolated_expression)),
      ')',
    ),

    _raw_string_literal: $ => choice(
      field('text', $.raw_str_end_part),
      seq(
        field('text', $.raw_str_part),
        field('interpolation', $.raw_str_interpolation),
        repeat(seq(
          optional(field('text', alias($._raw_str_mid_part, $.raw_str_part))),
          field('interpolation', $.raw_str_interpolation),
        )),
        field('text', alias($._raw_str_tail_part, $.raw_str_end_part)),
      ),
    ),

    raw_str_part: $ => token(seq(/#+"/, RAW_STR_CONTENT)),
    raw_str_end_part: $ => token(seq(/#+"/, RAW_STR_CONTENT, /"#+/)),
    _raw_str_mid_part: $ => token(RAW_STR_CONTENT_NONEMPTY),
    _raw_str_tail_part: $ => token(seq(RAW_STR_CONTENT, /"#+/)),

    raw_str_interpolation: $ => seq(
      $.raw_str_interpolation_start,
      optional(alias($._value_argument_list, $.interpolated_expression)),
      ')',
    ),
    raw_str_interpolation_start: $ => /\\#*\(/,

    regex_literal: $ => choice(
      token(seq('#/', /[^\r\n]*/, '/#')),
      token(prec(TOKEN_PRECS.regex, seq(
        '/',
        token.immediate(/[^ \t\n]?[^/\n]*[^ \t\n/]/),
        token.immediate('/'),
      ))),
    ),

    simple_identifier: $ => choice(
      LEXICAL_IDENTIFIER,
      /`[^\r\n` ]*`/,
      /\$[0-9]+/,
      token(seq('$', LEXICAL_IDENTIFIER)),
    ),

    custom_operator: $ => token(/[+\-*%<>=!&|^~][+\-*\/%<>=!&|^~?]*/),
  },
});

function sep1(rule, separator) {
  return seq(rule, repeat(seq(separator, rule)));
}

function sep1Opt(rule, separator) {
  return seq(rule, repeat(seq(separator, rule)), optional(separator));
}
