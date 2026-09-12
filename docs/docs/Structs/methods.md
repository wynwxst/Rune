# Methods

**Reading, deriving, and changing in place**

```rune
import std::io

struct Rect { width: f64, height: f64 }

extend Rect {
    pub fn area(&self) -> f64 { self.width * self.height }
    pub fn scaled(&self, by: f64) -> Rect {
        Rect { width: self.width * by, height: self.height * by }
    }
    pub fn widen(&var self, by: f64) { (*self).width += by }
}

fn main() -> i64 {
    var r = Rect { width: 3.0, height: 4.0 }
    io::println(r.area().$str())
    io::println(r.scaled(2.0).area().$str())
    r.widen(1.0)
    io::println(r.area().$str())
    0
}
```

Methods live in the struct body, or in an `extend` block, or in a `bind` block. A method with `&self` borrows the receiver; one with `self` takes a copy.

**Reading and mutating methods**

```rune
import std::io

struct Rect {
    pub width: f64
    pub height: f64

    pub fn area(&self) -> f64 { self.width * self.height }
    pub fn scaled(&self, k: f64) -> Rect {
        Rect { width: self.width * k, height: self.height * k }
    }
    pub fn grow(&var self, by: f64) {
        self.width += by
        self.height += by
    }
}

fn main() -> i64 {
    let r = Rect { width: 3.0, height: 4.0 }
    io::println(r.area())
    io::println(r.scaled(2.0).area())

    var growable = Rect { width: 1.0, height: 1.0 }
    growable.grow(2.0)
    io::println(growable.area())
    0
}
```
