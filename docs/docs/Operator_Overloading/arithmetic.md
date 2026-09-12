# Arithmetic

**`+ - * -a` and `+=`**

```rune
import std::io

struct Vec2 { pub x: f64, pub y: f64 }

bind operator::add to Vec2 {
    fn add(&self, rhs: &Vec2) -> Vec2 {
        Vec2 { x: self.x + rhs.x, y: self.y + rhs.y }
    }
}
bind operator::sub to Vec2 {
    fn sub(&self, rhs: &Vec2) -> Vec2 {
        Vec2 { x: self.x - rhs.x, y: self.y - rhs.y }
    }
}
bind operator::mul to Vec2 {
    fn mul(&self, k: f64) -> Vec2 {
        Vec2 { x: self.x * k, y: self.y * k }
    }
}
bind operator::neg to Vec2 {
    fn neg(&self) -> Vec2 { Vec2 { x: 0.0 - self.x, y: 0.0 - self.y } }
}

fn show(v: Vec2) -> String { "(" + v.x.$str() + "," + v.y.$str() + ")" }

fn main() -> i64 {
    let a = Vec2 { x: 1.0, y: 2.0 }
    let b = Vec2 { x: 0.5, y: 0.5 }

    io::println(show(a + b))
    io::println(show(a - b))
    io::println(show(a * 3.0))
    io::println(show(-a))

    // A compound assignment uses the same overload.
    var acc = Vec2 { x: 0.0, y: 0.0 }
    acc += a
    acc += b
    io::println(show(acc))
    0
}
```
