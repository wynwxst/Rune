# Structs

A Rune `struct` of scalars has the layout C gives the same declaration, so the two describe one piece of memory. Cross it by **pointer**: what the ABIs agree on is what a pointer to a struct means, not when a struct itself travels in a register. `&value as *T` is how a Rune value's address becomes a C pointer.

**A struct, by pointer**

```rune
import std::io

struct Point { x: f64, y: f64 }

extern "C" {
    fn memcpy(dst: *var Point, src: *Point, size: u64) -> *var Point
    fn memcmp(a: *Point, b: *Point, size: u64) -> i32
}

fn main() -> i64 {
    let origin = Point { x: 0.0, y: 0.0 }
    let corner = Point { x: 3.0, y: 4.0 }
    var copy = Point { x: 0.0, y: 0.0 }
    let size = 16 as u64          // two f64, as C lays them out

    unsafe { memcpy(&var copy as *var Point, &corner as *Point, size) }
    io::println("copied: " + copy.x.$str() + ", " + copy.y.$str())

    let same = unsafe { memcmp(&corner as *Point, &copy as *Point, size) } == 0
    let differs = unsafe { memcmp(&corner as *Point, &origin as *Point, size) } != 0
    io::println("same: " + same.$str() + ", differs: " + differs.$str())
    0
}
```

> [!NOTE]
> **Structs by value**
>
> A struct, tuple or array passed to C by value, or returned from it, travels the way the platform's C compiler passes one: in registers, packed into them, or in memory, by the same rules the C++ interop follows. So does a call through a `@cfunction`, and a Rune function handed to C as one takes its arguments that way too. A pointer is still the cheaper way to hand over anything large.

> [!NOTE]
> **Being called with a struct**
>
> A Rune function `#export`ed to C takes its arguments the C way too: where C passes a signature differently from Rune — a struct by value — the exported symbol is an adapter that receives them as C sends them and makes the Rune call.
