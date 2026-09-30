# Iterating generically

The marks are bounds like any other: one takes anything a `for` can walk, the other takes a cursor.

**Bounds that name the marks**

```rune
import std::io
import std::collections::vector

fn count<S: Sequence>(s: S) -> i64 {
    var n = 0
    for _ in s { n += 1 }
    n
}

struct Ticks { pub left: i64 }
bind Iterator to Ticks {
    type Item = i64
    fn next(&var self) -> Self::Item? {
        if self.left <= 0 { return nil }
        self.left -= 1
        self.left
    }
}

fn drain<I: Iterator>(it: I) -> i64 {
    var n = 0
    for _ in it { n += 1 }
    n
}

fn main() -> i64 {
    var v = vector::Vector<i64>()
    v.push(1)
    v.push(2)
    io::println(count(v))
    io::println(drain(Ticks { left: 4 }))
    0
}
```

> [!NOTE]
> **Note**
>
> A `dyn Iterator` cannot be iterated: a mark object does not carry the binding's associated types, so there is nothing for the loop variable to be. Iterate the concrete type.

`flatten` and `flatMap` lay an iterator of iterators out flat. Their item type is two marks deep — the item of the source's item, which `I::Item::Item` spells — and the binding that supplies it is conditional on `where I::Item: Iterator`, a clause whose subject is itself a projection.

**Flattening a vector of vectors**

```rune
import std::io
import std::text
import std::collections::vector

fn main() -> i64 {
    let rows = vec!(vec!(1, 2), vec!(3), vec!())
    let flat = rows.flatMap(||(r: &vector::Vector<i64>) -> vector::VectorIter<i64> { r.as_iter() })
    io::println(flat.fold(0, ||(acc: i64, x: i64) -> i64 { acc + x }))

    let names = vec!("ab", "cd")
    var letters = ""
    for c in names.flatMap(||(n: &String) -> text::Chars { text::chars(n.$clone()) }) {
        letters += c.$str()
    }
    io::println(letters)
    0
}
```

> [!NOTE]
> **What is not here**
>
> There is no `sum`, `min` or `max` yet: each waits on a way to name a type's zero and its ordering as a bound. `fold` and `best` cover both in the meantime.
