# std::text

Everything about a `String` beyond building one with `+`: the everyday half —
`split`, `trim`, `replace`, `startsWith` — and the hard half — Unicode
normalisation, case folding, and a collation that sorts `apple APPLE Ápple
äpple Banana zebra` the way a person would.

## The everyday half

These work on bytes, which is exact for the ASCII delimiters a separator
almost always is.

```rune
import std::io
import std::text
import std::collections::vector

fn main() -> i64 {
    let line = "  name = ada  "
    let parts = text::split(text::trim(line), "=")
    io::println(text::trim(parts.get(0)) + ":" + text::trim(parts.get(1)))

    io::println(text::startsWith("hello", "he"))
    io::println(text::endsWith("hello", "lo"))
    io::println(text::contains("hello", "ell"))
    io::println(text::find("hello", "l") ?? -1)
    io::println(text::replace("a-b-c", "-", "+"))
    io::println(text::join(vec!("x", "y", "z"), ", "))
    io::println(text::padStart("7", 3, '0'))
    io::println(text::withoutSuffix("file.rune", ".rune"))
    for l in text::splitLines("one\ntwo") { io::println(l) }
    0
}
```

## Walking characters

A `String` is UTF-8, so `$length()` is bytes. `chars` walks the characters,
and `codePoints` gives them as numbers.

```rune
import std::io
import std::text

fn main() -> i64 {
    let word = "héllo"
    io::println(word.$length())
    io::println(word.$charCount())
    var upperFirst = ""
    for c in text::chars(word) { upperFirst += c.$str() + "." }
    io::println(upperFirst)
    let cps = text::codePoints("ab")
    io::println(cps.get(0))
    io::println(text::fromCodePoints(cps))
    0
}
```

## Comparing text the way people do

`equal` compares after normalisation, so a composed `é` and a decomposed one
are the same word; `equalIgnoringCase` folds case as well; `compare` orders
by the root collation — base letters first, then accents, then case.

```rune
import std::io
import std::text
import std::collections::slice

fn main() -> i64 {
    io::println(text::equal("é", "e\u{301}"))
    io::println(text::equalIgnoringCase("Straße", "STRASSE"))
    let words: [4:String] = ["zebra", "Ápple", "apple", "Banana"]
    let sorted = slice::sortedBy(words, ||(a: String, b: String) -> bool {
        text::compare(a, b) < 0
    })
    var line = ""
    for w in sorted { line += w + " " }
    io::println(line)
    io::println(text::normalize("e\u{301}", text::Form::Composed).$length())
    0
}
```
