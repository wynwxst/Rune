# std::collections::slice

Everything an array or a slice can do, as free functions. An array converts
to a slice on its own, so one set covers both: `slice::first([1, 2, 3])` and
`slice::first(someSlice)` are the same call.

## Asking about a run of values

```rune
import std::io
import std::collections::slice

fn main() -> i64 {
    let xs: [5:i64] = [4, 8, 15, 16, 23]
    io::println(slice::first(xs) ?? 0)
    io::println(slice::last(xs) ?? 0)
    io::println(slice::at(xs, 9).isNil())
    io::println(slice::contains(xs, 15))
    io::println(slice::indexOf(xs, 16) ?? -1)
    io::println(slice::anyOf(xs, ||(x: i64) -> bool { x > 20 }))
    io::println(slice::allOf(xs, ||(x: i64) -> bool { x > 0 }))
    io::println(slice::countWhere(xs, ||(x: i64) -> bool { x % 2 == 0 }))
    io::println(slice::firstWhere(xs, ||(x: i64) -> bool { x > 10 }) ?? 0)
    io::println(slice::fold(xs, 0, ||(a: i64, x: i64) -> i64 { a + x }))
    io::println(slice::join(xs, ", "))
    0
}
```

## Making new ones

The functions that reorder or select allocate a `Vector`, since a slice does
not own its storage.

```rune
import std::io
import std::collections::slice

fn main() -> i64 {
    let xs: [4:i64] = [3, 1, 4, 2]
    let sorted = slice::sortedBy(xs, ||(a: i64, b: i64) -> bool { a < b })
    io::println(sorted[0].$str() + " " + sorted[3].$str())
    let big = slice::filtered(xs, ||(x: i64) -> bool { x > 2 })
    io::println(big.length())
    let backwards = slice::reversed(xs)
    io::println(backwards[0])
    io::println(slice::minimumBy(xs, ||(a: i64, b: i64) -> bool { a < b }) ?? 0)
    let window = xs[1..3]                   // a slice into the same storage
    io::println(window.$length())
    0
}
```
