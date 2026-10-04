# std::Macro

What a procedural macro is written with. A procedural macro is an ordinary
`pub fn` marked `#macro` that takes the tokens it was invoked with and hands
back the tokens to put in their place:

```rune
import std::io
import std::Macro

/// `twice!(e)` is `((e) * 2)`.
#macro
pub fn twice(input: Macro::Tokens) -> Macro::Tokens {
    Macro::parse("((" + input.text() + ") * 2)")
}

fn main() -> i64 {
    io::println(twice!(3 + 4))
    0
}
```

The compiler builds every `#macro fn` into a program of its own — for the
machine doing the compiling — and runs it to expand each invocation. So a
macro has the whole language and the whole standard library, and nothing it
imports reaches the program. It may be written in any file: one that opens
with `#type(Macros)` holds a family of macros and their helpers, and a single
macro can sit beside the code that uses it, as above. A library's macros are
exported with it.

## Tokens

A `Tokens` is a run of tokens with their structure kept: a bracketed group is
**one** token — `block`, `parens` or `brackets` — whose `text()` is the group
as written and whose `inner()` is what is inside.

| On a `Tokens` | Is |
| --- | --- |
| `length()`, `isEmpty()`, `at(i)`, `slice(a, b)` | its tokens, counting a group as one |
| `text()` | the run as source; for one token, a string's contents or a name's spelling |
| `kind()`, `isA(kind)` | `name`, `number`, `string`, `character`, `punctuation`, `keyword`, `block`, `parens`, `brackets`, or `run` |
| `split(sep)` | the parts between a piece of punctuation, at the top level only |
| `find(word)`, `findFrom(word, i)`, `contains(word)` | where a word stands at the top level |
| `before(word)`, `after(word)` | the run on either side of its first appearance |
| `capture(pattern)` | the run taken apart by a pattern |
| `add(more)`, `flat()` | append; every token with groups opened out |

`Macro::parse(source)` is how an expansion is usually written: build the
source as a string and let the lexer make tokens of it. `ident`, `number`,
`string`, `punct` and `tokens()` build tokens one at a time.

```rune
import std::io
import std::Macro

/// `around!(a => b c)` is the string "a | b c".
#macro
pub fn around(input: Macro::Tokens) -> Macro::Tokens {
    if !input.contains("=>") { return Macro::fail("around! needs `=>`") }
    Macro::string(input.before("=>").text() + " | " + input.after("=>").text())
}

fn main() -> i64 {
    io::println(around!(left => right side))
    0
}
```

## Patterns and templates

`capture` takes a pattern of words: `$name` takes one or more tokens, any
other word must be there as written, and `[ ... ]` is a part that may be
missing. A capture takes as few tokens as it can. `Macro::quote` writes the
expansion with `$name` holes filled from the captures; a `$` that names no
capture, as in `.$length()`, is left alone, and `$$` is a literal `$`.

```rune
import std::io
import std::Macro

/// `squares!(x for x in xs)` — every element squared.
#macro
pub fn squares(input: Macro::Tokens) -> Macro::Tokens {
    let c = input.capture("$value for $item in $source [if $condition]")
    if !c.matched() { return Macro::fail("squares! " + c.why()) }
    if c.has("condition") {
        return Macro::quote("$source.values().filter(||($item) { $condition })" +
                            ".map(||($item) { ($value) * ($value) }).collect()", c)
    }
    Macro::quote("$source.values().map(||($item) { ($value) * ($value) }).collect()", c)
}

fn main() -> i64 {
    let xs = vec![1, 2, 3, 4]
    for v in squares!(x for x in xs if x % 2 == 0) { io::println(v) }
    0
}
```

| On `Captures` | Is |
| --- | --- |
| `matched()` | whether the run had the pattern's shape |
| `why()` | if not, why — fit to hand to `Macro::fail` |
| `has(name)` | whether `$name` took anything |
| `get(name)`, `text(name)` | what it took, as tokens or as source |

## Refusing

`Macro::error(message)` refuses what the macro was given; `Macro::fail` does
the same as a value to `return`. The message is reported against the
invocation somebody wrote, with an arrow back at the line that refused.
