# Numbering

`0..n` is loop syntax rather than a value, so `iter::counting` is what numbers something. It never ends, which is safe because `zip` stops with the shorter side — and `enumerate` is the same pairing without the second iterator.

**`counting`, `zip` and `enumerate`**

```rune
import std::io
import std::iter
import std::collections::vector

fn main() -> i64 {
    var names = vector::Vector<String>()
    names.push("ada")
    names.push("grace")
    names.push("edsger")

    for (i, name) in iter::counting(1).zip(names.iterate()) {
        io::println(i.$str() + ". " + name)
    }
    for (i, name) in names.enumerate() {
        io::println(i.$str() + " -> " + name)
    }
    0
}
```

**`zip` ends with the shorter side**

```rune
import std::io

struct Countdown { pub remaining: i64 }

bind Iterator to Countdown {
    type Item = i64
    fn next(&var self) -> Self::Item? {
        if self.remaining <= 0 { return nil }
        self.remaining -= 1
        self.remaining + 1
    }
}

fn main() -> i64 {
    // `zip` ends as soon as either side does, and does not advance the longer
    // one past the pair it produced.
    for (a, b) in (Countdown { remaining: 2 }).zip(Countdown { remaining: 9 }) {
        io::println(a.$str() + "/" + b.$str())
    }
    0
}
```
