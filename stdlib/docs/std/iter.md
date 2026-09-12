# std::iter

What `for` walks, and how to reshape it on the way past. Two marks:
`Iterator` is a cursor (`type Item`, `fn next(&var self) -> Self::Item?`),
and `Sequence` is something that hands one out. Both are in scope everywhere.
Every adaptor is lazy: a value moves through the chain only when the loop at
the end asks for it.

## Writing an iterator

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
    var down = Countdown { remaining: 3 }
    for n in down { io::print(n.$str() + " ") }
    io::newline()
    io::println(Countdown { remaining: 5 }.count())
    0
}
```

## Reshaping a chain

```rune
import std::io
import std::iter
import std::collections::vector

fn main() -> i64 {
    let names = vec!("ada", "grace", "linus")
    for (i, name) in names.as_iter().enumerate() {
        io::println(i.$str() + ": " + name)
    }
    let long = names.filter(||(n: String) -> bool { n.$length() > 3 })
                    .map(||(n: String) -> i64 { n.$length() })
                    .collect()
    io::println(long.length())
    io::println(iter::counting(1).take(4).fold(0, ||(a: i64, b: i64) -> i64 { a + b }))
    io::println(iter::counting(0).step_by(5).take_while(||(n: i64) -> bool { n < 12 }).count())
    for (a, b) in vec!(1, 2).zip(vec!("x", "y").as_iter()) { io::println(a.$str() + b) }
    io::println(names.as_iter().find(||(n: String) -> bool { n == "grace" }) ?? "none")
    io::println(names.as_iter().position(||(n: String) -> bool { n == "linus" }) ?? -1)
    0
}
```

## Flattening, and returning a chain

`flatten` lays an iterator of iterators out flat. A function that hands back
a chain writes `-> some Iterator` rather than the wrapper type.

```rune
import std::io
import std::iter
import std::collections::vector

fn evens(limit: i64) -> some iter::Iterator {
    iter::counting(0).filter(||(n: i64) -> bool { n % 2 == 0 }).take(limit)
}

fn main() -> i64 {
    let rows = vec!(vec!(1, 2), vec!(3))
    let flat = rows.flatMap(||(r: vector::Vector<i64>) -> vector::VectorIter<i64> { r.as_iter() })
    io::println(flat.count())
    for n in evens(3) { io::print(n.$str() + " ") }
    io::newline()
    0
}
```
