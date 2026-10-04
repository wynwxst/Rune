# What a macro is given

Tokens, with their structure kept. A bracketed group is **one** token — `block` for `{ ... }`, `parens` for `( ... )`, `brackets` for `[ ... ]` — whose `text()` is the group exactly as written and whose `inner()` is what is inside it.

| On a `Tokens` | Is |
| --- | --- |
| `length()`, `isEmpty()` | how many tokens, counting a group as one |
| `at(i)` | the token there, or an empty one |
| `slice(start, stop)` | part of the run |
| `text()` | the run as source — or, for a run of one token, that token: a string literal's contents, a name's spelling |
| `kind()`, `isA(kind)` | `name`, `number`, `string`, `character`, `punctuation`, `keyword`, `block`, `parens`, `brackets`, or `run` for several |
| `split(sep)` | the parts between a piece of punctuation. A group is one token, so a comma inside brackets does not split |
| `add(more)` | append |
| `flat()` | every token, groups opened out |
| `find(word)` / `findFrom(word, start)` | where a piece of punctuation or a name stands at the top level, or `-1` |
| `contains(word)` | whether it is there at all |
| `before(word)` / `after(word)` | the run on either side of its first appearance |
| `capture(pattern)` | the run taken apart by a pattern — see below |

*`Macro::parse`, `ident`, `number`, `string`, `punct` and `tokens` build them; `Macro::error` refuses, and `Macro::fail` refuses as something to `return`. On a single `Token`, `isGroup()` says whether it is bracketed.*

**Three macros**

```text
#type(Macros)

import std::Macro
import std::collections::vector
import std::text

/// One accessor per name given. A pattern could not: it has no way to make
/// `getX` out of `x`.
#macro
pub fn getters(input: Macro::Tokens) -> Macro::Tokens {
    var lines = vector::Vector<String>()
    for field in input.split(",") {
        let name = field.text()
        let capital = text::upper(name.$substring(0, 1)) +
                      name.$substring(1, name.$length())
        lines.push("extend Point { fn get" + capital +
                   "(&self) -> i64 { self." + name + " } }")
    }
    Macro::parse(text::join(lines, "\n"))
}

/// Refuses what it cannot use. The message is reported against the
/// invocation, with an arrow back at this line.
#macro
pub fn firstWord(input: Macro::Tokens) -> Macro::Tokens {
    if input.isEmpty() { Macro::error("firstWord! needs a word") }
    if !input.at(0).isA("name") {
        Macro::error("firstWord! wants a name, and this is a " +
                     input.at(0).kind())
    }
    Macro::string(input.at(0).text())
}

/// A block goes in as one token and comes back as written.
#macro
pub fn traced(input: Macro::Tokens) -> Macro::Tokens {
    let label = input.at(0).text()
    let body = input.at(input.length() - 1)
    if !body.isA("block") {
        Macro::error("traced! wants a block last, and got a " + body.kind())
    }
    Macro::parse("{ io::println(\"enter " + label + "\"); " +
                 body.inner().text() + " }")
}
```

> [!NOTE]
> **Reflection, through an expansion**
>
> Because a block passes through untouched, whatever is inside it is compiled in the **program**. That is how a macro reaches `std::reflect`: `Macro::parse("reflect::typeName<" + t + ">()")` answers about the program's types, not the package's.
