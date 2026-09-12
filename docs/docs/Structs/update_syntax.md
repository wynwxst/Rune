# Update syntax

`..other` fills in every field the literal does not mention, taking them from another value of the same type.

**Deriving one value from another**

```rune
import std::io

struct Options {
    pub width: i64
    pub height: i64
    pub label: String
}

fn main() -> i64 {
    let base = Options { width: 80, height: 24, label: "base" }
    let wide = Options { width: 120, ..base }
    let renamed = Options { label: "renamed", ..base }

    io::println(wide.width.$str() + "x" + wide.height.$str() + " " + wide.label)
    io::println(renamed.width.$str() + "x" + renamed.height.$str() + " " + renamed.label)
    0
}
```

**Every field needs a value**

```rune
struct Point { pub x: f64, pub y: f64 }

fn main() -> i64 {
    let p = Point { x: 1.0 }
    0
}
```
