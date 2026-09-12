# Nil coalescing

`a ?? b` produces the value inside `a` when it has one, and `b` otherwise. It is right associative, so a chain falls through.

**Defaulting an Option**

```rune
import std::io

fn lookup(key: String) -> i64? {
    if key == "found" { return 7 }
    nil
}

fn main() -> i64 {
    io::println(lookup("found") ?? -1)
    io::println(lookup("missing") ?? -1)
    io::println(lookup("missing") ?? lookup("found") ?? -1)
    0
}
```
