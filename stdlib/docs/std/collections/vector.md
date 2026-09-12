# std::collections::vector

`Vector<T>` is the growable array: a class, so it is shared by reference and
freed when the last reference goes. `vec!(...)` builds one, `for` walks it,
`v[i]` reads it, and `v[i] = x` writes it.

## Building and reading

```rune
import std::io
import std::collections::vector

fn main() -> i64 {
    var v = vec!(3, 1, 2)
    v.push(4)
    io::println(v.length())
    io::println(v[0])
    io::println(v.at(99).isNil())          // `at` is the checked form
    io::println(v.last() ?? 0)
    v[1] = 10
    io::println(v.get(1))
    io::println(v.pop() ?? 0)
    for x in v { io::print(x.$str() + " ") }
    io::newline()
    v.clear()
    io::println(v.isEmpty())
    0
}
```

## As a sequence

Every iterator adaptor is available on a vector directly, and `collect` runs
a chain back into one.

```rune
import std::io
import std::collections::vector

fn main() -> i64 {
    let scores = vec!(70, 85, 92, 40)
    let passing = scores.filter(||(s: i64) -> bool { s >= 60 })
                        .map(||(s: i64) -> String { s.$str() + "%" })
                        .collect()
    io::println(passing.length())
    io::println(passing[0])
    io::println(scores.as_iter().fold(0, ||(a: i64, b: i64) -> i64 { a + b }))
    let fromSlice = vector::from([1, 2, 3])
    io::println(fromSlice.length())
    0
}
```

## As a slice

`asSlice` views the vector's elements as a `[T]` without copying them, so
anything written against a slice — `std::collections::slice`, a slice
pattern, a function of your own — reads a vector as it is. The slice is a
view: it is valid while the vector lives and until the next `push`,
`reserve` or `clear`, which may move the storage.

```rune
import std::io
import std::collections::vector
import std::collections::slice

fn total(values: [i64]) -> i64 {
    var t = 0
    for v in values { t += v }
    t
}

fn main() -> i64 {
    let v = vec!(3, 4, 5)
    let view = v.asSlice()
    io::println(total(view))
    io::println(slice::last(view) ?? 0)
    match view {
        [first, .., last] => io::println(first + last)
        _ => io::println("short")
    }
    0
}
```
