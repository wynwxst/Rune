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

> [!WARNING]
> **Why not by value**
>
> Windows x64 passes an aggregate in a register only at 1, 2, 4 or 8 bytes wide and passes anything else indirectly. Rather than hand C something it will misread, a `struct` parameter or result that cannot travel by value on the target is refused with an error saying so. Pointers behave identically everywhere, which is why they are the advice and not the workaround.
