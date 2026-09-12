# The full set

| Category | Written | Notes |
| --- | --- | --- |
| Signed integers | `i8` `i16` `i32` `i64` `isize` | `int` is an alias for `i64` |
| Unsigned integers | `u8` `u16` `u32` `u64` `usize` | `uint` aliases `u64`; `byte` and `Byte` both alias `u8` |
| Floating point | `f32` `f64` | `float` aliases `f32`, `double` aliases `f64` |
| Boolean | `bool` | no implicit conversion to or from integers |
| Character | `Character` | exactly one Unicode scalar |
| Foreign text | `CString` | a borrowed NUL-terminated byte pointer |
| Text | `String` | owned, reference counted, UTF-8 |
| Array | `[5:i64]` | fixed length, a constant expression |
| Slice | `[i64]` | a pointer and a length |
| Tuple | `(i64, String)` | `()` is the unit type |
| Optional | `i64?` | sugar for `Option<i64>` |
| Borrow | `&T` `&var T` | checked, non-owning |
| Raw pointer | `*T` `*var T` | unchecked; every use is unsafe |
| Function | `@function(i64) -> bool` | see below for both spellings |
| Nominal | `struct` `enum` `class` `mark` | declared types |
| Mark object | `dyn Show` | a value plus a dispatch table |
| Opaque result | `some Show` | the body fixes the type; callers see only the mark |
| Dynamic | `Any` | one value of any type, asked at run time what it is |
| Never | `Never` | the type of an expression that does not return |

*Every type Rune has*

**One of everything**

```rune
import std::io

struct Pair { left: i64, right: i64 }
enum Colour { Red, Green }
class Handle { pub id: i64
    fn init(self, id: i64) { self.id = id } }
mark Named { fn name(&self) -> String }
bind Named to Pair { fn name(&self) -> String { "pair" } }

fn main() -> i64 {
    let signed: i64 = -5
    let unsigned: u32 = 5
    let real: f64 = 2.5
    let flag: bool = true
    let letter: Character = 'R'
    let foreign: CString = "for C"
    let text: String = "owned"
    let array: [3:i64] = [1, 2, 3]
    let slice: [i64] = array[0..2]
    let tuple: (i64, String) = (1, "one")
    let optional: i64? = 7
    let pair = Pair { left: 1, right: 2 }
    let borrow: &Pair = &pair
    let colour: Colour = Colour::Green
    let handle: Handle = Handle(9)
    let marked: dyn Named = pair

    io::println(signed.$str() + " " + unsigned.$str() + " " + real.$str())
    io::println(flag.$str() + " " + letter.$str() + " " + text)
    io::println(array.$length().$str() + " " + slice.$length().$str())
    io::println(tuple.1 + " " + optional.or(0).$str())
    io::println(borrow.left.$str() + " " + handle.id.$str())
    io::println(marked.name() + " " + (colour as i64).$str())
    io::println(foreign.$str())
    0
}
```
