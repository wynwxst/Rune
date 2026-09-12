# std::fmt

What a format string expands into. `println!("{} {:.2}", a, b)` is sugar over
these, and they are useful on their own when a number has to be laid out by
hand.

## Numbers with a shape

```rune
import std::io
import std::fmt

fn main() -> i64 {
    io::println(fmt::fixed(3.14159, 2))
    io::println(fmt::radix(255, 16, false, true))     // 0xff
    io::println(fmt::radix(5, 2, false, false))       // 101
    io::println(fmt::plus("7"))                       // +7
    io::println(fmt::pad("7", 4, '>', '0'))           // 0007
    io::println(fmt::pad("ab", 6, '<', '.') + "|")
    io::println(fmt::pad("ab", 6, '^', '-'))
    io::println(fmt::show(true))
    0
}
```

## Through a format string

```rune
import std::io

fn main() -> i64 {
    let name = "ada"
    let score = 9.5
    println!("{} scored {:.1}", name, score)
    println!("[{:>6}] [{:<6}] [{:^6}]", "r", "l", "c")
    println!("{:04}", 42)
    let line = format!("{} + {} = {}", 1, 2, 3)
    io::println(line)
    0
}
```
