# Matching through a borrow

`match` looks through borrows, so a `&self` method can match on `self` directly without dereferencing first.

**No explicit dereference needed**

```rune
import std::io

enum Shape { Circle(f64), Square(f64) }

mark Area { fn area(&self) -> f64 }

bind Area to Shape {
    fn area(&self) -> f64 {
        match self {                       // `self` is `&Shape` here
            Shape::Circle(r) => 3.14159 * r * r,
            Shape::Square(s) => s * s,
        }
    }
}

fn label(s: &Shape) -> String {
    match s {                              // and here it is a parameter
        Shape::Circle(r) => "circle",
        Shape::Square(s) => "square",
    }
}

fn main() -> i64 {
    let c = Shape::Circle(2.0)
    io::println(label(&c) + " " + c.area().$str())
    0
}
```
