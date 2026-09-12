# Function types

A function type is written the way a `fn` is: the parameters in parentheses, then `->` and the result. Leave the arrow off and the function returns nothing, exactly as a `fn` with no `->` does — so `@function(i64)` takes an `i64` and produces `()`.

**Function types**

```rune
import std::io

type Predicate = @function(i64) -> bool
type Combine = @function(i64, i64) -> i64
type Producer = @function() -> i64             // no parameters, returns i64
type Sink = @function(i64)                     // takes an i64, returns nothing

fn apply(f: Predicate, value: i64) -> bool { f(value) }
fn fold(f: Combine, a: i64, b: i64) -> i64 { f(a, b) }
fn produce(f: Producer) -> i64 { f() }
fn drain(f: Sink, value: i64) { f(value) }

fn main() -> i64 {
    let isPositive = ||(n: i64) -> bool { n > 0 }
    let add = ||(a: i64, b: i64) -> i64 { a + b }
    io::println(apply(isPositive, 3))
    io::println(fold(add, 20, 22))
    io::println(produce(||() -> i64 { 7 }))
    drain(||(n: i64) { io::println(n) }, 9)
    0
}
```
