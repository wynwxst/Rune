# Producing an Option

**`?` gives up early, once**

```rune
import std::io

struct Config { retries: i64, host: String }

fn parseRetries(text: String) -> i64? { text.$toInt() }

// `?` gives up early, so the failure is answered for once, at the end.
fn load(host: String, retries: String) -> Config? {
    let n = parseRetries(retries)?
    if n < 0 { return nil }
    Config { retries: n, host: host }
}

fn describe(c: Config?) -> String {
    match c {
        Some(cfg) => cfg.host + " x" + cfg.retries.$str(),
        None => "unusable",
    }
}

fn main() -> i64 {
    io::println(describe(load("example.com", "3")))
    io::println(describe(load("example.com", "-1")))
    io::println(describe(load("example.com", "many")))
    0
}
```

**Returning, wrapping, and building explicitly**

```rune
import std::io
import std::option

fn firstEven(values: [6:i64]) -> i64? {
    for v in values {
        if v % 2 == 0 { return v }      // wrapped as Some(v)
    }
    nil                                  // Option::None
}

fn explicit(flag: bool) -> i64? {
    if flag { Some(9) } else { None }    // spelled out
}

fn main() -> i64 {
    let evens: [6:i64] = [1, 3, 8, 5, 7, 9]
    let odds: [6:i64] = [1, 3, 5, 7, 9, 11]

    io::println(firstEven(evens).or(-1))
    io::println(firstEven(odds).or(-1))
    io::println(explicit(true).or(-1))
    io::println(explicit(false).or(-1))

    // Built through the helper functions, when there is no context to infer.
    io::println(option::some(3).or(0))
    io::println(option::none::<i64>().or(0))
    0
}
```
