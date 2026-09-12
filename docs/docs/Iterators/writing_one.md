# Writing one

**Two hand-written iterators**

```rune
import std::io

struct Countdown { pub remaining: i64 }

bind Iterator to Countdown {
    type Item = i64
    fn next(&var self) -> Self::Item? {
        if self.remaining <= 0 { return nil }     // `nil` ends the loop
        self.remaining -= 1
        self.remaining + 1
    }
}

/// A series is often clearest as an iterator: the state is named, and the
/// loop that consumes it does not have to know any of it.
struct Fibonacci { pub limit: i64, pub a: i64, pub b: i64 }

fn fibonacci(limit: i64) -> Fibonacci { Fibonacci { limit: limit, a: 0, b: 1 } }

bind Iterator to Fibonacci {
    type Item = i64
    fn next(&var self) -> Self::Item? {
        if self.a >= self.limit { return nil }
        let value = self.a
        let onward = self.a + self.b
        self.a = self.b
        self.b = onward
        value
    }
}

fn main() -> i64 {
    for n in (Countdown { remaining: 5 }) { io::print(n.$str() + " ") }
    io::newline()
    for n in fibonacci(100) { io::print(n.$str() + " ") }
    io::newline()
    0
}
```

> [!NOTE]
> **Note**
>
> A `for` sequence cannot be a bare struct literal — the `{` would start the loop body. Name it first, or wrap it in parentheses.
