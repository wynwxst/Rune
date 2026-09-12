# Default arguments

A parameter may have a default. Defaults are evaluated at the call site, once per call that omits the argument.

**Trailing defaults**

```rune
import std::io

fn indent(text: String, width: i64 = 2, fill: String = " ") -> String {
    fill.$repeat(width) + text
}

fn main() -> i64 {
    io::println(indent("a"))
    io::println(indent("b", 6))
    io::println(indent("c", 4, "."))
    0
}
```
