# Destructuring

A binding's left side is a pattern, so a tuple can be taken apart on the way in.

**Tuple destructuring**

```rune
import std::io

fn minMax(values: [4:i64]) -> (i64, i64) {
    var low = values[0]
    var high = values[0]
    for v in values {
        if v < low { low = v }
        if v > high { high = v }
    }
    (low, high)
}

fn main() -> i64 {
    let (a, b) = (10, 20)
    let (low, high) = minMax([3, 9, 1, 7])
    io::println(a.$str() + " " + b.$str())
    io::println(low.$str() + ".." + high.$str())
    0
}
```
