# The three text types

| Type | Owns | Encoding | Used for |
| --- | --- | --- | --- |
| `String` | yes, reference counted | UTF-8, NUL-terminated | everything in Rune |
| `Character` | no, it is a value | one Unicode scalar | a single character |
| `CString` | no, it borrows | bytes to a NUL | passing text to C |

**One of each, and the conversions**

```rune
import std::io

fn main() -> i64 {
    let owned: String = "a Rune String"
    let letter: Character = 'R'
    let foreign: CString = "a C string"

    io::println(owned)
    io::println(letter)
    io::println(foreign)

    // Converting between them.
    io::println(letter.$str())            // Character -> String
    io::println(foreign.$str())           // CString -> String
    io::println(owned.$cstr().$str())      // String -> CString -> String
    0
}
```
