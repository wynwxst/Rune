# Integer literals

Decimal, hexadecimal, binary and octal, with `_` allowed anywhere as a separator. A suffix pins the type; without one the literal takes the type its context wants, defaulting to `i64`.

**Every integer form**

```rune
import std::io

fn main() -> i64 {
    let decimal = 1_000_000
    let hex = 0xFF
    let binary = 0b1010_1010
    let octal = 0o755
    let sized: u8 = 200
    let suffixed = 42i32
    let unsigned = 255u8
    let wide = 9_223_372_036_854_775_807

    io::println(decimal)
    io::println(hex)
    io::println(binary)
    io::println(octal)
    io::println(sized)
    io::println(suffixed)
    io::println(unsigned)
    io::println(wide)
    0
}
```

| Suffix | Type |
| --- | --- |
| `i8` `i16` `i32` `i64` | signed integers of that width |
| `u8` `u16` `u32` `u64` | unsigned integers of that width |
| `isize` `usize` | pointer-sized: 8 bytes on a 64-bit target, 4 on a 32-bit one |
| `f32` `f64` | makes an integer literal a float |
