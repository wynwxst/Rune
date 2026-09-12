# `break` and `continue`

**Skipping and stopping**

```rune
import std::io

fn main() -> i64 {
    var evens = 0
    for i in 0..10 {
        if i % 2 == 1 { continue }
        if i > 6 { break }
        evens += 1
    }
    io::println(evens)
    0
}
```
