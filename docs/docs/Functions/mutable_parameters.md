# Mutable parameters

A parameter is immutable unless declared `var`. A `var` parameter is a local copy, so changing it does not affect the caller — pass `&var T` for that.

**`var` parameter versus `&var` borrow**

```rune
import std::io

fn countdown(var n: i64) -> String {
    var out = ""
    while n > 0 {
        out += n.$str() + " "
        n -= 1
    }
    out
}

fn bump(target: &var i64) {
    *target += 1
}

fn main() -> i64 {
    let start = 3
    io::println(countdown(start))
    io::println(start)              // untouched: `var n` was a copy

    var counter = 0
    bump(&var counter)
    bump(&var counter)
    io::println(counter)            // changed: passed by mutable borrow
    0
}
```
