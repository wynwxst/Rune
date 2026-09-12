# Declaration order does not matter

Sema resolves a module in four passes — collect names, resolve shapes, resolve signatures, then check bodies — so a function may call one declared below it, and two types may refer to each other.

**Used above, declared below**

```rune
import std::io

fn main() -> i64 {
    io::println(describe(Point { x: 3, y: 4 }))
    0
}

// Declared after both of its uses, and after the type it takes.
fn describe(p: Point) -> String {
    "(" + p.x.$str() + ", " + p.y.$str() + ")"
}

struct Point { x: i64, y: i64 }
```
