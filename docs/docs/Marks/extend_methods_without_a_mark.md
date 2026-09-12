# `extend`: methods without a mark

`extend` adds inherent methods to a type you did not declare, or to a builtin. There is no mark involved.

**Extending your types and the builtins**

```rune
import std::io

struct Point { x: f64, y: f64 }

extend Point {
    fn magnitude(&self) -> f64 {
        squareRoot(self.x * self.x + self.y * self.y)
    }
    fn scaled(&self, k: f64) -> Point {
        Point { x: self.x * k, y: self.y * k }
    }
}

extend i64 {
    fn squared(&self) -> i64 { self * self }
    fn isEven(&self) -> bool { self % 2 == 0 }
}

extend String {
    fn shout(&self) -> String { self + "!" }
}

extern "C" { fn sqrt(v: f64) -> f64 }

@safe("sqrt of a sum of squares is always in libm's domain")
fn squareRoot(v: f64) -> f64 { sqrt(v) }

fn main() -> i64 {
    io::println(Point { x: 3.0, y: 4.0 }.magnitude())
    io::println(Point { x: 1.0, y: 1.0 }.scaled(3.0).x)
    io::println(7.squared())
    io::println(8.isEven())
    io::println("hey".shout())
    0
}
```
