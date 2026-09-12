# `if` as a statement

**`if` / `elif` / `else`**

```rune
import std::io

fn classify(n: i64) -> String {
    if n < 0 {
        "negative"
    } elif n == 0 {
        "zero"
    } elif n < 10 {
        "small"
    } else {
        "large"
    }
}

fn main() -> i64 {
    io::println(classify(-3))
    io::println(classify(0))
    io::println(classify(4))
    io::println(classify(400))
    0
}
```

> [!NOTE]
> **Spelling**
>
> `elif` is one word. `else if` also parses, and means the same thing.
