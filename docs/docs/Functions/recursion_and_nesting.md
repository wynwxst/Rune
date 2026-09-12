# Recursion and nesting

A function may be declared inside another. It behaves like a private module-level function: it sees no locals from its parent, which is what separates it from a closure.

**Recursive and nested functions**

```rune
import std::io

fn factorial(n: i64) -> i64 {
    if n <= 1 { 1 } else { n * factorial(n - 1) }
}

fn fibonacci(n: i64) -> i64 {
    fn step(a: i64, b: i64, left: i64) -> i64 {
        if left == 0 { a } else { step(b, a + b, left - 1) }
    }
    step(0, 1, n)
}

fn main() -> i64 {
    io::println(factorial(10))
    io::println(fibonacci(20))
    0
}
```
