# The checker is precise

A borrow lasts until its last use, not to the end of the block, so a value can be borrowed, finished with, and then moved or changed. Two borrows of different fields never clash. And a method that takes `&var self` may still read the receiver while its own arguments are worked out — the receiver is reserved when the call is written and becomes exclusive only when it runs — so `self`-reading arguments to a mutating method are fine.

**Last-use, disjoint fields, and two-phase borrows**

```rune
import std::io

struct Point { var x: i64, var y: i64 }

class Counter {
    var n: i64
    fn init(self) { self.n = 0 }
    fn count(&self) -> i64 { self.n }
    fn add(&var self, by: i64) { self.n += by }
}

fn main() -> i64 {
    var p = Point { x: 1, y: 2 }
    let a = &var p.x
    let b = &var p.y        // a different field: no conflict
    *a += *b
    io::println(p.x.$str())              // 3

    var c = Counter()
    c.add(c.count() + 5)    // reads `self` for the argument, then mutates it
    io::println(c.count().$str())        // 5
    0
}
```

Taking `&var` and `&` of the same value at once is refused, because a writer has to be the only one that can reach it.

**Two borrows, one of them mutable**

```rune
struct Point { var x: i64, var y: i64 }
fn main() -> i64 {
    var p = Point { x: 1, y: 2 }
    var a = &var p
    let b = &p              // a reader while `a` can still write
    a.x + b.y
}
```
