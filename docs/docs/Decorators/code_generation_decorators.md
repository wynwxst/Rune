# Code generation decorators

**#inline and #noinline**

```rune
import std::io

#inline
fn square(n: i64) -> i64 { n * n }

#noinline
fn shout(text: String) { io::println(text + "!") }

fn main() -> i64 {
    shout("area is " + square(7).$str())
    0
}
```
