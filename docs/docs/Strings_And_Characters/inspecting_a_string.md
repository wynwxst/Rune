# Inspecting a string

**Length, search, and reading characters**

```rune
import std::io

fn main() -> i64 {
    let text = "héllo wörld"

    io::println(text.$length())        // bytes
    io::println(text.$charCount())     // Unicode scalars
    io::println(text.$isEmpty())
    io::println("".$isEmpty())

    io::println(text.$substring(0, 6))
    io::println(text.$find("wörld"))   // byte offset, or -1
    io::println(text.$find("absent"))

    io::println(text.$at(1))           // the second character: é
    io::println(text[1])               // the same, as a subscript
    io::println(text.$byteAt(0))       // the raw byte
    io::println(text.$hash() != 0)
    0
}
```

| Method | Result | Notes |
| --- | --- | --- |
| `length()` | `i64` | bytes, not characters |
| `charCount()` | `i64` | Unicode scalars |
| `isEmpty()` | `bool` |  |
| `at(i)` | `Character` | the i-th character, counted from the start; `text[i]` is the same read |
| `byteAt(i)` | `u8` | one raw byte |
| `substring(a, b)` | `String` | bytes `a` up to `b` |
| `find(needle)` | `i64` | byte offset, or `-1` |
| `repeat(n)` | `String` |  |
| `toInt()` | `i64?` | `None` unless the whole string parses |
| `toFloat()` | `f64?` | likewise |
| `cstr()` | `CString` | borrows this String's bytes |
| `hash()` | `u64` | FNV-1a over the bytes |
| `str()` | `String` | itself; every type has it |

*Every String method*
