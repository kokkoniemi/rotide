/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

module.exports = grammar({
  name: 'php',

  extras: $ => [$.comment, /[\s\u00A0\u200B\u2060\uFEFF]/, $.text_interpolation],
  word: $ => $.name,

  externals: $ => [
    $._automatic_semicolon,
    $.encapsed_string_chars,
    $.encapsed_string_chars_after_variable,
    $.execution_string_chars,
    $.execution_string_chars_after_variable,
    $.encapsed_string_chars_heredoc,
    $.encapsed_string_chars_after_variable_heredoc,
    $._eof,
    $.heredoc_start,
    $.heredoc_end,
    $.nowdoc_string,
    $.sentinel_error,
  ],

  conflicts: $ => [
    [$.heredoc_body],
    [$._namespace_use_prefix_1, $._namespace_use_prefix_2,
      $._namespace_use_prefix_3, $._namespace_use_prefix_4],
    [$._namespace_use_prefix_2, $._namespace_use_prefix_3, $._namespace_use_prefix_4],
    [$._namespace_use_prefix_3, $._namespace_use_prefix_4],
  ],

  rules: {
    program: $ => seq(
      optional($.text),
      optional(seq($.php_tag, repeat($.statement))),
    ),
    php_tag: _ => /<\?([pP][hH][pP]|=)?/,
    php_end_tag: _ => '?>',
    text_interpolation: $ => seq($.php_end_tag, optional($.text), choice($.php_tag, $._eof)),
    text: _ => repeat1(choice(token(prec(-1, /</)), token(prec(1, /[^\s<][^<]*/)))),

    statement: $ => choice(
      $.namespace_definition,
      $.namespace_use_declaration,
      $.class_declaration,
      $.interface_declaration,
      $.trait_declaration,
      $.enum_declaration,
      $.function_definition,
      $.const_declaration,
      $.echo_statement,
      $.exit_statement,
      $.return_statement,
      $.foreach_statement,
      $.if_statement,
      $.while_statement,
      $.expression_statement,
      $.compound_statement,
      $.compatibility_marker,
    ),
    expression_statement: $ => seq($.expression, $._semicolon),
    compound_statement: $ => seq('{', repeat($.statement), '}'),

    namespace_definition: $ => seq(
      'namespace', field('name', $.namespace_name), choice($._semicolon, $.compound_statement),
    ),
    namespace_name: $ => prec.right(1, seq($.name, repeat(seq('\\', $.name)))),
    qualified_name: $ => prec.right(2, seq(
      field('prefix', seq(optional('\\'), optional($.namespace_name), '\\')),
      $.name,
    )),
    relative_name: $ => seq('namespace', '\\', $.name),
    namespace_use_declaration: $ => seq(
      'use', $.namespace_use_clause, repeat(seq(',', $.namespace_use_clause)), $._semicolon,
    ),
    namespace_use_clause: $ => seq(
      field('type', optional(choice('function', 'const'))),
      choice($.name, alias($._namespace_use_qualified_name, $.qualified_name)),
      optional(seq('as', field('alias', $.name))),
    ),
    _namespace_use_qualified_name: $ => choice(
      seq(field('prefix', alias($._namespace_use_prefix_1, $.namespace_name)), '\\', $.name),
      seq(field('prefix', alias($._namespace_use_prefix_2, $.namespace_name)), '\\', $.name),
      seq(field('prefix', alias($._namespace_use_prefix_3, $.namespace_name)), '\\', $.name),
      seq(field('prefix', alias($._namespace_use_prefix_4, $.namespace_name)), '\\', $.name),
    ),
    _namespace_use_prefix_1: $ => seq($.name),
    _namespace_use_prefix_2: $ => seq($.name, '\\', $.name),
    _namespace_use_prefix_3: $ => seq($.name, '\\', $.name, '\\', $.name),
    _namespace_use_prefix_4: $ => seq($.name, '\\', $.name, '\\', $.name, '\\', $.name),

    class_declaration: $ => seq(
      repeat($._modifier), 'class', field('name', $.name),
      optional(seq('extends', $._name)),
      optional(seq('implements', $._name, repeat(seq(',', $._name)))),
      field('body', $.declaration_list),
    ),
    interface_declaration: $ => seq('interface', field('name', $.name), $.declaration_list),
    trait_declaration: $ => seq('trait', field('name', $.name), $.declaration_list),
    enum_declaration: $ => seq('enum', field('name', $.name), $.declaration_list),
    declaration_list: $ => seq('{', repeat(choice(
      $.const_declaration, $.property_declaration, $.method_declaration,
    )), '}'),

    _modifier: $ => choice(
      $.abstract_modifier, $.final_modifier, $.readonly_modifier,
      $.static_modifier, $.visibility_modifier,
    ),
    abstract_modifier: _ => 'abstract',
    final_modifier: _ => 'final',
    readonly_modifier: _ => 'readonly',
    static_modifier: _ => 'static',
    visibility_modifier: _ => choice('public', 'protected', 'private'),

    const_declaration: $ => seq(
      repeat($._modifier), 'const', optional(field('type', $.type)),
      $.const_element, repeat(seq(',', $.const_element)), $._semicolon,
    ),
    const_element: $ => seq($.name, optional(seq('=', $.expression))),
    property_declaration: $ => seq(
      repeat1($._modifier), optional(field('type', $.type)),
      $.property_element, repeat(seq(',', $.property_element)), $._semicolon,
    ),
    property_element: $ => seq($.variable_name, optional(seq('=', $.expression))),

    method_declaration: $ => seq(
      repeat($._modifier), 'function', field('name', $.name),
      field('parameters', $.formal_parameters), optional($._return_type),
      choice(field('body', $.compound_statement), $._semicolon),
    ),
    function_definition: $ => seq(
      'function', field('name', $.name), field('parameters', $.formal_parameters),
      optional($._return_type), field('body', $.compound_statement),
    ),
    formal_parameters: $ => seq(
      '(', optional(seq($.simple_parameter, repeat(seq(',', $.simple_parameter)), optional(','))), ')',
    ),
    simple_parameter: $ => seq(
      repeat($._modifier), optional(field('type', $.type)), field('name', $.variable_name),
      optional(seq('=', field('default_value', $.expression))),
    ),
    _return_type: $ => seq(':', field('return_type', $.type)),

    type: $ => choice($.optional_type, $.named_type, $.primitive_type),
    optional_type: $ => seq('?', choice($.named_type, $.primitive_type)),
    named_type: $ => choice($.name, $.qualified_name, $.relative_name),
    primitive_type: _ => choice(
      'array', 'bool', 'callable', 'false', 'float', 'int', 'iterable', 'mixed',
      'null', 'object', 'string', 'true', 'void',
    ),
    cast_type: _ => choice('array', 'bool', 'float', 'int', 'object', 'string', 'unset'),

    echo_statement: $ => seq('echo', $.expression, repeat(seq(',', $.expression)), $._semicolon),
    exit_statement: $ => seq('exit', optional(seq('(', optional($.expression), ')')), $._semicolon),
    return_statement: $ => seq('return', optional($.expression), $._semicolon),
    foreach_statement: $ => seq(
      'foreach', '(', $.expression, 'as', optional(seq($.expression, '=>')),
      $.expression, ')', $.statement,
    ),
    if_statement: $ => prec.right(seq(
      'if', '(', $.expression, ')', $.statement,
      repeat(seq('elseif', '(', $.expression, ')', $.statement)),
      optional(seq('else', $.statement)),
    )),
    while_statement: $ => seq('while', '(', $.expression, ')', $.statement),

    expression: $ => choice(
      $.assignment_expression,
      $.binary_expression,
      $.object_creation_expression,
      $.function_call_expression,
      $.scoped_call_expression,
      $.member_call_expression,
      $.member_access_expression,
      $.array_creation_expression,
      $.parenthesized_expression,
      $.encapsed_string,
      $.string,
      $.heredoc,
      $.nowdoc,
      $.boolean,
      $.null,
      $.integer,
      $.float,
      $.variable_name,
      $._name,
    ),
    _atom: $ => choice(
      $.function_call_expression, $.scoped_call_expression, $.member_call_expression,
      $.member_access_expression, $.variable_name, $._name, $.encapsed_string,
      $.string, $.heredoc, $.nowdoc, $.boolean, $.null, $.integer, $.float,
    ),
    assignment_expression: $ => prec.right(1, seq(
      field('left', choice($.variable_name, $.member_access_expression)),
      '=', field('right', $.expression),
    )),
    binary_expression: $ => prec.left(2, seq(
      field('left', $._atom),
      field('operator', choice('.', '??', '+', '-', '*', '/', '>', '<', '==', '===')),
      field('right', $.expression),
    )),
    parenthesized_expression: $ => seq('(', $.expression, ')'),

    object_creation_expression: $ => prec.right(seq(
      'new', choice($.name, $.qualified_name, $.relative_name), optional($.arguments),
    )),
    function_call_expression: $ => prec(8, seq(
      field('function', choice($.name, $.qualified_name, $.relative_name)),
      field('arguments', $.arguments),
    )),
    scoped_call_expression: $ => prec(8, seq(
      field('scope', choice($.name, $.qualified_name, $.relative_name)),
      '::', field('name', $.name), field('arguments', $.arguments),
    )),
    member_call_expression: $ => prec.left(9, seq(
      field('object', choice($.variable_name, $.function_call_expression, $.scoped_call_expression,
        $.member_call_expression, $.member_access_expression)),
      '->', field('name', $.name), field('arguments', $.arguments),
    )),
    member_access_expression: $ => prec.left(9, seq(
      field('object', choice($.variable_name, $.member_call_expression, $.member_access_expression)),
      '->', field('name', choice($.name, $.variable_name)),
    )),
    arguments: $ => seq(
      '(', optional(seq($.argument, repeat(seq(',', $.argument)), optional(','))), ')',
    ),
    argument: $ => seq(optional(seq(field('name', $.name), ':')), $.expression),
    array_creation_expression: $ => choice(
      seq('array', $.arguments),
      seq('[', optional(seq($.expression, repeat(seq(',', $.expression)), optional(','))), ']'),
    ),

    encapsed_string: $ => prec.right(seq(
      '"', repeat(choice(
        $.escape_sequence,
        seq($.variable_name, alias($.encapsed_string_chars_after_variable, $.string_content)),
        alias($.encapsed_string_chars, $.string_content),
        seq('{', $.member_access_expression, '}'),
        seq('{', $.variable_name, '}'),
      )), '"',
    )),
    string: $ => seq("'", optional($.string_content), "'"),
    string_content: _ => token.immediate(/([^'\\]|\\.)+/),
    escape_sequence: _ => token.immediate(seq('\\', /./)),

    _interpolated_string_body_heredoc: $ => repeat1(choice(
      $.escape_sequence,
      seq(
        $.variable_name,
        alias($.encapsed_string_chars_after_variable_heredoc, $.string_content),
      ),
      alias($.encapsed_string_chars_heredoc, $.string_content),
    )),
    heredoc_body: $ => seq(
      $._new_line,
      repeat1(prec.right(seq(
        optional($._new_line),
        $._interpolated_string_body_heredoc,
      ))),
    ),
    heredoc: $ => seq(
      token('<<<'), optional('"'), field('identifier', $.heredoc_start), optional(token.immediate('"')),
      choice(seq(field('value', $.heredoc_body), $._new_line),
        field('value', optional($.heredoc_body))),
      field('end_tag', $.heredoc_end),
    ),
    nowdoc_body: $ => seq($._new_line, repeat1($.nowdoc_string)),
    nowdoc: $ => prec.right(20, seq(
      token('<<<'), "'", field('identifier', $.heredoc_start), token.immediate("'"),
      choice(seq(field('value', $.nowdoc_body), $._new_line),
        field('value', optional($.nowdoc_body))),
      field('end_tag', $.heredoc_end),
    )),
    _new_line: _ => /\r?\n|\r/,

    variable_name: $ => seq('$', $.name),
    _name: $ => choice($.name, $.qualified_name, $.relative_name),
    name: _ => /[_a-zA-Z\u0080-\uffff][_a-zA-Z\u0080-\uffff\d]*/,
    boolean: _ => token(prec(2, /true|false/i)),
    null: _ => token(prec(2, /null/i)),
    integer: _ => token(choice(/[1-9]\d*(_\d+)*/, /0[xX][0-9a-fA-F_]+/, /0[bB][01_]+/, /0[oO]?[0-7_]*/)),
    float: _ => /\d+\.\d+([eE][+-]?\d+)?/,
    comment: _ => token(choice(seq('//', /[^\r\n]*/), seq('#', /[^\r\n]*/),
      seq('/*', /[^*]*\*+([^/*][^*]*\*+)*/, '/'))),
    _semicolon: $ => choice($._automatic_semicolon, ';'),

    compatibility_marker: $ => choice(
      seq('__rotide_const__', $.const_declaration, $.const_element),
      seq('__rotide_relative__', $.relative_scope),
      seq('__rotide_cast__', $.cast_type),
      seq('__rotide_static__', $.function_static_declaration),
      seq('__rotide_list__', $.list_literal),
      seq('__rotide_keywords__', choice(
        'and', 'as', 'break', 'case', 'catch', 'class', 'clone', 'const', 'continue',
        'declare', 'default', 'do', 'echo', 'else', 'elseif', 'enddeclare', 'endfor',
        'endforeach', 'endif', 'endswitch', 'endwhile', 'enum', 'exit', 'extends',
        'finally', 'fn', 'for', 'foreach', 'function', 'global', 'goto', 'if',
        'implements', 'include', 'include_once', 'instanceof', 'insteadof',
        'interface', 'match', 'namespace', 'new', 'or', 'print', 'require',
        'require_once', 'return', 'switch', 'throw', 'trait', 'try', 'use', 'while',
        'xor', 'yield', 'yield from',
      )),
    ),
    relative_scope: _ => choice('self', 'parent', 'static'),
    function_static_declaration: $ => seq('static', $.variable_name, ';'),
    list_literal: $ => seq('list', $.arguments),
  },
});
