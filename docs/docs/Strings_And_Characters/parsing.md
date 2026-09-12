# Parsing

**`toInt` and `toFloat` return an Option**

```rune
import std::io

fn main() -> i64 {
    io::println("42".$toInt().or(-1))
    io::println("-17".$toInt().or(-1))
    io::println("2.5".$toFloat().or(0.0))
    io::println("nope".$toInt().or(-1))
    io::println("42x".$toInt().isNil())      // the whole string must parse

    match "123".$toInt() {
        Some(n) => io::println("parsed " + n.$str()),
        None => io::println("not a number"),
    }
    0
}
```
