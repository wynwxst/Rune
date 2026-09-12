# Conversions you have to ask for

**Explicit casts with `as`**

```rune
import std::io

fn main() -> i64 {
    let big: i64 = 300
    let narrowed = big as u8            // truncates, so it must be explicit
    let rounded = 2.9 as i64            // toward zero
    let widened = 7 as f64
    let code = 'R' as i64               // a Character's scalar value
    let letter = 82 as Character
    let flag = 1 as bool
    let number = true as i64
    let text = "borrowed" as String     // CString to String

    io::println(narrowed)
    io::println(rounded)
    io::println(widened)
    io::println(code)
    io::println(letter)
    io::println(flag)
    io::println(number)
    io::println(text)
    0
}
```

> [!WARNING]
> **Unsafe casts**
>
> Pointer casts, and any cast between a pointer and an integer, are unsafe operations. See **Safety**.

**A cast that has no meaning**

```rune
fn main() -> i64 {
    let text = "not a number"
    let wrong = text as i64
    0
}
```
