# std::collections::vector

A growable array, and the worked example for everything above: every unsafe operation in the standard library's `Vector` is inside it, behind an interface that hands back `Option` where an index might not exist.

| Method | Signature | Does |
| --- | --- | --- |
| `length` | `(&self) -> i64` | how many it holds |
| `isEmpty` | `(&self) -> bool` |  |
| `capacityOf` | `(&self) -> i64` | room before it must grow |
| `at` | `(&self, index: i64) -> T?` | the element, or nothing |
| `get` | `(&self, index: i64) -> T` | the element; **aborts** if absent |
| `last` | `(&self) -> T?` | the final element |
| `push` | `(&var self, value: T)` | appends, growing if needed |
| `pop` | `(&var self) -> T?` | removes and returns the last |
| `set` | `(&var self, index: i64, value: T) -> bool` | replaces; false if out of range |
| `clear` | `(&var self)` | drops every element, keeps the storage |
| `reserve` | `(&var self, wanted: i64)` | room for `wanted` |
| `asSlice` | `(&self) -> [T]` | every element as a slice over the vector's own storage — no copy |
| `v[i]` / `v[i] = x` |  | the direct forms — **abort** out of range, where `at` and `set` answer |
| `from` | `<T>(values: [T]) -> Vector<T>` | builds one from an array or slice |

**A vector of numbers**

```rune
import std::io
import std::collections::vector

fn main() -> i64 {
    var squares = vector::Vector<i64>()
    var i = 1
    while i <= 6 {
        squares.push(i * i)
        i += 1
    }
    io::println("length   " + squares.length().$str())
    io::println("capacity " + squares.capacityOf().$str())
    io::println("at 2     " + squares.get(2).$str())
    io::println("popped   " + squares.pop().unwrap().$str())

    // Out of range is an Option, not a crash.
    match squares.at(99) {
        Some(v) => io::println("at 99 " + v.$str()),
        None => io::println("at 99 nothing there"),
    }
    0
}
```

A vector of reference-counted values keeps the books itself: it claims a reference when a value goes in and gives one up when it comes out or when the vector is released. The count at the end is the proof.

**A vector of strings**

```rune
import std::io
import std::process
import std::collections::vector

fn main() -> i64 {
    let before = process::liveObjectCount()
    {
        var names = vector::from(["ada", "grace", "alan"])
        names.push("edsger")
        io::println(names.length().$str() + ", last " + names.last().unwrap())
        io::println("popped " + names.pop().unwrap())
    }
    let after = process::liveObjectCount()
    io::println("balanced " + (before == after).$str())
    0
}
```

> [!NOTE]
> **Sharing**
>
> `Vector` is a class, so passing one around shares it rather than copying. Its storage is a single block from `mem::allocator`, grown by doubling and handed back when the last reference goes.

`asSlice` views the elements as a `[T]` without copying them, so anything written against a slice — `std::collections::slice`, a slice pattern, a function of your own — reads a vector as it is. The slice is a view, not an owner: it is valid while the vector lives and until the next `push`, `reserve` or `clear`, which may move the storage. Underneath is `mem::slice_of<T>(block, count)`, the one intrinsic that makes a slice from an address and a count, for a container of your own to do the same.

**A vector as a slice**

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
    io::println(total(view).$str())
    io::println((slice::last(view) ?? 0).$str())
    match view {
        [first, .., last] => io::println((first + last).$str()),
        _ => io::println("short"),
    }
    v.push(6)                      // may move the storage: take a new view
    io::println(v.asSlice().$length().$str())
    0
}
```
