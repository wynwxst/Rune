# Warnings

Warnings follow the same shape in yellow. `-Werror` promotes them, `-w` silences them, and `--error-limit <n>` stops the compiler after *n* errors so a single mistake does not fill your terminal.

**A warning does not stop the build**

```rune
import std::io

fn main() -> i64 {
    let unused = 99
    io::println("hello")
    0
}
```
