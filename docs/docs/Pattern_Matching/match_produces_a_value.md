# `match` produces a value

Like `if`, a `match` used for its value needs every arm to agree on a type. An arm body may be a block.

**An arm can be a block**

```rune
import std::io

enum Level { Debug, Info, Warning, Error }

fn severity(l: Level) -> i64 {
    match l {
        Level::Debug => 10,
        Level::Info => 20,
        Level::Warning => {
            let base = 30
            base + 0
        }
        Level::Error => 40,
    }
}

fn main() -> i64 {
    io::println(severity(Level::Debug))
    io::println(severity(Level::Warning))
    io::println(severity(Level::Error))
    0
}
```
