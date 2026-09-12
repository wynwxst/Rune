# Slices

A slice is a pointer and a length. Arrays coerce to slices, so a function taking `[i64]` accepts an array of any size — that is how you write code that does not care how long its input is.

**One function, arrays of every length**

```rune
import std::io

fn sum(values: [i64]) -> i64 {
    var total = 0
    for v in values { total += v }
    total
}

fn largest(values: [i64]) -> i64? {
    if values.$isEmpty() { return nil }
    var best = values[0]
    for v in values { if v > best { best = v } }
    best
}

fn main() -> i64 {
    let five: [5:i64] = [3, 1, 4, 1, 5]
    let three: [3:i64] = [10, 20, 30]
    io::println("sum of five:  " + sum(five).$str())
    io::println("sum of three: " + sum(three).$str())
    // A range narrows a slice further; both bounds are optional.
    io::println("middle:       " + sum(five[1..4]).$str())
    io::println("tail:         " + sum(five[2..]).$str())
    io::println("largest:      " + largest(five).or(0).$str())
    io::println("of nothing:   " + largest(five[0..0]).hasValue().$str())
    0
}
```
