# Asking what a value really is

**`is` and a heterogeneous array of subclasses**

```rune
import std::io

class Shape { fn init(self) {} pub fn sides(&self) -> i64 { 0 } }
class Triangle : Shape { fn init(self) { super.init() }
    pub fn sides(&self) -> i64 { 3 } }
class Square : Shape { fn init(self) { super.init() }
    pub fn sides(&self) -> i64 { 4 } }

fn label(s: &Shape) -> String {
    if s is Triangle { return "triangle" }
    if s is Square { return "square" }
    "shape"
}

fn main() -> i64 {
    let shapes: [3:Shape] = [Triangle(), Square(), Shape()]
    var out = ""
    for s in shapes {
        out += label(s) + "/" + s.sides().$str() + " "
    }
    io::println(out)
    0
}
```
