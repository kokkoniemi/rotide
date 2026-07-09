/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

const IDENT = /[_a-zA-Z][a-zA-Z0-9_]*/;
const PKG_IDENT = /(::)?[_a-zA-Z][a-zA-Z0-9_]*(::[_a-zA-Z][a-zA-Z0-9_]*)*/;

const Q_BRACE = /\{([^{}\\]|\\.|\{([^{}\\]|\\.)*\})*\}/;
const Q_PAREN = /\(([^()\\]|\\.|\(([^()\\]|\\.)*\))*\)/;
const Q_BRACKET = /\[([^\[\]\\]|\\.|\[([^\[\]\\]|\\.)*\])*\]/;
const Q_ANGLE = /<([^<>\\]|\\.)*>/;
const Q_SLASH = /\/([^\/\\]|\\.)*\//;
const Q_BANG = /!([^!\\]|\\.)*!/;
const REGEX_MODS = /[msixpogcdualn]+/;

const TOKEN_PRECS = {
  sigil: 2,
  regex: -1,
};

const PRECS = {
  arrow: 25,
  preinc: 24,
  unary: 22,
  binary: 10,
  listop: 2,
  comma: 1,
  loose: 0,
};

const binop = (op, term) =>
  seq(field('left', term), field('operator', op), field('right', term));

module.exports = grammar({
  name: 'perl',

  externals: $ => [
    $.heredoc_token,
    $.command_heredoc_token,
    $._heredoc_start,
    $._heredoc_text,
    $.heredoc_end,
    $.pod,
  ],

  extras: $ => [
    /\s/,
    $.comment,
    $.pod,
    $.heredoc_content,
  ],

  word: $ => $._bareword,

  conflicts: $ => [
    [$.autoquoted_bareword, $.bareword],
    [$.autoquoted_bareword, $._hash_key, $.bareword],
    [$.function, $.bareword],
    [$.block, $.anonymous_hash_expression],
    [$.expression_statement, $.anonymous_hash_expression],
    [$._primary_expression, $.function_call_expression],
    [$.scalar, $.container_variable],
    [$.array, $.slice_container_variable],
    [$.hash, $.keyval_container_variable],
    [$.cstyle_for_statement, $.for_statement],
    [$._indirect_object, $._term],
    [$._indirect_object, $._primary_expression],
    [$._indirect_object, $._variables],
  ],

  rules: {
    source_file: $ => seq(
      repeat($._fullstmt),
      optional($.expression_statement),
      optional($.__DATA__),
    ),

    comment: $ => token(/#.*/),

    block: $ => prec.dynamic(1, seq('{', repeat($._fullstmt), optional($.expression_statement), '}')),

    _fullstmt: $ => choice($._barestmt, $.statement_label),

    statement_label: $ => seq(
      field('label', $.identifier),
      ':',
      field('statement', $._fullstmt),
    ),

    identifier: $ => $._bareword,

    _barestmt: $ => choice(
      $.package_statement,
      $.class_statement,
      $.class_phaser_statement,
      $.use_statement,
      $.subroutine_declaration_statement,
      $.method_declaration_statement,
      $.phaser_statement,
      $.conditional_statement,
      $.loop_statement,
      $.cstyle_for_statement,
      $.for_statement,
      $.try_statement,
      $.defer_statement,
      alias($.block, $.block_statement),
      seq($.expression_statement, ';'),
      ';',
    ),

    package_statement: $ => seq(
      'package',
      field('name', $.package),
      optional(field('version', choice($.version, $.number))),
      choice(';', $.block),
    ),

    class_statement: $ => seq(
      choice('class', 'role'),
      field('name', $.package),
      optional(field('version', choice($.version, $.number))),
      optional(seq(':', optional($.attrlist))),
      choice(';', $.block),
    ),

    class_phaser_statement: $ => seq(
      field('phase', choice('BUILD', 'ADJUST')),
      optional($.signature),
      $.block,
    ),

    phaser_statement: $ => seq(
      field('phase', choice('BEGIN', 'INIT', 'CHECK', 'UNITCHECK', 'END')),
      $.block,
    ),

    use_statement: $ => seq(
      choice('use', 'no'),
      choice(
        seq(field('module', $.package), optional($._expr)),
        field('version', $.version),
      ),
      ';',
    ),

    package: $ => $._bareword,

    subroutine_declaration_statement: $ => seq(
      optional(choice('my', 'state', 'our')),
      optional(choice('async', 'extended')),
      'sub',
      field('name', $.bareword),
      $._sub_decl_tail,
    ),

    method_declaration_statement: $ => seq(
      optional(choice('my', 'state', 'our')),
      optional(choice('async', 'extended')),
      'method',
      field('name', $.bareword),
      $._sub_decl_tail,
    ),

    _sub_decl_tail: $ => seq(
      optional(seq(':', optional($.attrlist))),
      optional($.signature),
      choice(field('body', $.block), ';'),
    ),

    signature: $ => seq('(', optional($._expr), ')'),

    attrlist: $ => prec.left(seq(
      $.attribute,
      repeat(seq(optional(':'), $.attribute)),
    )),
    attribute: $ => prec.right(seq(
      field('name', $.attribute_name),
      optional(seq('(', field('value', $.attribute_value), ')')),
    )),
    attribute_name: $ => $._bareword,
    attribute_value: $ => token(/[^)]+/),

    conditional_statement: $ => seq(
      choice('if', 'unless'),
      '(', field('condition', $._expr), ')',
      field('block', $.block),
      optional($._else),
    ),
    _else: $ => choice($.else, $.elsif),
    else: $ => seq('else', field('block', $.block)),
    elsif: $ => seq(
      'elsif', '(', field('condition', $._expr), ')',
      field('block', $.block),
      optional($._else),
    ),

    _loop_body: $ => seq(
      field('block', $.block),
      optional(seq('continue', field('continue', $.block))),
    ),

    loop_statement: $ => seq(
      choice('while', 'until'),
      '(', field('condition', $._expr), ')',
      $._loop_body,
    ),

    cstyle_for_statement: $ => seq(
      choice('for', 'foreach'),
      '(',
      field('initialiser', optional($._expr)), ';',
      field('condition', optional($._expr)), ';',
      field('iterator', optional($._expr)),
      ')',
      $._loop_body,
    ),

    for_statement: $ => seq(
      choice('for', 'foreach'),
      optional(seq(optional(choice('my', 'state', 'our')), field('variable', $.scalar))),
      '(', field('list', $._expr), ')',
      $._loop_body,
    ),

    try_statement: $ => seq(
      'try',
      field('try_block', $.block),
      optional(seq(
        'catch',
        optional(seq('(', field('catch_expr', $._expr), ')')),
        field('catch_block', $.block),
      )),
      optional(seq('finally', field('finally_block', $.block))),
    ),

    defer_statement: $ => seq('defer', field('block', $.block)),

    expression_statement: $ => choice(
      $._expr,
      $.postfix_conditional_expression,
      $.postfix_loop_expression,
      $.postfix_for_expression,
      $.yadayada,
    ),
    postfix_conditional_expression: $ => seq(
      $._expr, choice('if', 'unless'), field('condition', $._expr)),
    postfix_loop_expression: $ => seq(
      $._expr, choice('while', 'until'), field('condition', $._expr)),
    postfix_for_expression: $ => seq(
      $._expr, choice('for', 'foreach'), field('list', $._expr)),

    yadayada: $ => prec(1, '...'),

    __DATA__: $ => seq(
      alias(token(choice('__DATA__', '__END__')), $.eof_marker),
      /.*/,
      optional(alias(token(/[\s\S]+/), $.data_section)),
    ),

    _expr: $ => prec.right(seq(
      $._list_item,
      repeat(seq($._comma, optional($._list_item))),
    )),
    _comma: $ => choice(',', '=>'),

    _list_item: $ => choice($._term, $.autoquoted_bareword),

    autoquoted_bareword: $ => prec.dynamic(1, $._bareword),

    _term: $ => choice(
      $.binary_expression,
      $.relational_expression,
      $._unary_expression,
      $._primary_expression,
    ),

    binary_expression: $ => choice(
      prec.left(PRECS.binary, binop(choice(
        token(prec(2, '**')),
        '*', '/', '%', 'x',
        '+', '-', '.',
        '<<', '>>',
        '<', '>', '<=', '>=', 'lt', 'le', 'ge', 'gt',
        '==', '!=', '<=>', 'eq', 'ne', 'cmp', '~~',
        '&', '|', '^',
        token(prec(2, '&&')), '||', '//',
        '..', '...',
        '=~', '!~',
        '=', token(prec(2, '**=')), '+=', '-=', '.=',
        token(prec(2, '*=')), '/=', token(prec(2, '%=')), 'x=',
        token(prec(2, '&=')), '|=', '^=', '<<=', '>>=',
        token(prec(2, '&&=')), '||=', '//=',
        'and', 'or', 'xor',
      ), $._term)),
      prec.left(PRECS.binary, seq(
        field('condition', $._term),
        alias($._ternary_tail, $.conditional_expression),
      )),
    ),

    _ternary_tail: $ => seq(
      '?', field('consequent', $._term), ':', field('alternative', $._term),
    ),

    relational_expression: $ => prec.left(PRECS.binary, seq(
      field('left', $._term),
      field('operator', 'isa'),
      field('right', $._term),
    )),

    _unary_expression: $ => choice(
      prec(PRECS.unary, seq(
        field('operator', choice('-', '+', '~', '!', '\\')),
        field('operand', $._term),
      )),
      prec.right(PRECS.loose, seq(field('operator', 'not'), field('operand', $._term))),
      prec.right(PRECS.preinc, seq(field('operator', choice('++', '--')), field('operand', $._term))),
      prec.left(PRECS.preinc, seq(field('operand', $._term), field('operator', choice('++', '--')))),
    ),

    _primary_expression: $ => choice(
      $._variables,
      $.subscripted,
      $.slice_expression,
      $.keyval_expression,
      $._postfix_deref,
      $.method_call_expression,
      $.function_call_expression,
      $.ambiguous_function_call_expression,
      $.func0op_call_expression,
      $.func1op_call_expression,
      $.map_grep_expression,
      $.amper_sub,
      $.variable_declaration,
      $.localization_expression,
      $._control_expressions,
      $.do_eval_expression,
      $.await_expression,
      $.anonymous_subroutine_expression,
      $.anonymous_array_expression,
      $.anonymous_hash_expression,
      seq('(', optional($._expr), ')'),
      $.quoted_word_list,
      $.heredoc_token,
      $.command_heredoc_token,
      $.readline_expression,
      $.require_expression,
      $._literal,
      $.number,
      $.version,
      alias($._builtin_filehandle, $.filehandle),
      $.bareword,
    ),

    _control_expressions: $ => choice(
      $.loopex_expression,
      prec.right(PRECS.listop, seq('return', optional($._expr))),
      prec.right(PRECS.listop, seq('goto', $._term)),
      prec.right(seq('undef', optional($._term))),
    ),

    loopex_expression: $ => prec.right(seq(
      choice('last', 'next', 'redo'),
      optional($.label),
    )),
    label: $ => $._bareword,

    localization_expression: $ => prec.right(seq(
      choice('local', 'dynamically'),
      $._term,
    )),

    variable_declaration: $ => prec.right(seq(
      choice('my', 'state', 'our', 'field'),
      choice(
        field('variable', $._variables),
        field('variables', seq('(', optional(sep1_trailing($._variables, ',')), ')')),
      ),
      optional(seq(':', optional($.attrlist))),
    )),

    do_eval_expression: $ => prec.right(seq(choice('do', 'eval'), choice($.block, $._term))),
    await_expression: $ => prec.right(seq('await', $._term)),

    anonymous_subroutine_expression: $ => seq(
      optional(choice('async', 'extended')),
      choice('sub', 'method'),
      optional(seq(':', optional($.attrlist))),
      optional($.signature),
      field('body', $.block),
    ),

    anonymous_array_expression: $ => seq('[', optional($._expr), ']'),
    anonymous_hash_expression: $ => seq('{', optional($._expr), '}'),

    readline_expression: $ => seq(
      field('operator', '<'),
      optional(alias(choice($.scalar, $.bareword), $.filehandle)),
      field('operator', token.immediate('>')),
    ),

    function: $ => $._bareword,
    method: $ => choice($._bareword, $.scalar),

    method_call_expression: $ => prec.left(PRECS.arrow, seq(
      field('invocant', $._term),
      '->',
      optional('&'),
      field('method', $.method),
      optional(seq('(', optional($._expr), ')')),
    )),

    function_call_expression: $ => prec.dynamic(10, seq(
      field('function', choice($.function, alias($._listop_word, $.function))),
      '(',
      optional(field('arguments', $._expr)),
      ')',
    )),

    ambiguous_function_call_expression: $ => prec.right(PRECS.listop, seq(
      field('function', alias($._listop_word, $.function)),
      optional($._indirect_object),
      optional(field('arguments', $._expr)),
    )),

    _indirect_object: $ => choice(
      alias($._builtin_filehandle, $.filehandle),
      $.scalar,
    ),

    _builtin_filehandle: $ => token(prec(1, choice('STDIN', 'STDOUT', 'STDERR'))),

    _listop_word: $ => token(choice(
      'accept', 'atan2', 'bind', 'binmode', 'bless', 'crypt', 'chmod', 'chown',
      'connect', 'die', 'dbmopen', 'fcntl', 'flock', 'getpriority',
      'getprotobynumber', 'gethostbyaddr', 'getnetbyaddr', 'getservbyname',
      'getservbyport', 'getsockopt', 'glob', 'index', 'ioctl', 'join', 'kill',
      'link', 'listen', 'mkdir', 'msgctl', 'msgget', 'msgrcv', 'msgsend',
      'opendir', 'push', 'pack', 'pipe', 'rename', 'rindex', 'read', 'recv',
      'reverse', 'select', 'seek', 'semctl', 'semget', 'semop', 'send',
      'setpgrp', 'setpriority', 'seekdir', 'setsockopt', 'shmctl', 'shmread',
      'shmwrite', 'shutdown', 'socket', 'socketpair', 'split', 'sprintf',
      'splice', 'substr', 'symlink', 'syscall', 'sysopen', 'sysseek',
      'sysread', 'syswrite', 'tie', 'truncate', 'unlink', 'unpack', 'utime',
      'unshift', 'vec', 'warn', 'waitpid', 'formline', 'open',
      'print', 'printf', 'say', 'exec', 'system',
    )),

    func0op_call_expression: $ => prec.right(seq(
      field('function', alias(token(choice(
        '__FILE__', '__LINE__', '__PACKAGE__', '__SUB__',
        'break', 'fork', 'getppid', 'time', 'times', 'wait', 'wantarray',
      )), $.func0op)),
      optional(seq('(', ')')),
    )),

    func1op_call_expression: $ => prec.right(PRECS.listop + 1, seq(
      field('function', choice(alias($._func1op_word, $.func1op), alias(token(/-[a-zA-Z]/), $.func1op))),
      optional(choice(
        seq('(', optional($._expr), ')'),
        $._term,
      )),
    )),

    _func1op_word: $ => token(choice(
      'abs', 'alarm', 'chop', 'chdir', 'close', 'closedir', 'caller', 'chomp',
      'chr', 'cos', 'chroot', 'defined', 'delete', 'dbmclose', 'exists',
      'exit', 'eof', 'exp', 'each', 'fc', 'fileno', 'gmtime', 'getc',
      'getpgrp', 'getprotobyname', 'getpwname', 'getpwuid', 'getpeername',
      'getnetbyname', 'getsockname', 'getgrnam', 'getgrgid', 'hex', 'int',
      'keys', 'lc', 'lcfirst', 'length', 'localtime', 'log', 'lock', 'lstat',
      'oct', 'ord', 'prototype', 'pop', 'pos', 'quotemeta', 'reset', 'rand',
      'rmdir', 'readdir', 'readline', 'readpipe', 'rewinddir', 'readlink',
      'ref', 'scalar', 'shift', 'sin', 'sleep', 'sqrt', 'srand', 'stat',
      'study', 'tell', 'telldir', 'tied', 'uc', 'ucfirst', 'untie', 'umask',
      'values', 'write',
    )),

    map_grep_expression: $ => prec.right(PRECS.listop, seq(
      choice('map', 'grep', 'sort'),
      choice(
        seq(field('callback', $.block), field('list', $._expr)),
        seq('(', field('callback', $.block), field('list', $._expr), ')'),
        $._expr,
      ),
    )),

    _variables: $ => choice($.scalar, $.array, $.hash, $.arraylen, $.glob),

    scalar: $ => seq('$', $._var_indirob),
    array: $ => seq('@', $._var_indirob),
    hash: $ => seq($._HASH_PERCENT, $._var_indirob),
    arraylen: $ => seq('$#', $._var_indirob),
    glob: $ => seq($._GLOB_STAR, $._var_indirob),

    _HASH_PERCENT: $ => alias(token(prec(TOKEN_PRECS.sigil, '%')), '%'),
    _GLOB_STAR: $ => alias(token(prec(TOKEN_PRECS.sigil, '*')), '*'),
    _SUB_AMPER: $ => alias(token(prec(TOKEN_PRECS.sigil, '&')), '&'),

    amper_sub: $ => prec.right(seq(
      $._SUB_AMPER,
      choice(alias($._bareword, $.varname), $.scalar),
      optional(seq('(', optional($._expr), ')')),
    )),

    _var_indirob: $ => choice(
      $.varname,
      $.scalar,
      $._var_brace_block,
    ),

    _var_brace_block: $ => prec(1, seq(
      '{',
      choice(alias($._bareword, $.varname), $._term),
      '}',
    )),

    varname: $ => choice($._varname_token, $.block),
    _varname_token: $ => token(choice(
      PKG_IDENT,
      /[0-9]+/,
      /\^[A-Z^_]/,
      /[-!"#$%&'()+,.\/:;<=>?@\\`|~^\[\]]/,
      '_',
    )),

    container_variable: $ => prec(TOKEN_PRECS.sigil, seq('$', $._var_indirob)),
    slice_container_variable: $ => prec(TOKEN_PRECS.sigil, seq('@', $._var_indirob)),
    keyval_container_variable: $ => prec(TOKEN_PRECS.sigil, seq($._HASH_PERCENT, $._var_indirob)),

    _index_subscript: $ => seq('[', field('index', $._expr), ']'),
    _key_subscript: $ => seq('{', field('key', $._hash_key), '}'),
    _args_subscript: $ => seq('(', optional(field('arguments', $._expr)), ')'),
    _hash_key: $ => choice(
      prec.dynamic(2, alias($._bareword, $.autoquoted_bareword)),
      $._expr,
    ),

    subscripted: $ => choice(
      $.array_element_expression,
      $.hash_element_expression,
      $.coderef_call_expression,
    ),

    array_element_expression: $ => choice(
      seq(field('array', $.container_variable), $._index_subscript),
      prec.left(PRECS.arrow, seq($._term, '->', $._index_subscript)),
      seq($.subscripted, $._index_subscript),
    ),

    hash_element_expression: $ => choice(
      seq(field('hash', $.container_variable), $._key_subscript),
      prec.left(PRECS.arrow, seq($._term, '->', $._key_subscript)),
      seq($.subscripted, $._key_subscript),
    ),

    coderef_call_expression: $ => choice(
      prec.left(PRECS.arrow, seq($._term, '->', $._args_subscript)),
      seq($.subscripted, $._args_subscript),
    ),

    slice_expression: $ => choice(
      seq(field('array', $.slice_container_variable), '[', $._expr, ']'),
      seq(field('hash', $.slice_container_variable), '{', $._hash_key, '}'),
      prec.left(PRECS.arrow, seq(field('arrayref', $._term), '->', '@', '[', $._expr, ']')),
      prec.left(PRECS.arrow, seq(field('hashref', $._term), '->', '@', '{', $._hash_key, '}')),
    ),

    keyval_expression: $ => choice(
      seq(field('hash', $.keyval_container_variable), '{', $._hash_key, '}'),
      seq(field('array', $.keyval_container_variable), '[', $._expr, ']'),
    ),

    _postfix_deref: $ => choice(
      $.scalar_deref_expression,
      $.array_deref_expression,
      $.arraylen_deref_expression,
      $.hash_deref_expression,
      $.amper_deref_expression,
      $.glob_deref_expression,
      $.glob_slot_expression,
    ),
    scalar_deref_expression: $ => prec.left(PRECS.arrow, seq($._term, '->', '$', '*')),
    array_deref_expression: $ => prec.left(PRECS.arrow, seq($._term, '->', '@', '*')),
    arraylen_deref_expression: $ => prec.left(PRECS.arrow, seq($._term, '->', '$#', '*')),
    hash_deref_expression: $ => prec.left(PRECS.arrow, seq($._term, '->', '%', '*')),
    amper_deref_expression: $ => prec.left(PRECS.arrow, seq($._term, '->', '&', '*')),
    glob_deref_expression: $ => prec.left(PRECS.arrow, seq($._term, '->', '*', '*')),
    glob_slot_expression: $ => seq($.glob, '{', $._hash_key, '}'),

    require_expression: $ => prec.right(seq('require', $._term)),

    bareword: $ => $._bareword,
    _bareword: $ => token(PKG_IDENT),

    _literal: $ => choice(
      $.string_literal,
      $.interpolated_string_literal,
      $.command_string,
      $.quoted_regexp,
      $.match_regexp,
      $.substitution_regexp,
      $.transliteration_expression,
    ),

    number: $ => token(choice(
      /0[xX][0-9a-fA-F][0-9a-fA-F_]*/,
      /0[bB][01][01_]*/,
      /(\d[\d_]*)?\.\d[\d_]*([eE][+-]?\d+)?/,
      /\d[\d_]*(\.\d[\d_]*)?([eE][+-]?\d+)?/,
    )),

    version: $ => token(prec(2, /v[0-9]+(\.[0-9]+)*|[0-9]+(\.[0-9]+){2,}/)),

    string_literal: $ => choice(
      seq("'", repeat(choice($._sq_text, $.escaped_delimiter)), "'"),
      token(seq('q', choice(Q_BRACE, Q_PAREN, Q_BRACKET, Q_ANGLE, Q_SLASH, Q_BANG))),
    ),
    _sq_text: $ => token.immediate(prec(1, /[^'\\]+/)),
    escaped_delimiter: $ => token.immediate(/\\['\\]/),

    interpolated_string_literal: $ => choice(
      seq('"', repeat($._interp_content), '"'),
      token(seq('qq', choice(Q_BRACE, Q_PAREN, Q_BRACKET, Q_ANGLE, Q_SLASH, Q_BANG))),
    ),
    _dq_text: $ => token.immediate(prec(1, /[^"\\$@]+/)),

    command_string: $ => choice(
      seq('`', repeat(alias($._bt_text, $._dq_text)), '`'),
      token(seq('qx', choice(Q_BRACE, Q_PAREN, Q_BRACKET, Q_ANGLE, Q_SLASH, Q_BANG))),
    ),
    _bt_text: $ => token.immediate(prec(1, /[^`\\$@]+|\\./)),

    _interp_content: $ => choice(
      $._dq_text,
      $.escape_sequence,
      $._interp_term,
    ),
    _interp_term: $ => choice(
      alias($._interp_hash_elem, $.hash_element_expression),
      alias($._interp_array_elem, $.array_element_expression),
      $.scalar,
      $.array,
    ),
    _interp_hash_elem: $ => seq(
      field('hash', alias($._interp_container, $.container_variable)),
      token.immediate('{'),
      choice(alias($._bareword, $.autoquoted_bareword), $.scalar),
      '}',
    ),
    _interp_array_elem: $ => seq(
      field('array', alias($._interp_container, $.container_variable)),
      token.immediate('['),
      choice($.number, $.scalar),
      ']',
    ),
    _interp_container: $ => seq('$', $.varname),

    escape_sequence: $ => token.immediate(
      /\\(x[0-9a-fA-F]{1,2}|x\{[^}]*\}|N\{[^}]*\}|c.|0[0-7]*|[^xNc0])/),

    quoted_word_list: $ => token(seq('qw', choice(Q_BRACE, Q_PAREN, Q_BRACKET, Q_ANGLE, Q_SLASH, Q_BANG))),

    match_regexp: $ => seq(
      choice(
        seq(
          choice(
            field('operator', alias(token(seq('m', '/')), 'm')),
            alias(token(prec(TOKEN_PRECS.regex, '/')), '/'),
          ),
          optional(alias($._slash_regexp_content, $.regexp_content)),
          token.immediate('/'),
        ),
        alias(token(seq('m', choice(Q_BRACE, Q_PAREN, Q_BRACKET, Q_ANGLE, Q_BANG))), $.regexp_content),
      ),
      optional(field('modifiers', alias(token.immediate(REGEX_MODS), $.match_regexp_modifiers))),
    ),

    _slash_regexp_content: $ => token.immediate(/([^\/\\]|\\.)+/),

    quoted_regexp: $ => seq(
      choice(
        seq(
          alias(token(seq('qr', '/')), 'qr'),
          optional(alias($._slash_regexp_content, $.regexp_content)),
          token.immediate('/'),
        ),
        alias(token(seq('qr', choice(Q_BRACE, Q_PAREN, Q_BRACKET, Q_ANGLE, Q_BANG))), $.regexp_content),
      ),
      optional(field('modifiers', alias(token.immediate(REGEX_MODS), $.match_regexp_modifiers))),
    ),

    substitution_regexp: $ => choice(
      seq(
        field('operator', alias(token(seq('s', '/')), 's')),
        optional(alias($._slash_regexp_content, $.regexp_content)),
        token.immediate('/'),
        optional(field('replacement', alias($._slash_regexp_content, $.replacement))),
        token.immediate('/'),
        optional(field('modifiers', alias(token.immediate(/[msixpogcedualnr]+/), $.substitution_regexp_modifiers))),
      ),
      seq(
        alias(token(seq('s', Q_BRACE)), $.regexp_content),
        field('replacement', alias(token(Q_BRACE), $.replacement)),
        optional(field('modifiers', alias(token.immediate(/[msixpogcedualnr]+/), $.substitution_regexp_modifiers))),
      ),
    ),

    transliteration_expression: $ => seq(
      field('operator', alias(token(seq(choice('tr', 'y'), '/')), 'tr')),
      optional(alias($._slash_regexp_content, $.transliteration_content)),
      token.immediate('/'),
      optional(alias($._slash_regexp_content, $.transliteration_content)),
      token.immediate('/'),
      optional(field('modifiers', alias(token.immediate(/[cdsr]+/), $.transliteration_modifiers))),
    ),

    heredoc_content: $ => seq(
      $._heredoc_start,
      repeat($._heredoc_text),
      $.heredoc_end,
    ),
  },
});

function sep1_trailing(rule, separator) {
  return seq(rule, repeat(seq(separator, rule)), optional(separator));
}
