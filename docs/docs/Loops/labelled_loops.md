# Labelled loops

A label is written before the loop as `name:` and referenced as `:name`. It lets `break` and `continue` reach past the innermost loop.

**Escaping a nested loop**

```rune
import std::io

fn findPair(target: i64) -> (i64, i64) {
    var foundA = -1
    var foundB = -1
    outer: for a in 1..10 {
        for b in 1..10 {
            if a * b == target {
                foundA = a
                foundB = b
                break :outer
            }
        }
    }
    (foundA, foundB)
}

fn main() -> i64 {
    let (a, b) = findPair(42)
    io::println(a.$str() + " x " + b.$str())

    // `continue :label` restarts the outer loop.
    var trace = ""
    rows: for row in 0..3 {
        for col in 0..3 {
            if col == 1 { continue :rows }
            trace += row.$str() + col.$str() + " "
        }
    }
    io::println(trace)
    0
}
```
