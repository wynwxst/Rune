# Strings across the boundary

`CString` is a borrowed pointer to NUL-terminated bytes; a string literal already is one. A Rune `String` is a counted buffer, so hand C its `.$cstr()` view — valid for the duration of the call.

**Passing a String to C**

```rune
import std::io

extern "C" {
    fn strlen(text: CString) -> u64
    fn atoi(text: CString) -> i32
}

fn main() -> i64 {
    let built = "12" + "34"
    io::println("length: " + strlen(built.$cstr()).$str())
    io::println("parsed: " + atoi(built.$cstr()).$str())
    0
}
```

> [!WARNING]
> **Lifetime**
>
> The pointer from `.$cstr()` belongs to the string. Do not store it — if the `String` is released, the bytes go with it.
