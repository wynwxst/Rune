# Writing a closure

**Closures of several shapes**

```rune
import std::io

fn main() -> i64 {
    let add = ||(a: i64, b: i64) -> i64 { a + b }
    let negate = ||(n: i64) -> i64 { 0 - n }
    let greet = ||(name: String) -> String { "hi " + name }
    let tick = ||() { io::println("tick") }

    io::println(add(20, 22))
    io::println(negate(5))
    io::println(greet("there"))
    tick()
    0
}
```
