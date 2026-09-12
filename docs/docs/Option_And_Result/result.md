# Result

**Constructing, testing and unwrapping**

```rune
import std::io
import std::result

type Parsed = result::Result<i64, String>

fn parse(text: String) -> Parsed {
    match text.$toInt() {
        Some(n) => Ok(n),
        None => Err("'" + text + "' is not a number"),
    }
}

fn main() -> i64 {
    match parse("42") {
        Ok(v) => io::println("ok " + v.$str()),
        Err(e) => io::println("err " + e),
    }
    match parse("forty") {
        Ok(v) => io::println("ok " + v.$str()),
        Err(e) => io::println("err " + e),
    }

    io::println(parse("1").isOk())
    io::println(parse("x").isErr())
    io::println(parse("x").or(-1))
    io::println(parse("7").unwrap())

    // A Result narrows to an Option when the reason stops mattering.
    io::println(parse("5").ok().or(0))
    io::println(parse("x").ok().or(0))

    match parse("x").error() {
        Some(message) => io::println("reason: " + message),
        None => io::println("no error"),
    }
    0
}
```

| Method | Result |
| --- | --- |
| `isOk()` `isErr()` | `bool` |
| `or(fallback)` | the value, or `fallback` |
| `unwrap()` | the value; aborts on an error |
| `expect(message)` | the value; aborts with `message` |
| `ok()` | `Option<T>` — the value, discarding the error |
| `error()` | `Option<E>` — the error, discarding the value |

*Result's methods*
