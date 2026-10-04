# Taking input apart by a pattern

Most macros want their input in named pieces. `capture` takes a pattern of words: `$name` takes one or more tokens, any other word has to be there as written, and `[ ... ]` is a part that may be missing. A capture takes as few tokens as it can, so `$value for` stops at the first `for` — and since a group is one token, never at a `for` inside brackets.

`Macro::quote` is the other half: Rune source with `$name` holes, filled from the captures. A `$` that is not a capture's name, as in `.$length()`, is left alone, and `$$` is a `$` when one would be read as a capture.

**Patterns and templates**

```text
#type(Macros)

import std::Macro

/// `comprehend!(x * 2 for x in xs if x > 0)` — a list comprehension.
#macro
pub fn comprehend(input: Macro::Tokens) -> Macro::Tokens {
    let c = input.capture("$value for $item in $source [if $condition]")
    if !c.matched() { return Macro::fail("comprehend! " + c.why()) }
    if c.has("condition") {
        return Macro::quote("$source.values().filter(||($item) { $condition })" +
                            ".map(||($item) { $value }).collect()", c)
    }
    Macro::quote("$source.values().map(||($item) { $value }).collect()", c)
}

/// `around!(a => b c)` gives `"a | b c"`.
#macro
pub fn around(input: Macro::Tokens) -> Macro::Tokens {
    if !input.contains("=>") { return Macro::fail("around! needs `=>`") }
    Macro::string(input.before("=>").text() + " | " + input.after("=>").text())
}
```

| On the `Captures` it answers | Is |
| --- | --- |
| `matched()` | whether the run had the pattern's shape |
| `why()` | if not, why — words fit for `Macro::fail` |
| `has(name)` | whether `$name` took anything; false for one in a `[ ... ]` part that was not written |
| `get(name)` / `text(name)` | what it took, as tokens or as source |
