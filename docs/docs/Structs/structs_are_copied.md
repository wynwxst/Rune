# Structs are copied

**Assignment copies a struct**

```rune
import std::io

struct Counter { pub n: i64 }

fn main() -> i64 {
    var a = Counter { n: 1 }
    var b = a               // a copy, not a second name for the same value
    b.n = 99
    io::println(a.n.$str() + " " + b.n.$str())
    0
}
```

> [!NOTE]
> **Structs with references**
>
> A struct holding a `String` or a class still copies, but the copy shares the referenced object and takes a reference of its own. See **Memory**.
