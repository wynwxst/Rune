# std::mem

A fixed-size run of values on the heap, checked on every access. An array's length is part of its type, so it cannot be decided at run time; a `Buffer` can. Reading and writing both go through `[]`.

| Member | Signature | Does |
| --- | --- | --- |
| `Buffer<T>` | `(size: i64, fill: T)` | `size` slots, each holding `fill` |
| `length` | `(&self) -> i64` | how many, fixed for its life |
| `isEmpty` | `(&self) -> bool` |  |
| `holds` | `(&self, index: i64) -> bool` | is that a slot? |
| `at` | `(&self, index: i64) -> T?` | the value, or nothing — a slot of all zero bytes reads as nothing |
| `read` | `(&self, index: i64) -> T?` | the value, whatever its bytes are; only the index is checked |
| `get` | `(&self, index: i64) -> T` | the value; **aborts** if absent |
| `put` | `(&var self, index: i64, value: T) -> bool` | writes; false if out of range |
| `fill` | `(&var self, value: T)` | writes every slot |
| `b[i]` / `b[i] = v` | — | `get` and `put`, both bounds checked |

> [!WARNING]
> **Zero is a value**
>
> `at` decides "nothing has been written here" from the slot's bytes being all zero, which is right for a slot nobody has touched and wrong for one holding a value whose bytes happen to all be zero — an empty `String`, or the first variant of a counted enum that carries nothing. `read` asks only whether the index is in range, and is the form for a caller that keeps its own record of which slots are live.

**Reading and writing through `[]`**

```rune
import std::io
import std::mem

fn main() -> i64 {
    var b = mem::Buffer<i64>(size: 4, fill: 0)
    b[0] = 10
    b[1] = 20
    b[2] = b[0] + b[1]
    b[3] += 5
    io::println(b[0].$str() + " " + b[1].$str() + " " +
                b[2].$str() + " " + b[3].$str())

    // An index from outside is a question, not an assumption.
    io::println(b.at(9).hasValue().$str())
    io::println(b.holds(3).$str())
    0
}
```

**Past the end aborts**

```rune
import std::io
import std::mem

fn main() -> i64 {
    var b = mem::Buffer<i64>(size: 2, fill: 0)
    io::println("about to read past the end")
    io::println(b[5].$str())
    0
}
```

> [!NOTE]
> **Why a fill**
>
> The `fill` is required rather than optional. A buffer with uninitialised slots would hand out whatever the allocator left there — the one thing a safe container must not do — so every slot holds a real value from the moment it exists. For a reference-counted `T` the buffer owns one reference per slot and gives them all back.
