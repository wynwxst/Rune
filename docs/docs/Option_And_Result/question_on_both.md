# `?`, on both

`?` unwraps or returns early. The function it appears in has to return the same shape: an Option for an Option, a Result for a Result. The error travels out as the function's own error type — the same type passes through untouched, and a different one is converted, as the next heading describes.

**Chaining fallible steps**

```rune
import std::io
import std::result

fn firstEven(values: [4:i64]) -> i64? {
    for v in values { if v % 2 == 0 { return v } }
    nil
}

fn halfOfFirstEven(values: [4:i64]) -> i64? {
    let found = firstEven(values)?        // returns None from here
    found / 2
}

fn parse(text: String) -> result::Result<i64, String> {
    match text.$toInt() {
        Some(n) => Ok(n),
        None => Err("bad number: " + text),
    }
}

fn sumOf(a: String, b: String, c: String) -> result::Result<i64, String> {
    Ok(parse(a)? + parse(b)? + parse(c)?)  // the first Err travels out
}

fn main() -> i64 {
    io::println(halfOfFirstEven([1, 8, 3, 5]).or(-1))
    io::println(halfOfFirstEven([1, 3, 5, 7]).or(-1))

    match sumOf("1", "2", "3") {
        Ok(v) => io::println("total " + v.$str()),
        Err(e) => io::println(e),
    }
    match sumOf("1", "two", "3") {
        Ok(v) => io::println("total " + v.$str()),
        Err(e) => io::println(e),
    }
    0
}
```

**`?` needs a matching result type**

```rune
import std::io

fn firstEven(values: [4:i64]) -> i64? {
    for v in values { if v % 2 == 0 { return v } }
    nil
}

fn wrong(values: [4:i64]) -> i64 {
    firstEven(values)?
}

fn main() -> i64 { 0 }
```
