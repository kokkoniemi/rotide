/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

const C_KEYWORDS = [
  'break', 'case', 'const', 'continue', 'default', 'do', 'else', 'enum',
  'extern', 'for', 'if', 'inline', 'return', 'sizeof', 'static', 'struct',
  'switch', 'typedef', 'union', 'volatile', 'while',
];

const GLSL_QUALIFIERS = [
  'in', 'out', 'inout', 'uniform', 'shared', 'layout', 'attribute', 'varying',
  'buffer', 'coherent', 'readonly', 'writeonly', 'precision', 'highp',
  'mediump', 'lowp', 'centroid', 'sample', 'patch', 'smooth', 'flat',
  'noperspective', 'invariant', 'precise', 'subroutine',
];

const OPERATORS = [
  '--', '-', '-=', '->',
  '=', '!=', '==',
  '*', '&', '&&',
  '+', '++', '+=',
  '<', '>', '||',
];

const PREPROC_KEYWORDS = ['#define', '#elif', '#else', '#endif', '#if', '#ifdef', '#ifndef', '#include'];

const PRIMITIVE_TYPES = [
  'void', 'bool', 'int', 'uint', 'float', 'double',
  'char',
  'vec2', 'vec3', 'vec4',
  'bvec2', 'bvec3', 'bvec4',
  'ivec2', 'ivec3', 'ivec4',
  'uvec2', 'uvec3', 'uvec4',
  'dvec2', 'dvec3', 'dvec4',
  'mat2', 'mat3', 'mat4',
  'mat2x2', 'mat2x3', 'mat2x4',
  'mat3x2', 'mat3x3', 'mat3x4',
  'mat4x2', 'mat4x3', 'mat4x4',
  'dmat2', 'dmat3', 'dmat4',
  'sampler1D', 'sampler2D', 'sampler3D', 'samplerCube',
  'sampler2DShadow', 'sampler2DArray', 'sampler2DArrayShadow',
  'image1D', 'image2D', 'image3D', 'imageCube',
  'atomic_uint',
];

const SIZED_TYPE_SPECIFIERS = ['short', 'long', 'signed', 'unsigned'];

module.exports = grammar({
  name: 'glsl',

  extras: $ => [/\s/, $.comment],

  word: $ => $.identifier,

  conflicts: $ => [
    [$.call_expression, $.function_declarator],
  ],

  rules: {
    translation_unit: $ => repeat($._item),

    _item: $ => choice(
      $.function_declarator,
      $.preproc_function_def,
      $.preproc_directive_line,
      $.extension_storage_class,
      $.field_expression,
      $.call_expression,
      $.field_identifier,
      $.statement_identifier,
      $.type_identifier,
      $.primitive_type,
      $.sized_type_specifier,
      $.string_literal,
      $.system_lib_string,
      $.number_literal,
      $.char_literal,
      $.null,
      $.identifier,
      $._punctuation_token,
      ...C_KEYWORDS,
      ...GLSL_QUALIFIERS,
      ...PREPROC_KEYWORDS,
    ),

    comment: $ => token(choice(
      seq('//', /[^\r\n]*/),
      seq('/*', repeat(choice(/[^*]+/, /\*[^/]/)), optional('*/')),
    )),

    preproc_directive: $ => token(seq('#', /[a-z]+/)),

    preproc_directive_line: $ => prec.right(seq(
      $.preproc_directive,
      repeat(choice($.identifier, $.number_literal, $.string_literal, $.system_lib_string, $._punctuation_token)),
    )),

    preproc_function_def: $ => prec(2, seq(
      '#define',
      field('name', $.identifier),
      '(',
      optional(seq($.identifier, repeat(seq(',', $.identifier)))),
      ')',
    )),

    extension_storage_class: $ => prec(2, seq(
      '#extension',
      $.identifier,
      ':',
      $.identifier,
    )),

    call_expression: $ => prec.left(1, seq(
      field('function', choice($.identifier, $.field_expression)),
      '(',
      optional(seq(
        choice($.identifier, $.number_literal, $.string_literal),
        repeat(seq(',', choice($.identifier, $.number_literal, $.string_literal))),
      )),
      ')',
    )),

    field_expression: $ => prec.left(1, seq(
      $.identifier,
      '.',
      field('field', $.field_identifier),
    )),

    function_declarator: $ => prec(2, seq(
      field('declarator', $.identifier),
      '(',
      optional(seq($.identifier, repeat(seq(',', $.identifier)))),
      ')',
    )),

    type_identifier: $ => /[A-Za-z_][A-Za-z0-9_]*_t/,

    field_identifier: $ => token(prec(-1, /_ROTIDE_field_[A-Za-z0-9_]*/)),
    statement_identifier: $ => token(prec(-1, /_ROTIDE_stmt_[A-Za-z0-9_]*/)),

    primitive_type: $ => choice(...PRIMITIVE_TYPES),
    sized_type_specifier: $ => choice(...SIZED_TYPE_SPECIFIERS),

    _punctuation_token: $ => choice(
      ...OPERATORS,
      '(', ')', '[', ']', '{', '}',
      ',', ';', '.', ':',
    ),

    // Literals
    null: $ => 'NULL',

    number_literal: $ => token(choice(
      /0[xX][0-9a-fA-F]+[uUlLfF]*/,
      /\d+\.\d*(?:[eE][+-]?\d+)?[fFlLdD]?/,
      /\.\d+(?:[eE][+-]?\d+)?[fFlLdD]?/,
      /\d+[eE][+-]?\d+[fFlLdD]?/,
      /\d+[uUlLfF]*/,
    )),

    char_literal: $ => seq(
      "'",
      choice($.escape_sequence, /[^'\\\n]/),
      "'",
    ),

    escape_sequence: $ => token.immediate(/\\(?:['"?abfnrtv\\]|[0-7]{1,3}|x[0-9a-fA-F]+|u[0-9a-fA-F]{4}|U[0-9a-fA-F]{8})/),

    string_literal: $ => seq(
      '"',
      repeat(choice($.escape_sequence, $.string_content)),
      '"',
    ),

    system_lib_string: $ => token(seq('<', /[^>\r\n]*/, '>')),

    string_content: $ => token.immediate(prec(1, /[^"\\\n]+/)),

    identifier: $ => /[A-Za-z_][A-Za-z0-9_]*/,
  },
});
