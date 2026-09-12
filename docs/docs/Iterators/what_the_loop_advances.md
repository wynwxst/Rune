# What the loop advances

`for` puts the iterator in a slot of its own and advances that, which is what a binding would have got: a struct iterator is copied, so the one you named is untouched afterwards; a class iterator is shared, so it is left spent. Neither is special to `for` — both follow from what the type already means.

**Copied, or shared**

```rune
import std::io

struct Ticks { pub left: i64 }
class Cursor {
    pub left: i64
    fn init(self, left: i64) { self.left = left }
}

bind Iterator to Ticks {
    type Item = i64
    fn next(&var self) -> Self::Item? {
        if self.left <= 0 { return nil }
        self.left -= 1
        self.left
    }
}
bind Iterator to Cursor {
    type Item = i64
    fn next(&var self) -> Self::Item? {
        if self.left <= 0 { return nil }
        self.left -= 1
        self.left
    }
}

fn main() -> i64 {
    var value = Ticks { left: 3 }
    for _ in value { }
    io::println("struct, after the loop: " + value.left.$str())

    let shared = Cursor(3)
    for _ in shared { }
    io::println("class,  after the loop: " + shared.left.$str())
    0
}
```

The loop also keeps whatever it walks alive for as long as the walk lasts, so iterating a temporary is safe.
