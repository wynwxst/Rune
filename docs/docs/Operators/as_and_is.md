# `as` and `is`

`as` converts. `is` asks a class-typed or `dyn`-typed value what it actually is at run time.

**Testing a dynamic type**

```rune
import std::io

class Shape { fn init(self) {} }
class Circle : Shape { fn init(self) { super.init() } }
class Square : Shape { fn init(self) { super.init() } }

fn describe(s: Shape) -> String {
    if s is Circle { return "circle" }
    if s is Square { return "square" }
    "some shape"
}

fn main() -> i64 {
    io::println(describe(Circle()))
    io::println(describe(Square()))
    io::println(describe(Shape()))
    0
}
```
