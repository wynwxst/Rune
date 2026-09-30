# std::mem

Layout questions, an allocator, and a smart pointer. Most programs need only the first and the last.

| Member | Signature | Does |
| --- | --- | --- |
| `size_of` | `<T>() -> usize` | bytes one `T` occupies |
| `align_of` | `<T>() -> usize` | the alignment `T` requires |
| `is_counted` | `<T>() -> bool` | whether `T` is reference counted — decided at compile time |
| `hash` | `<T>(value: T) -> u64` | a structural hash, worked out from the layout; no bound on `T` |
| `equals` | `<T>(a: T, b: T) -> bool` | structural equality, consistent with `hash` |
| `Handle<T>` | `class` | one `T` on the heap, reference counted; `*h` reads it, `*h = v` writes it, `look()`/`touch()` borrow it |
| `of` | `<T>(value: T) -> Handle<T>` | a handle, type inferred |
| `Box<T>` | `class` | one `T` on the heap with a single owner — the same reach-through, no sharing |
| `boxed` | `<T>(value: T) -> Box<T>` | a box, type inferred |
| `Rc<T>` / `Weak<T>` | `class` | a counted value and a reference that does not keep it alive; `w.get()` answers `Rc<T>?` |
| `shared` | `<T>(value: T) -> Rc<T>` | an `Rc`, type inferred |
| `replace` | `<T>(place: &var T, value: T) -> T` | puts `value` there and hands back what was there |
| `take` | `<T>(place: &var T) -> T` | the same, leaving the type's default behind |
| `store` | `<T>(place: &var T, value: T)` | writes, destroying what was there |
| `Allocator` | `mark` | `allocate`, `deallocate`, `reallocate` |
| `allocator` | `SystemAllocator` | the process heap |
| `noBlock` | `() -> *var u8` | a block pointer to nothing |
| `isNull` | `(block: *var u8) -> bool` | tests one |
| `copy` | `(dst: *var u8, src: *u8, bytes: usize)` | moves bytes |
| `retain` / `release` | `<T>(value: T)` | **unsafe** — counting by hand, for storage the compiler cannot see |
| `slice_of` | `<T>(block: *var T, count: usize) -> [T]` | **unsafe** — `count` elements at `block` as a slice; no copy, and the block's extent is the caller's promise |
| `slice_data` | `<T>(values: [T]) -> *var T` | **unsafe** — the other direction: where a slice's elements actually are, so they can be moved out one by one rather than copied |

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

`look` and `touch` are how a structure built out of handles is walked. `*h` and `get()` both hand back a **copy** of what the handle owns — under single ownership that means cloning everything below it, which for a list is the rest of the list, at every step. A borrow reads the one that is there.

**Walking a list of handles**

```rune
import std::io
import std::mem
import std::mem::{Handle}

enum Link<T> { Empty, More(Handle<Node<T>>) }
struct Node<T> { elem: T, next: Link<T> }
struct List<T> { head: Link<T> }

extend List {
    fn push(&var self, value: T) {
        self.head = Link::More(Handle<Node<T>>(Node<T> {
            elem: value, next: mem::replace(&var self.head, Link::Empty)
        }))
    }

    /// A walk that reads: one borrow at a time, nothing copied.
    fn length(&self) -> i64 {
        var n = 0
        var cur = &self.head
        while cur is Link::More(node) { n += 1; cur = &node.look().next }
        n
    }

    /// A walk that writes: the cursor is a `&var` the whole way.
    fn doubleAll(&var self) {
        var cur = &var self.head
        loop {
            match cur {
                Link::Empty => break,
                Link::More(node) => {
                    node.touch().elem = node.look().elem * 2
                    cur = &var node.touch().next
                }
            }
        }
    }
}

fn main() -> i64 {
    var list = List<i64> { head: Link::Empty }
    list.push(1)
    list.push(2)
    io::println("length " + list.length().$str())
    list.doubleAll()
    io::println("head " + (if list.head is Link::More(n) { n.look().elem } else { 0 }).$str())
    0
}
```

| `Handle<T>` | Hands back |
| --- | --- |
| `get()` | a copy of the value — a share under counting, a clone under single ownership |
| `*h` | the same, as an operator |
| `look()` | `&T from self` — the value where it lies |
| `touch()` | `&var T from self` — the same, to write through |
| `set(v)` | replaces the value |

> [!NOTE]
> **Long chains**
>
> However long the structure is, giving it back costs no stack: a destruction reached from inside another one is queued and run after it, so a list of a million links is freed in a loop rather than a million nested calls.

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
