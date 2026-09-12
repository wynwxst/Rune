# `while`

**A counted loop**

```rune
import std::io

fn main() -> i64 {
    var n = 5
    var product = 1
    while n > 1 {
        product *= n
        n -= 1
    }
    io::println(product)
    0
}
```

A `while` may destructure as well as test. `while value is Some(v)` runs for as long as the pattern matches, binding afresh each turn and releasing the binding at the end of every iteration.

**Looping while a pattern matches**

```rune
import std::io

struct Queue { items: [4:i64], taken: i64 }

extend Queue {
    pub fn next(&var self) -> i64? {
        if (*self).taken >= 4 { return nil }
        let v = (*self).items[(*self).taken]
        (*self).taken += 1
        v
    }
}

fn main() -> i64 {
    var q = Queue { items: [3, 1, 4, 1], taken: 0 }
    var total = 0
    while q.next() is Some(v) {
        io::println("got " + v.$str())
        total += v
    }
    io::println("total " + total.$str())
    0
}
```
