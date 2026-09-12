# Arithmetic

**Integer and float arithmetic**

```rune
import std::io

fn main() -> i64 {
    io::println(7 + 3)
    io::println(7 - 3)
    io::println(7 * 3)
    io::println(7 / 3)        // integer division truncates
    io::println(7 % 3)
    io::println(-7 / 3)
    io::println(-7 % 3)       // remainder keeps the dividend's sign
    io::println(7.0 / 2.0)
    io::println(-2.5)
    0
}
```

> [!NOTE]
> **Division by zero**
>
> With `--safety=full` (the default) integer division and remainder check for a zero divisor and panic rather than trapping.
