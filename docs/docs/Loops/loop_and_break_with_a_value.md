# `loop` and `break` with a value

`loop` runs until something breaks out of it. Because `break` can carry a value, a `loop` is the natural way to write a search that produces a result.

**`loop` as an expression**

```rune
import std::io

fn firstPowerOfTwoAbove(limit: i64) -> i64 {
    var candidate = 1
    loop {
        if candidate > limit {
            break candidate
        }
        candidate *= 2
    }
}

fn main() -> i64 {
    io::println(firstPowerOfTwoAbove(100))
    io::println(firstPowerOfTwoAbove(1000))
    0
}
```

> [!NOTE]
> **Why only `loop`**
>
> Only `loop` can produce a value this way. A `break` with a value inside a `while` or `for` is rejected, because those loops can also finish without breaking.
