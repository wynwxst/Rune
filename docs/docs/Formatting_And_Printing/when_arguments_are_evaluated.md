# When arguments are evaluated

Each argument is evaluated once, in the order it is written — whatever order the placeholders read them in. Where the placeholders happen to use each argument once and in order, they are spliced where they are used; otherwise they are bound first, which is what keeps `{0} {0}` from calling twice.

**Once each, in the order written**

```rune
import std::io

global var calls = 0
fn tick() -> i64 { calls += 1; calls }

global var order = ""
fn tag(name: String, value: i64) -> i64 { order += name; value }

fn main() -> i64 {
    println!("{0} {0} {0}", tick())
    println!("tick ran {} time(s)", calls)

    // Read second-then-first, but still evaluated first-then-second.
    println!("{1} {0}", tag("a", 1), tag("b", 2))
    println!("evaluated: {}", order)
    0
}
```
