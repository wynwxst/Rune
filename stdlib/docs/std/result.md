# std::result

`Result<T, E>` is a value or a reason there is none. Like `Option`, it is an
ordinary enum with an ordinary API; unlike a panic, it can be handled. A
function returning one can write a bare `T` or `E` and the compiler wraps
it — the type says which channel a value belongs to.

## Returning one, and `?`

```rune
import std::io

enum ParseError { Empty, NotANumber(String) }

fn parse(text: String) -> Result<i64, ParseError> {
    if text.$isEmpty() { return ParseError::Empty }        // promoted to Err
    match text.$toInt() {
        Some(n) => n,                                       // promoted to Ok
        None => ParseError::NotANumber(text),
    }
}

fn sum(a: String, b: String) -> Result<i64, ParseError> {
    parse(a)? + parse(b)?                                   // the first Err travels out
}

fn main() -> i64 {
    match sum("2", "3") {
        Ok(n) => io::println(n),
        Err(e) => io::println("failed"),
    }
    match sum("2", "x") {
        Ok(n) => io::println(n),
        Err(ParseError::NotANumber(t)) => io::println("not a number: " + t),
        Err(ParseError::Empty) => io::println("empty"),
    }
    0
}
```

## The API

```rune
import std::io
import std::result

fn parse(text: String) -> Result<i64, String> {
    match text.$toInt() {
        Some(n) => n,
        None => "bad number: " + text,
    }
}

fn main() -> i64 {
    io::println(parse("42").or(0))
    io::println(parse("x").recover(||(e: String) -> i64 { e.$length() }))
    io::println(parse("21").map(||(n: i64) -> i64 { n * 2 }).or(0))
    io::println(parse("x").mapErr(||(e: String) -> i64 { 0 - 1 }).unwrapErr())
    io::println(parse("7").andThen(||(n: i64) -> Result<i64, String> { n + 1 }).or(0))
    io::println(parse("x").ok().isNil())
    io::println(parse("x").error() ?? "")

    // An Option becomes a Result by naming what its absence means.
    let missing: i64? = nil
    io::println(result::from(missing, "nothing there").isErr())
    0
}
```

## Converting the error with `?`

When the function's error type differs from the operand's, `?` looks for a
`bind Theirs into Ours` and calls its `convert` on the way out — see
`std::convert`.

```rune
import std::io

enum Low { Bad }
enum High { FromLow(Low), Other }

bind Low into High {
    fn convert(&self) -> High { High::FromLow(*self) }
}

fn low() -> Result<i64, Low> { Low::Bad }
fn high() -> Result<i64, High> { let v = low()?; v }

fn main() -> i64 {
    match high() {
        Ok(v) => io::println(v),
        Err(High::FromLow(Low::Bad)) => io::println("converted on the way out"),
        Err(High::Other) => io::println("other"),
    }
    0
}
```
