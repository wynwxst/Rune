# String literals

A `"..."` literal is a `String` — owned, reference counted, UTF-8 — unless the surrounding context wants a `CString`, in which case it becomes a borrowed NUL-terminated byte pointer instead. Raw strings take no escapes at all.

**String and raw-string literals**

```rune
import std::io

fn main() -> i64 {
    let plain = "ordinary text"
    let escaped = "tab\there, quote \" here"
    let unicode = "check \u{2713}"
    let raw = r"C:\not\an\escape"
    let rawQuotes = r#"he said "hello""#

    io::println(plain)
    io::println(escaped)
    io::println(unicode)
    io::println(raw)
    io::println(rawQuotes)
    0
}
```

> [!WARNING]
> **One line only**
>
> A string literal cannot span lines. Build a multi-line value by concatenating with `+`, or embed `\n`.
