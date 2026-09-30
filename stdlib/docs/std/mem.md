# std::mem

Layout questions, an allocator, `Handle<T>` — a reference-counted box for a
single value — and `Buffer<T>`, a bounds-checked block sized at run time.
Most programs need only the first and the last; the raw block functions are
for containers and anything else that manages its own storage.

## Layout, and structural hashing

```rune
import std::io
import std::mem

struct Pair { pub a: i64, pub b: String }

fn main() -> i64 {
    io::println(mem::size_of<i64>())
    io::println(mem::align_of<f64>())
    io::println(mem::is_counted<String>())
    io::println(mem::is_counted<i64>())
    // `hash` and `equals` work a value out from its layout — what `Map` keys on.
    io::println(mem::equals(Pair { a: 1, b: "x" }, Pair { a: 1, b: "x" }))
    io::println(mem::hash("key") == mem::hash("key"))
    0
}
```

## `Handle<T>` and `Buffer<T>`

A `Handle` is one value behind a counted reference — how a closure or two
places share a value. A `Buffer` is a block with every read and write
checked; leave the fill out and it zeroes.

```rune
import std::io
import std::mem

fn main() -> i64 {
    var h = mem::Handle<i64>(41)
    h.set(h.get() + 1)
    io::println(h.get())
    io::println(*h)                 // `deref` is bound, so `*h` reads it too
    let shared = mem::of("text")
    io::println(shared.get())

    var b = mem::Buffer<i64>(4)
    io::println(b.length())
    b[2] = 7
    io::println(b[2])
    io::println(b.at(9).isNil())      // out of range is nil here, an abort with `[]`
    b.fill(1)
    io::println(b[0] + b[3])
    0
}
```

## Borrowing what a handle owns

`get()` and `*h` hand back a **copy**. `look()` and `touch()` hand back the
value where it lies — a borrow that lasts as long as the handle does — which
is what a structure built out of handles is walked with: copying at every
step would copy the rest of the structure with it.

```rune
import std::io
import std::mem
import std::mem::{Handle}

struct Cell { value: i64, next: Handle<Cell>? }

fn main() -> i64 {
    var chain = Handle<Cell>(Cell {
        value: 1,
        next: Some(Handle<Cell>(Cell { value: 2, next: nil }))
    })

    // Reading: one borrow at a time.
    var total = 0
    var cur = &chain.look().next
    total += chain.look().value
    while cur is Some(cell) {
        total += cell.look().value
        cur = &cell.look().next
    }
    io::println(total)

    // Writing through the same shape.
    chain.touch().value = 10
    io::println(chain.look().value)
    0
}
```

## Copying: `Clone`

`mem::Clone` is an **automatic** mark: a type has it when every part of it
has it, so a struct of numbers has it without saying so. Two things do not:
anything that runs a `deinit` — a destructor is a promise the compiler
cannot read — and anything it cannot look into, such as a closure or an
`Any`. A type that owns something says what copying it means by writing
`fn clone(&self) -> Self`, which `$clone()` then calls, and claims the mark
with `bind mem::Clone to ...`.

```rune
import std::io
import std::mem

struct Point { x: i64, y: i64 }

fn twice<T: mem::Clone>(value: &T) -> (T, T) {
    (value.$clone(), value.$clone())
}

fn main() -> i64 {
    let p = Point { x: 1, y: 2 }
    let pair = twice(&p)
    io::println(pair.0.x + pair.1.y)
    0
}
```

## Raw memory as a slice

`slice_of<T>(block, count)` makes a `[T]` out of an address and a count,
copying nothing. It is unsafe for the obvious reason — nothing checks that
`count` initialised elements are really there — and it is how a container
hands its contents to code that takes a slice: `Vector::asSlice` is one
line around it.

```rune
import std::io
import std::mem
import std::collections::slice
import std::collections::vector

fn main() -> i64 {
    let block = mem::allocator.allocate(4 * mem::size_of<i64>())
    let slots = unsafe { block as *var i64 }
    for i in 0..4 { unsafe { slots[i] = (i + 1) * 10 } }
    // Four elements, all written: the promise `slice_of` needs.
    let view = unsafe { mem::slice_of<i64>(slots, 4) }
    io::println(view.$length())
    io::println(slice::fold(view, 0, ||(a: i64, n: i64) -> i64 { a + n }))
    mem::allocator.deallocate(block)

    let v = vec!(1, 2, 3)
    io::println(slice::join(v.asSlice(), "+"))
    0
}
```
