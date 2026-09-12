# Raw pointers

A raw pointer is an address and nothing else: no length, no guarantee it points at anything. Creating one is fine; every *dereference* is an unsafe operation, so it has to happen inside a function marked `@unsafe` or an `unsafe { }` block.

**Reading and writing through a raw pointer**

```rune
import std::io

@unsafe
fn writeThrough(target: *var i64, value: i64) {
    *target = value
}

@unsafe
fn readThrough(source: *i64) -> i64 {
    *source
}

fn main() -> i64 {
    var cell = 7
    // The address is taken safely; only the dereference is unsafe.
    unsafe {
        writeThrough(&var cell as *var i64, 99)
        io::println("read back: " + readThrough(&cell as *i64).$str())
    }
    io::println("cell is now " + cell.$str())
    0
}
```

**Dereferencing outside an unsafe context**

```rune
fn peek(p: *i64) -> i64 {
    *p          // no @unsafe, no unsafe block
}
```

A raw pointer can also be indexed. `p[n]` is the nth element from it — the same arithmetic C does, on the pointee's size, with no length to check against. It is the one indexing form in the language that is never bounds checked, which is why it needs an unsafe context, and `*var T` to be written through.

**Indexing raw memory**

```rune
import std::io
import std::mem

@safe("the block holds four i64 and no index below goes past four")
fn main() -> i64 {
    let block = mem::allocator.allocate(4 as usize * mem::size_of<i64>())
    let cells = unsafe { block as *var i64 }

    var i = 0
    while i < 4 {
        unsafe { cells[i] = (i + 1) * 10 }
        i += 1
    }
    var total = 0
    i = 0
    while i < 4 {
        total += unsafe { cells[i] }
        i += 1
    }
    io::println(total.$str())

    mem::allocator.deallocate(block)
    0
}
```

> [!WARNING]
> **No counting**
>
> A store through a raw pointer is raw in the other sense too: no reference counting happens, and the slot is not assumed to hold anything already. `mem::retain` and `mem::release` are how a container built this way keeps its books — see `std::collections::vector`.

A wrapper that has genuinely established the invariant says so with `@safe("reason")`. The reason is not decoration — it is the record of *why* the unchecked operation inside is sound, and it appears in the diagnostic if someone later breaks the assumption.

**Justifying an unsafe operation**

```rune
import std::io

@unsafe
fn incrementThrough(cell: *var i64) {
    *cell += 1
}

/// The pointer below cannot dangle, so the unchecked write cannot be wrong.
@safe("the pointer names a live local that outlives the call")
fn bumped(start: i64) -> i64 {
    var cell = start
    unsafe { incrementThrough(&var cell as *var i64) }
    cell
}

fn main() -> i64 {
    io::println(bumped(41).$str())
    0
}
```

> [!NOTE]
> **Getting an address**
>
> `&value as *T` is the only way to get a raw pointer. There is none for a class instance — that would let you sidestep the reference count.
