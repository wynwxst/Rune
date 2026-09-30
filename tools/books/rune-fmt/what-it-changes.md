# What it changes

A file goes through five steps, in this order.

## 1. The linter's style fixes

The fixes `rune lint --fix` would make for these rules — the ones about how code
looks, never what it does:

| Rule | Change |
| --- | --- |
| `unnecessary-semicolon` | a `;` at the end of a line is removed |
| `while-true` | `while true { }` becomes `loop { }` |
| `bool-comparison` | `x == true` becomes `x` |
| `duplicate-import` | a second `import` of the same thing is removed |
| `trailing-whitespace`, `tab-indentation` | as step 4 would anyway |

An `@lint(allow(...))` directive that turns one of these off keeps it off here
too: `rune doc lint` has the details.

## 2. What the compiler knows, written in

**Types.** A `let` or `var` without a type gets the one the compiler inferred,
and so does a closure's parameter:

```rune
let names = vector::Vector<String>()
// becomes
let names: vector::Vector<String> = vector::Vector<String>()
```

A type is written the way the file itself would write it — through the name the
module is imported under (`vector::Vector`, or the alias of `import std::io as
out`) — and left out where there is no way to write it: a type from a module the
file does not import, a `some Mark`, the element of a `for` loop.

**Argument labels.** An argument given by position gets its parameter's name,
exactly as if it had been written with a label:

```rune
draw(10, 20, true)
// becomes
draw(x: 10, y: 20, filled: true)
```

Only calls that take labels get them: functions, methods and constructors. A
closure called through a variable does not, nor a macro's arguments, nor
anything inside a macro invocation.

Code inside a generic function or type is left as it is: the compiler checks it
once for each type it is used with, and the types it finds are those, not what
the source could say.

## 3. The top of the file in order

The file's top-level pieces are put in this order, each group separated by a
blank line:

1. the header — a comment at the very top, apart from what follows by a blank
   line;
2. the file's directives: `@type(...)`, `@link(...)`, `@linkpath(...)`, and an
   `@lint(...)` that applies to the whole file;
3. imports, the standard library's first, then the rest, each group sorted;
4. type aliases, `type Name = ...`;
5. globals, `global var` and `global let`;
6. everything else — types, marks, binds, functions, macros — in the order they
   were written.

A comment directly above a declaration, and its decorators, go with it. Only
imports are sorted: the order of everything else is the author's.

## 4. Indentation

Four spaces a level, counted from the brackets. A line that closes a bracket
lines up with the line that opened it, and a statement that carries on onto
the next line — after an operator, or before a leading `.` — is indented one
level more:

```rune
let total: i64 = first +
    second
```

The inside of a string or a block comment is never touched.

## 5. Blank lines

Never two in a row; none just inside a `{` or just before a `}`; one between
top-level declarations; and the file ends with exactly one newline.
