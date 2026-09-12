# Stopping early

`take_while` ends at the first value its test rejects and never asks the source again — the source is advanced exactly once past the last value taken. That is the difference from `filter`, which skips a value it does not want and carries on, and it is what lets a `take_while` sit in front of an iterator that never ends.

**`take_while` against `filter`**

```rune
import std::io
import std::iter
import std::collections::vector

fn main() -> i64 {
    var v = vector::Vector<i64>()
    v.push(1); v.push(2); v.push(3); v.push(10); v.push(4)

    // The 4 after the 10 is not taken: the run ended at the 10.
    io::println(v.take_while(||(n: i64) -> bool { n < 5 }).collect())
    // `filter` skips the 10 and keeps going.
    io::println(v.filter(||(n: i64) -> bool { n < 5 }).collect())
    // `skip_while` is the mirror: everything from the first `false` onwards.
    io::println(v.skip_while(||(n: i64) -> bool { n < 3 }).collect())

    // `counting` never ends. `take_while` is what ends it.
    io::println(iter::counting(1).take_while(||(n: i64) -> bool { n * n < 50 }).collect())
    0
}
```
