/// <reference types="tree-sitter-cli/dsl" />
// @ts-check
const JavaScript = require('../node_modules/tree-sitter-javascript/grammar');

module.exports = function defineGrammar(dialect) {
  if (dialect !== 'typescript' && dialect !== 'tsx') {
    throw new Error(`Unknown dialect ${dialect}`);
  }

  return grammar(JavaScript, {
    name: dialect,

    externals: ($, previous) => previous.concat([
      $._function_signature_automatic_semicolon,
      $.__error_recovery,
    ]),

    supertypes: ($, previous) => previous.concat([
      $.type,
      $.primary_type,
    ]),

    precedences: ($, previous) => previous.concat([
      ['call', 'instantiation', 'unary', 'binary', $.arrow_function],
      [$.union_type, $.function_type, 'binary'],
      [$.array_type, $.pattern, $.type],
      [$.primary_type, $.statement_block, 'object'],
    ]),

    conflicts: ($, previous) => previous.concat([
      [$.call_expression, $.binary_expression],
      [$.call_expression, $.binary_expression, $.unary_expression],
      [$.call_expression, $.binary_expression, $.update_expression],
      [$.call_expression, $.binary_expression, $.await_expression],
      [$.primary_expression, $._parameter_name],
      [$.primary_expression, $.primary_type],
      [$.primary_expression, $.pattern, $.primary_type],
      [$.pattern, $.primary_type],
      [$.object, $.object_type],
      [$.object, $.object_pattern, $.object_type],
      [$.object, $.object_pattern, $._property_name],
      [$.object_pattern, $.object_type],
      [$.array, $.array_pattern, $.tuple_type],
      [$.array, $.tuple_type],
      [$.array_pattern, $.tuple_type],
      [$.jsx_opening_element, $.type_parameter],
      [$.unary_expression, $.predefined_type],
      [$.primary_expression, $.generic_type],
      [$.nested_identifier, $.nested_type_identifier, $.primary_expression],
      [$._call_signature, $.function_type],
    ]),

    inline: ($, previous) => previous
      .filter((rule) => !['_formal_parameter', '_call_signature'].includes(rule.name))
      .concat([$._type_identifier]),

    rules: {
      declaration: ($, previous) => choice(
        previous,
        $.interface_declaration,
        $.type_alias_declaration,
        $.enum_declaration,
        prec('declaration', $.internal_module),
        $.compatibility_marker,
      ),

      expression: ($, previous) => dialect === 'typescript'
        ? choice(...previous.members.filter((member) => member.name !== '_jsx_element'))
        : previous,

      variable_declarator: $ => seq(
        field('name', choice($.identifier, $._destructuring_pattern)),
        field('type', optional($.type_annotation)),
        optional($._initializer),
      ),

      call_expression: $ => choice(
        prec('call', seq(
          field('function', choice($.expression, $.import)),
          field('type_arguments', optional($.type_arguments)),
          field('arguments', $.arguments),
        )),
        prec('template_call', seq(
          field('function', choice($.primary_expression, $.new_expression)),
          field('arguments', $.template_string),
        )),
        prec('member', seq(
          field('function', $.primary_expression),
          field('optional_chain', $.optional_chain),
          field('type_arguments', optional($.type_arguments)),
          field('arguments', $.arguments),
        )),
      ),

      new_expression: $ => prec.right('new', seq(
        'new',
        field('constructor', choice($.primary_expression, $.new_expression)),
        field('type_arguments', optional($.type_arguments)),
        field('arguments', optional(prec.dynamic(1, $.arguments))),
      )),

      class_declaration: $ => prec.left('declaration', seq(
        'class',
        field('name', $._type_identifier),
        field('type_parameters', optional($.type_parameters)),
        optional($.class_heritage),
        field('body', $.class_body),
        optional($._automatic_semicolon),
      )),

      class_heritage: $ => choice(
        seq($.extends_clause, optional($.implements_clause)),
        $.implements_clause,
      ),
      extends_clause: $ => seq('extends', $.expression),
      implements_clause: $ => seq('implements', commaSep1($.type)),

      class_body: $ => seq('{', repeat(choice(
        seq($.method_definition, optional($._semicolon)),
        seq($.public_field_definition, $._semicolon),
        $.class_static_block,
        ';',
      )), '}'),

      method_definition: $ => prec.left(seq(
        optional($.accessibility_modifier),
        optional('static'),
        optional($.override_modifier),
        optional('readonly'),
        optional('async'),
        optional(choice('get', 'set', '*')),
        field('name', $._property_name),
        optional('?'),
        $._call_signature,
        field('body', $.statement_block),
      )),

      public_field_definition: $ => seq(
        optional($.accessibility_modifier),
        optional('static'),
        optional($.override_modifier),
        optional('readonly'),
        field('name', $._property_name),
        optional(choice('?', '!')),
        field('type', optional($.type_annotation)),
        optional($._initializer),
      ),

      accessibility_modifier: _ => choice('public', 'private', 'protected'),
      override_modifier: _ => 'override',

      _formal_parameter: $ => choice($.required_parameter, $.optional_parameter),
      required_parameter: $ => seq(
        $._parameter_name,
        field('type', optional($.type_annotation)),
        optional($._initializer),
      ),
      optional_parameter: $ => seq(
        $._parameter_name,
        '?',
        field('type', optional($.type_annotation)),
        optional($._initializer),
      ),
      _parameter_name: $ => seq(
        optional($.accessibility_modifier),
        optional($.override_modifier),
        optional('readonly'),
        field('pattern', choice($.pattern, $.this)),
      ),
      _call_signature: $ => seq(
        field('type_parameters', optional($.type_parameters)),
        field('parameters', $.formal_parameters),
        field('return_type', optional($.type_annotation)),
      ),

      type_annotation: $ => seq(':', $.type),
      type_parameters: $ => seq('<', commaSep1($.type_parameter), optional(','), '>'),
      type_parameter: $ => seq(
        field('name', $._type_identifier),
        field('constraint', optional($.constraint)),
        field('value', optional($.default_type)),
      ),
      constraint: $ => seq('extends', $.type),
      default_type: $ => seq('=', $.type),
      type_arguments: $ => seq('<', commaSep1($.type), optional(','), '>'),

      type: $ => choice($.primary_type, $.function_type, $.union_type),
      primary_type: $ => choice(
        $.predefined_type,
        $._type_identifier,
        $.nested_type_identifier,
        $.generic_type,
        $.object_type,
        $.array_type,
        $.tuple_type,
        $.null,
        $.parenthesized_type,
      ),
      _type_identifier: $ => alias($.identifier, $.type_identifier),
      nested_type_identifier: $ => prec('member', seq(
        field('module', choice($.identifier, $.nested_identifier)),
        '.',
        field('name', $._type_identifier),
      )),
      generic_type: $ => prec('call', seq(
        field('name', choice($._type_identifier, $.nested_type_identifier)),
        field('type_arguments', $.type_arguments),
      )),
      predefined_type: _ => choice(
        'any', 'number', 'boolean', 'string', 'symbol', 'void', 'unknown', 'never', 'object',
      ),
      parenthesized_type: $ => seq('(', $.type, ')'),
      array_type: $ => seq($.primary_type, '[', ']'),
      tuple_type: $ => seq('[', commaSep($.type), optional(','), ']'),
      union_type: $ => prec.left(seq(optional($.type), '|', $.type)),
      function_type: $ => prec.left(seq(
        field('type_parameters', optional($.type_parameters)),
        field('parameters', $.formal_parameters),
        '=>',
        field('return_type', $.type),
      )),

      object_type: $ => seq('{', repeat(choice(
        seq($.property_signature, choice($._semicolon, ',')),
        seq($.method_signature, choice($._semicolon, ',')),
        ';',
      )), '}'),
      property_signature: $ => seq(
        optional('readonly'),
        field('name', $._property_name),
        optional('?'),
        field('type', optional($.type_annotation)),
      ),
      method_signature: $ => seq(
        field('name', $._property_name),
        optional('?'),
        $._call_signature,
      ),

      interface_declaration: $ => seq(
        'interface',
        field('name', $._type_identifier),
        field('type_parameters', optional($.type_parameters)),
        optional($.extends_type_clause),
        field('body', alias($.object_type, $.interface_body)),
      ),
      extends_type_clause: $ => seq(
        'extends', commaSep1(field('type', choice($._type_identifier, $.generic_type))),
      ),
      type_alias_declaration: $ => seq(
        'type',
        field('name', $._type_identifier),
        field('type_parameters', optional($.type_parameters)),
        '=',
        field('value', $.type),
        $._semicolon,
      ),
      enum_declaration: $ => seq(
        optional('const'), 'enum', field('name', $.identifier), field('body', $.enum_body),
      ),
      enum_body: $ => seq(
        '{', optional(seq(commaSep1(field('name', $._property_name)), optional(','))), '}',
      ),
      internal_module: $ => seq('namespace', field('name', $.identifier), $.statement_block),

      compatibility_marker: $ => seq(
        '__rotide_typescript__',
        choice(
          'abstract', 'declare', 'keyof', 'override', 'private', 'protected', 'public',
          'readonly', 'satisfies',
        ),
        $._semicolon,
      ),
    },
  });
};

function commaSep1(rule) {
  return seq(rule, repeat(seq(',', rule)));
}

function commaSep(rule) {
  return optional(commaSep1(rule));
}
