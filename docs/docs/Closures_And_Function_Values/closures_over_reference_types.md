# Closures over reference types

A closure that captures a class or a `String` keeps it alive. The captured references are released when the closure itself is.

**A captured class outlives its block**

```rune
import std::io

class Counter {
    pub total: i64
    fn init(self) { self.total = 0 }
    fn deinit(self) { io::println("  counter released") }
}

fn main() -> i64 {
    io::println("making the closure")
    let bump = {
        let shared = Counter()
        ||(amount: i64) -> i64 {
            shared.total += amount
            shared.total
        }
    }
    io::println(bump(3))
    io::println(bump(4))
    io::println("dropping the closure")
    0
}
```
