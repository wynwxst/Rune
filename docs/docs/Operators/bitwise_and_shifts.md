# Bitwise and shifts

**Bit manipulation**

```rune
import std::io

fn main() -> i64 {
    let flags: u8 = 0b1100
    io::println(flags & 0b1010)
    io::println(flags | 0b0011)
    io::println(flags ^ 0b1111)
    io::println(~flags)
    io::println(1 << 10)
    io::println(-16 >> 2)         // signed: arithmetic shift
    io::println(240u8 >> 2)       // unsigned: logical shift
    io::println(true & false)     // `&` `|` `^` also work on bool
    io::println(true ^ true)
    0
}
```
