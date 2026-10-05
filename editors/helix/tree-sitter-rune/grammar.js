/**
 * A tree-sitter grammar for Rune, for editors: Helix, and anything else that
 * reads tree-sitter.
 *
 * It is deliberately forgiving. The compiler is the authority on what a
 * program means; an editor needs a tree that holds together while the code is
 * half-typed. So this models what highlighting, indentation and text objects
 * need — brackets, strings, comments, declarations and their names, calls,
 * paths, decorators, macros — and treats everything else as a run of tokens.
 * A statement the grammar does not know is still highlighted token by token,
 * and never throws the rest of the file into an error.
 */

const PRIMITIVES = [
  'i8', 'i16', 'i32', 'i64', 'isize', 'u8', 'u16', 'u32', 'u64', 'usize',
  'f32', 'f64', 'bool',
];

const OPERATORS = [
  '->', '=>', '??', '..=', '...', '..',
  '<<=', '>>=', '+=', '-=', '*=', '/=', '%=', '&=', '|=', '^=',
  '==', '!=', '<=', '>=', '&&', '||',
  '<<', '>>', '+', '-', '*', '/', '%', '&', '|', '^', '~', '!',
  '<', '>', '=', '?',
];

// Words that only mean something in one place, and are names elsewhere.
const KEYWORDS = [
  'if', 'elif', 'else', 'match', 'while', 'loop', 'for', 'in',
  'return', 'break', 'continue', 'defer', 'await',
  'var', 'let', 'mut', 'unsafe', 'async', 'weak', 'uniq',
  'move', 'dyn', 'pub', 'to', 'into', 'where', 'as', 'is', 'super', 'operator',
];

module.exports = grammar({
  name: 'rune',

  word: $ => $.identifier,

  extras: $ => [/\s/, $.line_comment, $.doc_comment, $.block_comment],

  externals: $ => [$.block_comment],

  rules: {
    source_file: $ => repeat($._element),

    _element: $ => choice(
      $._declaration,
      $.import_statement,
      $.let_binding,
      $.block,
      $._token,
    ),

    // Anything but a `{ ... }` block: what a declaration's head is made of.
    _token: $ => choice(
      $.parenthesized,
      $.bracketed,
      $.string,
      $.raw_string,
      $.block_string,
      $.character,
      $.integer,
      $.float,
      $.boolean,
      $.nil,
      $.self,
      $.primitive_type,
      $.type_identifier,
      $.identifier,
      $.call,
      $.macro_invocation,
      $.decorator,
      $.function_type,
      $.path_segment,
      $.member,
      $.intrinsic,
      $.closure_bars,
      $.keyword,
      $.operator,
      ',', ';', ':', '$',
    ),

    //=== Brackets ===========================================================

    block: $ => seq('{', repeat($._element), '}'),
    parenthesized: $ => seq('(', repeat($._element), ')'),
    bracketed: $ => seq('[', repeat($._element), ']'),

    //=== Declarations =======================================================

    // `pub`, `async` and `unsafe` before a declaration are keywords of
    // their own, as they are before a field.
    _declaration: $ => choice(
      $.function_definition,
      $.type_definition,
      $.type_alias,
      $.implementation,
      $.macro_definition,
      $.global_definition,
    ),

    function_definition: $ => prec.right(seq(
      'fn',
      field('name', $.identifier),
      optional(field('type_parameters', $.type_parameters)),
      field('parameters', $.parenthesized),
      repeat($._token),
      optional(field('body', $.block)),
    )),

    type_definition: $ => prec.right(seq(
      field('kind', choice('class', 'struct', 'enum', 'mark')),
      field('name', choice($.type_identifier, $.identifier)),
      repeat($._token),
      optional(field('body', $.block)),
    )),

    type_alias: $ => prec.right(seq(
      'type',
      field('name', choice($.type_identifier, $.identifier)),
      repeat($._token),
    )),

    // `bind Mark to Type { ... }`, `extend Type { ... }`, `extern "C" { ... }`.
    implementation: $ => prec.right(seq(
      field('kind', choice('bind', 'extend', 'extern')),
      repeat($._token),
      optional(field('body', $.block)),
    )),

    macro_definition: $ => prec.right(seq(
      'macro',
      field('name', $.identifier),
      optional(field('body', $.block)),
    )),

    global_definition: $ => prec.right(seq(
      'global',
      optional(alias(choice('var', 'let'), $.keyword)),
      field('name', $.identifier),
    )),

    type_parameters: $ => seq('<', repeat($._token), '>'),

    // Ahead of `var` as a keyword, which it also is: `&var T`.
    let_binding: $ => prec(1, seq(
      choice('let', 'var', 'mut'),
      optional(alias('mut', $.keyword)),
      field('name', $.identifier),
    )),

    import_statement: $ => prec.right(seq(
      'import',
      field('path', $.import_path),
      optional(seq('as', field('alias', $.identifier))),
    )),

    import_path: $ => prec.right(seq(
      choice($.identifier, $.type_identifier),
      repeat(seq('::', choice($.identifier, $.type_identifier, '*', $.import_group))),
    )),

    import_group: $ => seq('{', repeat(choice($.identifier, $.type_identifier, ',', 'as')), '}'),

    //=== Expressions, loosely ===============================================

    // `name(` — a call, or a constructor when the name is a type.
    call: $ => prec(2, seq(
      field('function', choice($.identifier, $.type_identifier)),
      field('arguments', $.parenthesized),
    )),

    // `name!(...)`, `name![...]`, `name!{...}`.
    macro_invocation: $ => prec(3, seq(
      field('name', $.identifier),
      token.immediate('!'),
    )),

    // `.name`, and `.name(` as a method call.
    member: $ => prec.right(seq(
      '.',
      field('name', choice($.identifier, $.integer)),
      optional(field('arguments', $.parenthesized)),
    )),

    // `.$length()`: a member the compiler provides.
    intrinsic: $ => prec.right(seq(
      '.$',
      field('name', $.identifier),
      optional(field('arguments', $.parenthesized)),
    )),

    // `io::` — a path's leading segments.
    path_segment: $ => prec(1, seq(choice($.identifier, $.type_identifier), '::')),

    // `#name`, `#name(...)` — the compiler's own; `@name`, one the program
    // declares. `#lint(allow(rule))` is one of these.
    decorator: $ => prec.right(seq(
      choice('#', '@'),
      field('name', $.identifier),
      optional(field('arguments', $.parenthesized)),
    )),

    // `@function(i64) -> bool` and `@cfunction(...)`: function types.
    function_type: $ => token(prec(2, /@c?function/)),

    // The bars of a closure, `||(x: i64) -> i64 { ... }`.
    closure_bars: $ => prec(1, seq('||', '(')),

    //=== Tokens =============================================================

    keyword: $ => choice(...KEYWORDS),
    operator: $ => choice(...OPERATORS),

    primitive_type: $ => choice(...PRIMITIVES),

    identifier: $ => /[a-z_][A-Za-z0-9_]*/,
    type_identifier: $ => /[A-Z][A-Za-z0-9_]*/,

    self: $ => choice('self', 'Self'),
    boolean: $ => choice('true', 'false'),
    nil: $ => 'nil',

    integer: $ => token(choice(
      /0[xX][0-9A-Fa-f_]+([iu](8|16|32|64|size))?/,
      /0[bB][01_]+([iu](8|16|32|64|size))?/,
      /0[oO][0-7_]+([iu](8|16|32|64|size))?/,
      /[0-9][0-9_]*([iu](8|16|32|64|size))?/,
    )),

    float: $ => token(choice(
      /[0-9][0-9_]*\.[0-9][0-9_]*([eE][+-]?[0-9][0-9_]*)?(f32|f64)?/,
      /[0-9][0-9_]*[eE][+-]?[0-9][0-9_]*(f32|f64)?/,
      /[0-9][0-9_]*(f32|f64)/,
    )),

    string: $ => seq(
      '"',
      repeat(choice(
        $.string_content,
        $.escape_sequence,
        $.interpolation,
      )),
      token.immediate('"'),
    ),
    string_content: $ => token.immediate(prec(1, /[^"\\{}\n]+|\{|\}/)),
    escape_sequence: $ => token.immediate(/\\([ntr0\\"'e{}]|x[0-9A-Fa-f]{2}|u\{[0-9A-Fa-f]{1,6}\})/),
    // `{}`, `{name}`, `{0:.2}` in a string `format!` reads.
    interpolation: $ => token.immediate(prec(2, /\{[A-Za-z0-9_.:]*\}/)),

    // `"""` ... `"""`, which may span lines and hold `"`.
    block_string: $ => token(prec(1, seq('"""', repeat(choice(/[^"]/, /"[^"]/, /""[^"]/)), '"""'))),

    // `r"..."`, `r#"..."#`, `r##"..."##`.
    raw_string: $ => token(prec(2, choice(
      /r"[^"]*"/,
      /r#"([^"]|"[^#])*"#/,
      /r##"([^"]|"[^#]|"#[^#])*"##/,
    ))),

    character: $ => token(seq(
      "'",
      choice(/[^'\\\n]/, /\\([ntr0\\"'e]|x[0-9A-Fa-f]{2}|u\{[0-9A-Fa-f]{1,6}\})/),
      "'",
    )),

    line_comment: $ => token(seq('//', /[^/\n]?/, /[^\n]*/)),
    doc_comment: $ => token(prec(1, seq('///', /[^/\n]?/, /[^\n]*/))),
  },
});
