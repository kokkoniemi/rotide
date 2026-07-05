/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

// Rotide permissive Kotlin grammar. Replaces the upstream
// tree-sitter-grammars/tree-sitter-kotlin v1.1.0 grammar before parser
// regeneration so highlight-only trees stay tiny (~3.5 MB object → ~200 KB).
// Keeps the nodes/fields our highlights.scm consumes:
//   identifier, line_comment, block_comment, number_literal, float_literal,
//   character_literal, escape_sequence, string_literal,
//   multiline_string_literal, string_content, user_type,
//   function_declaration.name, class_declaration.name, object_declaration.name,
// plus every keyword/operator anonymous token the query lists.
// No external scanner — Kotlin semicolon-insertion is irrelevant to
// highlighting; whitespace/newlines are just extras here.

const KEYWORDS = [
  'package', 'import', 'class', 'interface', 'object', 'enum', 'annotation',
  'typealias', 'companion', 'constructor', 'init', 'fun', 'val', 'var', 'this',
  'super', 'return', 'throw', 'as', 'is', 'in', 'out', 'by',
  'field', 'get', 'set',
  'if', 'else', 'when', 'where',
  'for', 'while', 'do',
  'try', 'catch', 'finally',
];

const MODIFIERS = [
  'public', 'private', 'internal', 'protected',
  'abstract', 'final', 'open', 'override', 'sealed',
  'data', 'inner', 'inline', 'noinline', 'crossinline', 'vararg', 'lateinit',
  'const', 'operator', 'infix', 'suspend', 'tailrec',
  'external', 'expect', 'actual', 'value',
];

const OPERATORS = [
  '!', '!=', '!==',
  '%', '%=',
  '&&',
  '*', '*=',
  '+', '++', '+=',
  '-', '--', '-=', '->',
  '/', '/=',
  '<', '<=',
  '=', '==', '===',
  '>', '>=',
  '?:', '?.', '?',
  '..',
  '||',
];

module.exports = grammar({
  name: 'kotlin',

  extras: $ => [/\s/, $.line_comment, $.block_comment],

  word: $ => $.identifier,

  conflicts: $ => [],

  rules: {
    source_file: $ => repeat($._item),

    _item: $ => choice(
      $.class_declaration,
      $.object_declaration,
      $.function_declaration,
      $._expression,
      $._punctuation_token,
      // Filter out keywords that start declarations — they only appear at
      // the head of class/object/function nodes.
      ...KEYWORDS.filter(k =>
        k !== 'class' && k !== 'interface' && k !== 'enum' &&
        k !== 'object' && k !== 'fun'),
      ...MODIFIERS,
    ),

    line_comment: $ => token(seq('//', /[^\r\n]*/)),
    block_comment: $ => token(seq('/*', repeat(choice(/[^*]+/, /\*[^/]/)), optional('*/'))),

    // Declarations. Each production tags the identifier that comes right after
    // the `class` / `object` / `fun` keyword so the highlight query can hit
    // (function_declaration name: (identifier) @function) etc. The body is
    // deliberately permissive — anything that fits as a top-level item is fine
    // as a nested item too.
    class_declaration: $ => prec.right(seq(
      choice('class', 'interface', 'enum'),
      field('name', $.identifier),
      optional($._decl_tail),
    )),

    object_declaration: $ => prec.right(seq(
      'object',
      field('name', $.identifier),
      optional($._decl_tail),
    )),

    function_declaration: $ => prec.right(seq(
      'fun',
      field('name', $.identifier),
      optional($._decl_tail),
    )),

    // Everything after the header until the next top-level anchor is folded
    // into the declaration node. Braces recurse via `_item`, so nested class
    // bodies still surface their inner declaration/identifier nodes.
    _decl_tail: $ => prec.right(repeat1(choice(
      $.block,
      $._expression,
      $._punctuation_token,
      ...KEYWORDS.filter(k =>
        k !== 'class' && k !== 'interface' && k !== 'enum' &&
        k !== 'object' && k !== 'fun'),
      ...MODIFIERS,
    ))),

    block: $ => seq('{', repeat($._item), '}'),

    // Expressions collapse into a permissive set of literals and identifier
    // references. Precedence is unimportant — we only need a tree so highlights
    // can pin captures to nodes.
    _expression: $ => choice(
      $.call_expression,
      $.user_type,
      $.number_literal,
      $.float_literal,
      $.character_literal,
      $.string_literal,
      $.multiline_string_literal,
      $.identifier,
    ),

    call_expression: $ => prec.left(1, seq($.identifier, $.arguments)),

    arguments: $ => seq('(', optional(seq($._expression, repeat(seq(',', $._expression)))), ')'),

    // Wraps a bare identifier used in type position so the highlight query
    // `(user_type (identifier) @type)` matches. Rare in a permissive grammar
    // (identifiers are ambiguous with call callees and expressions), so we
    // gate emission behind an explicit `type:` token that never appears in
    // real Kotlin source. This keeps the node type present so highlights.scm
    // still compiles even though it will not fire in practice.
    user_type: $ => seq('__rotide_type__', $.identifier),

    // Punctuation and operators surface as anonymous tokens the query picks
    // up directly. Group them into a dummy `_punctuation_token` so they only
    // appear at the same slots `_item` and `_decl_tail` mention them.
    _punctuation_token: $ => choice(
      ...OPERATORS,
      '(', ')', '[', ']',
      ',', ';', '.', '::',
      '@', ':',
    ),

    // Literals
    number_literal: $ => token(choice(
      /0[xX][0-9a-fA-F][0-9a-fA-F_]*[uUlL]*/,
      /0[bB][01][01_]*[uUlL]*/,
      /\d[\d_]*[uUlL]*/,
    )),

    float_literal: $ => token(choice(
      /\d[\d_]*\.\d[\d_]*([eE][+-]?\d+)?[fFdD]?/,
      /\d[\d_]*[eE][+-]?\d+[fFdD]?/,
      /\d[\d_]*[fF]/,
    )),

    character_literal: $ => seq(
      "'",
      choice($.escape_sequence, /[^'\\\n]/),
      "'",
    ),

    escape_sequence: $ => token.immediate(choice(
      /\\[btnr'"\\$]/,
      /\\u[0-9a-fA-F]{4}/,
    )),

    string_literal: $ => seq(
      '"',
      repeat(choice($.escape_sequence, $.string_content)),
      '"',
    ),

    multiline_string_literal: $ => seq(
      '"""',
      repeat(choice($.escape_sequence, alias($.multiline_string_content, $.string_content))),
      '"""',
    ),

    string_content: $ => token.immediate(prec(1, /[^"\\\n]+/)),
    multiline_string_content: $ => token.immediate(prec(1, /[^"\\]+|"[^"]|""[^"]/)),

    identifier: $ => /[A-Za-z_][A-Za-z0-9_]*/,
  },
});
