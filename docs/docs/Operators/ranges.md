# Ranges

`a..b` excludes the upper bound, `a..=b` includes it. Ranges drive `for` loops, slicing, and range patterns.

**Exclusive, inclusive and open ranges**

```rune
import std::io

fn count(lo: i64, hi: i64) -> i64 {
    var n = 0
    for i in lo..hi { n += 1 }
    n
}

fn main() -> i64 {
    io::println(count(0, 5))
    var inclusive = 0
    for i in 0..=5 { inclusive += 1 }
    io::println(inclusive)

    let values: [6:i64] = [1, 2, 3, 4, 5, 6]
    io::println(values[1..4].$length())
    io::println(values[1..=4].$length())
    io::println(values[3..].$length())        // open upper bound: to the end
    io::println(values[..2].$length())        // open lower bound: from the start
    0
}
```
