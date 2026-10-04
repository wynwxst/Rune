# Rules

Every rule has a name — the one in brackets after a finding, and the one you give
`--allow`, `--warn`, `Rune.toml` and an `#lint(...)` directive. `rune lint --list`
prints them all with their levels.

| Rule | Level | Fix |
| --- | --- | --- |
| [unused-variable](#unused-variable) | warn | prefix with `_` |
| [unused-parameter](#unused-parameter) | allow | — |
| [unused-import](#unused-import) | warn | remove the import |
| [duplicate-import](#duplicate-import) | warn | — |
| [never-reassigned](#never-reassigned) | warn | `var` to `let` |
| [dead-code](#dead-code) | warn | — |
| [unreachable-code](#unreachable-code) | warn | — |
| [self-assignment](#self-assignment) | warn | — |
| [bool-comparison](#bool-comparison) | warn | remove the comparison |
| [empty-block](#empty-block) | warn | — |
| [naming](#naming) | note | rename a local |
| [needless-return](#needless-return) | note | remove `return` |
| [while-true](#while-true) | note | `loop` |
| [unnecessary-semicolon](#unnecessary-semicolon) | note | remove the `;` |
| [trailing-whitespace](#trailing-whitespace) | note | remove it |
| [tab-indentation](#tab-indentation) | note | indent with spaces |
| [line-too-long](#line-too-long) | note | — |
| [missing-docs](#missing-docs) | allow | — |
| [todo](#todo) | allow | — |
| [invalid-lint](#invalid-lint) | warn | — |

Each example below is linted when the toolchain's tests run, and has to produce
the rule it is under — so what this page shows is what the linter does.

## unused-variable

A `let` or `var`, a loop variable, a name bound by a pattern in a `match` arm or
an `is` test, or a closure's parameter, that nothing reads.

```rune
import std::io

fn main() -> i64 {
    let total = 3
    for i in 0..3 { io::println("tick") }
    0
}
```

Both `total` and `i` are reported. Assigning to a variable is not reading it: a
variable that is only ever written is still unused. A name that starts with `_`
is never reported, which is how you say the value is ignored on purpose — and
that is the fix: `_total`, `_i`.

A pattern that names a struct's fields, `Shape::Rect { width, height }`, cannot
rename them, since the names are the fields'. There the fix ignores the field
instead: `Shape::Rect { width, height: _ }`.

## unused-parameter

A parameter the function never reads. Off by default: a function that fills in a
mark's requirement, or is handed to something as a callback, has to take the
parameters it is given whether it needs them or not.

```rune
fn describe(count: i64, label: String) -> String {
    label
}
```

Turn it on with `--warn unused-parameter` when you want to hear about them. It
has no fix: a parameter's name is part of how the function is called, and
changing it is yours to decide.

## unused-import

An `import` whose name appears nowhere else in the file.

```rune
import std::io
import std::math

fn main() -> i64 {
    io::println("no maths here")
    0
}
```

An import is not reported when being imported is the point, even though its name
is never written:

- the module has `extend` or `bind` blocks, which change what other types can do
  wherever it is imported;
- a macro the module declares is invoked, as `name!(...)`;
- it is a package's own module imported by the package's root file,
  `src/lib.rune` or `src/main.rune`, which is how a module becomes part of the
  package;
- it is a glob, `import a::*`;
- it could not be followed at all — the compiler reports that one.

The fix removes the import's line. For one member of a group, `import std::{io,
math}`, it is reported without a fix, since the line has to stay.

## duplicate-import

Two imports that bind the same name. The second silently replaces the first, so
reordering the imports would change what the file means.

```rune
import std::io
import app::io
```

Qualify one of them in full where it is used, `std::io::println(...)`, and drop
its import.

## never-reassigned

A `var` that is never assigned again, never borrowed with `&var`, and never has a
method called on it or an element set — so it could be a `let`, which says so to
the reader.

```rune
import std::io

fn main() -> i64 {
    var limit = 10
    io::println(limit)
    0
}
```

The rule is careful rather than clever: any method call on the variable counts as
a possible change, because a method taking `&var self` changes it. So it misses
some `var`s that could be `let`, and never reports one that could not. The fix
changes the `var` to `let`.

## dead-code

A private function, type or global that nothing in its file uses. `pub` decides
what anyone else can see, so a private declaration that is not used in its own
file is not used anywhere.

```rune
fn helper() -> i64 { 1 }

fn main() -> i64 {
    0
}
```

`main` is never reported, and neither is anything with a decorator (an `#export`
is used from C), anything in an `extern` block, or anything in a file that says
`#type(Macros)`.

## unreachable-code

A statement after `return`, `break` or `continue` in the same block. It can never
run.

```rune
import std::io

fn main() -> i64 {
    return 0
    io::println("never printed")
}
```

A `return` in one arm of a `match`, `Some(x) => return x,`, only ends that arm, and
is not reported.

## self-assignment

Assigning something to itself, which does nothing and was usually meant to be
something else.

```rune
fn main() -> i64 {
    var count = 1
    count = count
    count
}
```

## bool-comparison

Comparing with `true` or `false`. `x == true` is `x`, and `x == false` is `!x`.

```rune
fn check(ready: bool) -> bool {
    ready == true
}
```

When the comparison changes nothing — `== true`, `!= false` — the fix removes it.
When it negates, the message says to use `!`, and there is no automatic fix,
since where the `!` goes depends on the expression.

## empty-block

An `if`, `elif`, `else`, `for` or `loop` whose body is empty.

```rune
fn main() -> i64 {
    for i in 0..10 {}
    0
}
```

A comment inside the braces says the emptiness is meant, and silences it. Two
empty bodies are idioms and never reported: `while` (as in
`while self.fill() {}`, where the condition does the work) and a `match` arm,
`None => {}`.

## naming

Rune's own naming: types, marks, enum variants and type aliases in
`UpperCamelCase`; functions, methods, fields, parameters and variables in
`lowerCamelCase`; globals in `lowerCamelCase` or `SCREAMING_SNAKE_CASE`. Leading
underscores are allowed.

```rune
struct point_2d {
    pub x: i64
}

fn read_line() -> i64 { 0 }
```

The message suggests the right spelling. A local variable is renamed by the fix,
together with every use of it; anything else is reported without one, since a
declaration may be used from other files. Declarations in `extern` blocks keep
their C names, and a method in a `bind` block keeps the name the mark gives it.

## needless-return

`return` as the last statement of a function. A block's last expression is its
value, so the `return` says nothing.

```rune
fn double(n: i64) -> i64 {
    return n * 2
}
```

The fix removes the `return`, leaving `n * 2`.

## while-true

`while true`, which is `loop` spelled the long way.

```rune
fn main() -> i64 {
    while true {
        break
    }
    0
}
```

The fix writes `loop`.

## unnecessary-semicolon

A `;` at the end of a line, or before a `}`. A line break already ends a
statement; a `;` is only needed between two statements on the same line.

```rune
fn main() -> i64 {
    let a = 1;
    a
}
```

The fix removes it.

## trailing-whitespace

Spaces or tabs at the end of a line. Lines inside a string that spans several
lines are left alone — there the whitespace is part of the string. The fix
removes it.

## tab-indentation

A line indented with tabs. Rune source is indented with spaces; the fix replaces
each tab with four.

## line-too-long

A line longer than the limit, which is 120 characters unless the package or the
command line sets another (see [Configuring it](configuration.md)). Characters
are counted, not bytes, so a line of non-ASCII text is measured as it looks. It
has no fix: where to break a line is a matter of reading it.

## missing-docs

A `pub` declaration with no `///` comment and no `#Doc(...)`. Off by default; a
library that others read will want it on.

```rune
pub fn area(width: i64, height: i64) -> i64 {
    width * height
}
```

Enum variants and the methods of a `bind` block are not reported: a variant is
documented by its enum, and a bound method by its mark.

## todo

A comment with `TODO`, `FIXME` or `XXX` in it — a note that something is not
finished. Off by default; turn it on to find them all before a release.

```rune
fn main() -> i64 {
    // TODO: read the configuration file
    0
}
```

## invalid-lint

An `#lint(...)` directive the linter cannot act on: it names a rule that does
not exist, uses a level other than `allow`, `warn` or `note`, or has nothing
after it to apply to. Without this, a misspelt rule name would leave the
warning it was meant to silence in place with no hint why.

```rune
#lint(allow(unused-varaible))
fn main() -> i64 {
    0
}
```
