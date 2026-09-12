# Float literals

**Every float form**

```rune
import std::io

fn main() -> i64 {
    let plain = 1.5
    let exponent = 1e-3
    let both = 6.022e23
    let single: f32 = 0.25
    let suffixed = 2.5f32
    let fromInt = 3f64

    io::println(plain)
    io::println(exponent)
    io::println(both)
    io::println(single)
    io::println(suffixed)
    io::println(fromInt)
    0
}
```

> [!NOTE]
> **Why `1..5` works**
>
> A `.` only begins a fraction when a digit follows it, which is what keeps `1..5` a range and `pair.0.1` two field accesses.
