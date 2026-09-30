# Characters: `text[i]` and `for c in text`

`text[i]` is the i-th *character*, however wide the ones before it were — the read `text.$at(i)` makes, spelled as a subscript. It is a value: a String's characters are not slots, so `text[i] = c` is refused, and so is `&text[i]`. Each subscript counts from the start of the UTF-8, so a loop over the characters is written as a loop, which decodes each one once:

**Reading characters**

```rune
import std::io

fn first_word(text: &String) -> String {
    var word = ""
    for c in text {
        if c == ' ' { break }
        word += c
    }
    word
}

fn main() -> i64 {
    let text = "héllo wörld"
    io::println(first_word(&text))
    io::println(text[1].$str() + text[7].$str())    // éö

    var count = 0
    for c in text { count += 1 }
    io::println(count.$str() + " characters in " + text.$length().$str() + " bytes")

    var reversed = ""
    for c in "abc" { reversed = c.$str() + reversed }
    io::println(reversed)
    0
}
```

A `String` is a `Sequence` whose items are `Character`s, so the iterator adaptors apply to it as they do to a `Vector`: `text.iterate().map(...)`, `.filter(...)`, `.count()`. Slicing with `[a..b]` is not offered — a range of *bytes* would cut a character in half, and a range of characters would have to count its way in — `$substring(a, b)` takes byte offsets and says so.

**Not a slot**

```rune
fn main() -> i64 {
    var text = "abc"
    text[0] = 'x'
    0
}
```
