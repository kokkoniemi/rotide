/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

// RotIDE reduced Erlang highlight grammar.
//
// This is a highlight grammar, not a faithful Erlang AST. It preserves the node
// and field surface used by the pinned upstream highlights.scm, but replaces the
// full expression grammar and external scanner with shallow structured islands
// inside mostly flat token streams.

const sepBy1 = (sep, rule) => seq(rule, repeat(seq(sep, rule)));
const sepBy = (sep, rule) => optional(sepBy1(sep, rule));
const commaSep = rule => sepBy(',', rule);

module.exports = grammar({
  name: 'erlang',

  word: $ => $.atom,

  extras: $ => [
    /[\s\x80-\xA0]/,
    $.comment,
  ],

  inline: $ => [
    $._form,
    $._expr,
    $._atomish,
    $._operand,
    $._keyword,
    $._operator,
    $._binary_operator,
  ],

  rules: {
    source_file: $ => repeat(choice($._form, $._expr, $._query_only, '.', ';', ',')),

    _form: $ => choice(
      $.module_attribute,
      $.behaviour_attribute,
      $.export_attribute,
      $.import_attribute,
      $.compile_options_attribute,
      $.record_decl,
      $.spec,
      $.callback,
      $.wild_attribute,
      $.pp_ifdef,
      $.pp_ifndef,
      $.pp_define,
      $.pp_else,
      $.pp_elif,
      $.pp_endif,
      $.pp_include,
      $.pp_undef,
      $.function_clause,
      $.shebang,
    ),

    _query_only: $ => seq('__rotide_erlang_query_only__', $.type_name),

    shebang: _ => token(seq('#!', /.*/)),
    comment: _ => token(seq('%', /.*/)),

    module_attribute: $ => prec(2, seq('-', 'module', '(', field('name', $.atom), ')', '.')),
    behaviour_attribute: $ => prec(2, seq('-', choice('behaviour', 'behavior'), '(', field('name', $.atom), ')', '.')),
    export_attribute: $ => prec(2, seq('-', 'export', '(', $.list, ')', '.')),
    import_attribute: $ => prec(2, seq('-', 'import', '(', field('module', $.atom), ',', $.list, ')', '.')),
    compile_options_attribute: $ => prec(2, seq('-', 'compile', '(', field('options', choice($.tuple, $.list, $._expr)), ')', '.')),

    record_decl: $ => prec(2, seq('-', 'record', '(', field('name', choice($.atom, $.macro_call_expr)), ',', $.tuple, ')', '.')),
    record_field: $ => prec(2, seq(field('name', $.atom), choice(seq('=', $._expr), seq('::', $._expr)))),
    record_field_name: $ => field('name', $.atom),

    spec: $ => choice(
      prec(1, seq('-', 'spec', field('module', $.module), ':', field('fun', $.atom), repeat($._body_token), '.')),
      seq('-', 'spec', field('fun', $.atom), repeat($._body_token), '.'),
    ),
    callback: $ => seq('-', 'callback', field('fun', $.atom), repeat($._body_token), '.'),
    module: $ => prec(1, field('name', $.atom)),
    type_name: $ => field('name', $.atom),

    wild_attribute: $ => seq('-', field('name', $.attr_name), optional(seq('(', repeat($._body_token), ')')), '.'),
    attr_name: $ => seq(field('name', $.atom)),

    pp_ifdef: $ => seq('-', 'ifdef', '(', field('name', $._pp_name), ')', '.'),
    pp_ifndef: $ => seq('-', 'ifndef', '(', field('name', $._pp_name), ')', '.'),
    pp_define: $ => seq('-', 'define', '(', field('lhs', $.macro_lhs), optional(seq(',', repeat($._body_token))), ')', '.'),
    pp_else: _ => seq('-', 'else', '.'),
    pp_elif: $ => seq('-', 'elif', '(', repeat($._body_token), ')', '.'),
    pp_endif: _ => seq('-', 'endif', '.'),
    pp_include: $ => seq('-', choice('include', 'include_lib'), '(', $._expr, ')', '.'),
    pp_undef: $ => seq('-', 'undef', '(', $._pp_name, ')', '.'),
    _pp_name: $ => choice($.atom, $.var, $.macro_call_expr),

    macro_lhs: $ => seq(field('name', choice($.var, $.atom)), optional(field('args', $.var_args))),
    var_args: $ => seq('(', commaSep(field('args', $.var)), ')'),

    function_clause: $ => prec(3, seq(field('name', $.atom), $.expr_args, optional($.guard), '->', repeat($._body_token), '.')),
    guard: $ => prec.right(1, seq('when', repeat1($._guard_token))),

    _expr: $ => choice(
      $._atomish,
      $.fa,
      $.macro_call_expr,
      $.call,
      $.remote,
      $.record_expr,
      $.record_index_expr,
      $.tuple,
      $.list,
      $.binary,
      $.map_expr,
      $.parenthesized_expr,
      $.internal_fun,
      $.binary_op_expr,
      $.match_expr,
      $.unary_op_expr,
      $.dotdotdot,
    ),

    _atomish: $ => choice($._operand, $._keyword),
    _operand: $ => choice($.string, $.char, $.integer, $.var, $.atom),

    _body_token: $ => choice(
      $._expr,
      $._operator,
      ',',
      ';',
      '=>',
      ':=',
      '::',
      '->',
      '<-',
      '||',
      '|',
      ':'
    ),
    _guard_token: $ => choice($._expr, $._operator, ',', ';'),

    parenthesized_expr: $ => seq('(', repeat($._body_token), ')'),
    expr_args: $ => seq('(', repeat($._body_token), ')'),

    tuple: $ => seq('{', repeat(choice(field('expr', $.record_field), field('expr', $._body_token))), '}'),
    list: $ => seq('[', repeat(field('exprs', $._body_token)), ']'),
    binary: $ => seq('<<', repeat($._body_token), '>>'),
    map_expr: $ => seq('#{', repeat($._body_token), '}'),
    map_pair: $ => seq($._expr, choice('=>', ':='), $._expr),

    fa: $ => seq(field('fun', $.atom), '/', field('arity', choice($.integer, $.var, $.macro_call_expr))),

    call: $ => prec(1, seq(field('expr', choice($.atom, $.remote, $.macro_call_expr, $.var)), $.expr_args)),
    remote_module: $ => prec(1, seq(field('module', $.atom), ':')),
    remote: $ => seq(field('module', $.remote_module), field('fun', choice($.atom, $.macro_call_expr))),

    record_name: $ => field('name', $.atom),
    record_expr: $ => prec.right(1, seq(optional(choice($.var, $.atom, $.call)), '#', field('name', $.record_name), optional(seq('{', repeat(choice($.record_field, $._body_token)), '}')))),
    record_index_expr: $ => prec(2, seq(choice($.var, $.atom, $.call), '#', field('name', $.record_name), '.', field('field', $.record_field_name))),

    case_expr: $ => seq('case', repeat(choice($.clause, $._body_token)), 'of', repeat(choice($.clause, $._body_token)), 'end'),
    receive_expr: $ => seq('receive', repeat(choice($.clause, $._body_token)), optional(seq('after', repeat($._body_token), '->', repeat($._body_token))), 'end'),
    try_expr: $ => seq('try', repeat(choice($.clause, $._body_token, 'of', 'catch', 'after')), 'end'),
    if_expr: $ => seq('if', repeat(choice($.clause, $._body_token)), 'end'),
    fun_expr: $ => seq('fun', repeat(choice($.clause, $._body_token, '/', 'end'))),
    internal_fun: $ => prec(1, seq('fun', field('fun', $.atom), '/', field('arity', choice($.integer, $.var)))),
    clause: $ => seq(repeat($._body_token), optional($.guard), '->', repeat($._body_token), optional(';')),

    match_expr: $ => prec.right(1, seq(field('lhs', $._operand), '=', field('rhs', $._expr))),
    binary_op_expr: $ => prec.left(1, seq(field('lhs', $._operand), field('operator', $._binary_operator), field('rhs', $._operand))),
    unary_op_expr: $ => prec(1, seq(field('operator', choice('+', '-', 'bnot', 'not', 'catch')), field('rhs', $._operand))),

    macro_call_expr: $ => prec.right(1, seq('?', field('name', choice($.var, $.atom)), optional(field('args', $.expr_args)))),
    dotdotdot: _ => '...',

    string: _ => token(choice(
      seq('"""', repeat(choice(/[^"]/, /"[^"]/, /""[^"]/)), '"""'),
      seq('"', repeat(choice(/[^"\\\n]/, /\\./)), '"'),
      // Sigils (OTP 27+) require a leading `~`; without it these delimiter
      // forms would swallow bare tuples, lists, maps, binaries, and parens.
      seq(/~[A-Za-z]*"/, repeat(choice(/[^"\\\n]/, /\\./)), '"'),
      seq(/~[A-Za-z]*\(/, /[^)]*/, ')'),
      seq(/~[A-Za-z]*\[/, /[^\]]*/, ']'),
      seq(/~[A-Za-z]*\{/, /[^}]*/, '}'),
      seq(/~[A-Za-z]*</, /[^>]*/, '>')
    )),
    char: _ => token(seq('$', choice(/\\./, /[^\s]/))),
    integer: _ => token(choice(/[0-9]+#[0-9A-Za-z]+/, /[0-9]+/)),
    var: _ => token(/[A-Z_][A-Za-z0-9_@]*/),
    atom: _ => token(prec(-1, choice(/[a-z][A-Za-z0-9_@]*/, seq("'", repeat(choice(/[^'\\\n]/, /\\./)), "'")))),

    _keyword: _ => choice(
      'after', 'and', 'band', 'begin', 'behavior', 'behaviour', 'bnot', 'bor',
      'bsl', 'bsr', 'bxor', 'callback', 'case', 'catch', 'compile', 'define',
      'div', 'elif', 'else', 'end', 'endif', 'export', 'export_type', 'file',
      'fun', 'if', 'ifdef', 'ifndef', 'import', 'include', 'include_lib',
      'module', 'of', 'opaque', 'optional_callbacks', 'or', 'receive', 'record',
      'spec', 'try', 'type', 'undef', 'unit', 'when', 'xor'
    ),


    _binary_operator: _ => choice(
      '!', '/', '*', 'div', 'rem', 'band', 'and', '+', '-', 'bor', 'bxor',
      'bsl', 'bsr', 'or', 'xor', '++', '--', '==', '/=', '=<', '<', '>=',
      '>', '=:=', '=/=', 'andalso', 'orelse'
    ),

    _operator: _ => choice(
      '!', '->', '<-', '#', '::', ':>', '|', ':', '=', '||', '+', '-', '/', '*',
      'div', 'rem', 'band', 'and', 'bor', 'bxor', 'bsl', 'bsr',
      'or', 'xor', '++', '--', '==', '/=', '=<', '<', '>=', '>', '=:=', '=/=',
      'andalso', 'orelse'
    ),
  },
});
