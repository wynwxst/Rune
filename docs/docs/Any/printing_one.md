# Printing one

Nothing prints an `Any` by default, because nothing can know how. A program that wants one to print says so.

**`bind io::Display to Any`**

```rune
import std::io

struct Point { pub x: f64 }

bind io::Display to Any {
    fn display(&self) -> String {
        if self is i64 { return self.expect::<i64>().$str() }
        if self is f64 { return self.expect::<f64>().$str() }
        if self is String { return self.expect::<String>() }
        "<" + self.typeName() + ">"
    }
}

fn main() -> i64 {
    let row: [4:Any] = [1i64, 2.5, "three", Point { x: 4.0 }]
    for v in row { io::println(v) }
    0
}
```
