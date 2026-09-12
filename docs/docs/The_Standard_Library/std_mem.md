# std::mem

Layout questions, an allocator, and a smart pointer. Most programs need only the first and the last.

| Member | Signature | Does |
| --- | --- | --- |
| `size_of` | `<T>() -> usize` | bytes one `T` occupies |
| `align_of` | `<T>() -> usize` | the alignment `T` requires |
| `is_counted` | `<T>() -> bool` | whether `T` is reference counted — decided at compile time |
| `hash` | `<T>(value: T) -> u64` | a structural hash, worked out from the layout; no bound on `T` |
| `equals` | `<T>(a: T, b: T) -> bool` | structural equality, consistent with `hash` |
| `Handle<T>` | `class` | one `T` on the heap, reference counted; `*h` reads it, `*h = v` writes it |
| `of` | `<T>(value: T) -> Handle<T>` | a handle, type inferred |
| `Allocator` | `mark` | `allocate`, `deallocate`, `reallocate` |
| `allocator` | `SystemAllocator` | the process heap |
| `noBlock` | `() -> *var u8` | a block pointer to nothing |
| `isNull` | `(block: *var u8) -> bool` | tests one |
| `copy` | `(dst: *var u8, src: *u8, bytes: usize)` | moves bytes |
| `retain` / `release` | `<T>(value: T)` | **unsafe** — counting by hand, for storage the compiler cannot see |
| `slice_of` | `<T>(block: *var T, count: usize) -> [T]` | **unsafe** — `count` elements at `block` as a slice; no copy, and the block's extent is the caller's promise |

**What a type costs**

```rune
import std::io
import std::mem

struct Pixel { r: u8, g: u8, b: u8, a: u8 }

fn main() -> i64 {
    io::println(mem::size_of<i64>().$str())
    io::println(mem::align_of<i64>().$str())
    io::println(mem::size_of<Pixel>().$str())
    0
}
```

`Handle<T>` is the one to reach for. It owns a value on the heap and is itself a class, so it is reference counted: copies share the value and the last one to go releases it. Nothing about using one is unsafe — there is no way to reach the value afterwards, and no way to free it twice.

**A handle**

```rune
import std::io
import std::mem

fn main() -> i64 {
    var counter = mem::Handle<i64>(41)
    counter.set(counter.get() + 1)
    io::println(counter.get().$str())

    // Or through `*`, which a handle overloads.
    *counter += 8
    io::println((*counter).$str())

    // Two names, one value.
    let shared = mem::of("first")
    let alias = shared
    alias.set("second")
    io::println(shared.get())
    0
}
```

The allocator underneath is a mark, so a program can supply its own. Blocks come back untyped and uninitialised, every one must go back exactly once, and reading through the pointer is unsafe — which is why almost nothing should use it directly.

**Using the allocator directly**

```rune
import std::io
import std::mem

@safe("the block is sized for four i64 and no index goes past four")
fn main() -> i64 {
    let block = mem::allocator.allocate(4 as usize * mem::size_of<i64>())
    if mem::isNull(block) { return 1 }

    let cells = unsafe { block as *var i64 }
    var i = 0
    while i < 4 {
        unsafe { cells[i] = (i + 1) * 100 }
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
> **Raw indexing**
>
> `p[n]` on a raw pointer is offset arithmetic with nothing to check against — the one indexing form that is never bounds checked. It needs an unsafe context, and `*var T` to be written through.
