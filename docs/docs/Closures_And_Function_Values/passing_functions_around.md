# Passing functions around

A named function converts to a function value automatically, so it can be passed wherever a closure can.

**Named functions and closures interchangeably**

```rune
import std::io

fn double(n: i64) -> i64 { n * 2 }
fn square(n: i64) -> i64 { n * n }

fn applyAll(f: @function(i64) -> i64, values: [4:i64]) -> String {
    var out = ""
    for v in values { out += f(v).$str() + " " }
    out
}

fn compose(f: @function(i64) -> i64, g: @function(i64) -> i64,
           value: i64) -> i64 {
    f(g(value))
}

fn main() -> i64 {
    let values: [4:i64] = [1, 2, 3, 4]
    io::println(applyAll(double, values))
    io::println(applyAll(square, values))
    io::println(applyAll(||(n: i64) -> i64 { n + 100 }, values))
    io::println(compose(double, square, 3))
    0
}
```
