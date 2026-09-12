# Logical operators

`&&` and `||` short-circuit: the right side is only evaluated when it can change the answer.

**Short-circuiting, demonstrated**

```rune
import std::io

global var probes: i64 = 0

fn probe(value: bool) -> bool {
    probes += 1
    value
}

fn main() -> i64 {
    io::println(false && probe(true))    // right side skipped
    io::println(true || probe(true))     // right side skipped
    io::println("probes so far: " + probes.$str())

    io::println(true && probe(false))    // right side evaluated
    io::println(false || probe(true))    // right side evaluated
    io::println("probes now:   " + probes.$str())
    io::println(!true)
    0
}
```
