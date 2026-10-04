# #Convention("C")

`#Convention("C")` on a struct or an enum promises C's layout, and the compiler holds it to that: fields in the order written, with C's padding and alignment; a payload-free enum is a C `int` holding its values; an enum with payloads is a tag (`int32_t`, the variant's index or value) followed by a union aligned for its widest member. Passed by value — to C, from C, through a `@cfunction` or an `#export` — it travels as the platform's C compiler would pass the same declaration.

**Layouts C shares**

```rune
import std::io
import std::mem

#Convention("C")
struct Packet { kind: u8, length: u32, checksum: u16, stamp: i64 }

#Convention("C")
enum Level { Debug = 10, Info = 20, Warn = 30 }

#Convention("C")
enum Reading { Celsius(f64), Raw(i64), Missing }

fn main() -> i64 {
    // 1 padded to 4, 4, 2 padded to 8, 8: what C's sizeof says
    io::println(mem::size_of<Packet>())
    io::println(mem::size_of<Level>())
    io::println(mem::size_of<Reading>())   // a 4-byte tag, then 8 aligned
    0
}
```

Every part has to be something C has: numbers, `bool`, `Character` (a `uint32_t`), raw pointers, `CString`, `@cfunction`s, arrays of those, and other `#Convention("C")` types. A `String`, a class, an `Option`, a borrow or a `dyn` is Rune's and is refused (E0544), as is a class, a generic type or a convention other than `"C"` (E0543).
