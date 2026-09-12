# Scope and shadowing

**Shadowing changes the type as it goes**

```rune
import std::io

fn main() -> i64 {
    let raw = "  42  "
    // Each `let` makes a new binding; the old one is untouched, and the type
    // may change on the way.
    let raw = raw.$substring(2, 4)
    let raw = raw.$toInt().or(0)
    io::println(raw.$str())
    0
}
```

Bindings belong to the block that declares them. A nested block may reuse a name without disturbing the outer one.

**An inner name shadows an outer one**

```rune
import std::io

fn main() -> i64 {
    let value = "outer"
    {
        let value = "inner"
        io::println(value)
    }
    io::println(value)
    0
}
```
