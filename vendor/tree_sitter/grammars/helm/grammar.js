/**
 * Minimal Go template (Helm) grammar for RotIDE syntax highlighting.
 *
 * Authored in-repo (NOT vendored from an upstream repo) so RotIDE does not
 * depend on an unmaintained third-party grammar. It parses Helm / Go
 * templates as a flat sequence of literal text and `{{ ... }}` actions. The
 * action body is tokenized loosely -- enough to color keywords, functions,
 * fields, variables, strings, numbers, and comments, but not a full Go
 * template expression tree. Keeping the body flat means the grammar is
 * LR(1)-clean (no conflicts) and needs no external scanner, so the vendored
 * grammar is parser.c only.
 *
 * The literal `text` between actions is injected as YAML by
 * src/language/queries/helm/injections.scm so the surrounding chart body still
 * highlights.
 *
 * Regenerate with: scripts/refresh_tree_sitter_vendor.sh --grammar helm
 */

module.exports = grammar({
  name: 'helm',

  // Reserve the control keywords out of the identifier token via automatic
  // keyword extraction.
  word: $ => $.identifier,

  rules: {
    template: $ => repeat($._node),

    _node: $ => choice(
      $.comment,
      $.action,
      $.text,
    ),

    // Any run of characters that is not the start of a `{{` action. The
    // alternation consumes a lone `{` only when it is not followed by another
    // `{`, so `{{` always starts an action while YAML flow braces (`{}`) stay
    // literal text. prec(-1) keeps the action delimiters winning on ties.
    text: $ => token(prec(-1, /([^{]|\{[^{])+/)),

    // {{/* ... */}} template comment, optionally trimmed (`{{- /* */ -}}`).
    comment: $ => token(seq(
      '{{',
      optional('-'),
      /[ \t\r\n]*/,
      '/*',
      /[^*]*\*+([^/*][^*]*\*+)*/,
      '/',
      /[ \t\r\n]*/,
      optional('-'),
      '}}',
    )),

    action: $ => seq(
      choice('{{-', '{{'),
      repeat($._expression),
      choice('-}}', '}}'),
    ),

    _expression: $ => choice(
      $.keyword,
      $.boolean,
      $.nil,
      $.number,
      $.string,
      $.variable,
      $.field,
      $.dot,
      $.identifier,
      $.operator,
      '(',
      ')',
      ',',
    ),

    keyword: $ => choice(
      'if',
      'else',
      'end',
      'range',
      'with',
      'define',
      'template',
      'block',
      'break',
      'continue',
    ),

    boolean: $ => choice('true', 'false'),

    nil: $ => 'nil',

    // `.Foo` or `.Foo.Bar.Baz` -- a field/selector chain into the value tree.
    field: $ => token(seq(
      '.',
      /[a-zA-Z_][a-zA-Z0-9_]*/,
      repeat(seq('.', /[a-zA-Z_][a-zA-Z0-9_]*/)),
    )),

    // Bare `.` = the current pipeline context (e.g. `{{ toYaml . }}`).
    dot: $ => '.',

    // `$` (root context) or `$name` (a defined variable).
    variable: $ => token(seq('$', optional(/[a-zA-Z_][a-zA-Z0-9_]*/))),

    identifier: $ => /[a-zA-Z_][a-zA-Z0-9_]*/,

    number: $ => token(choice(
      /0[xX][0-9a-fA-F]+/,
      /[0-9]+(\.[0-9]+)?([eE][-+]?[0-9]+)?/,
    )),

    string: $ => choice(
      $._interpreted_string,
      $._raw_string,
    ),
    _interpreted_string: $ => token(seq('"', repeat(choice(/[^"\\]/, /\\./)), '"')),
    _raw_string: $ => token(seq('`', /[^`]*/, '`')),

    operator: $ => choice('|', ':=', '='),
  },
});
