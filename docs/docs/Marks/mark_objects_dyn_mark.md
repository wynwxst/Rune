# Mark objects: `dyn Mark`

A generic parameter with a mark bound is resolved at compile time. When the concrete type is only known at run time — a collection of several unrelated types — use `dyn Mark`, which pairs the value with a dispatch table.

**Structs, enums and classes in one collection**

```rune
import std::io

mark Shape {
    fn area(&self) -> f64
    fn name(&self) -> String
    fn summary(&self) -> String { self.name() + " " + self.area().$str() }
}

struct Circle { r: f64 }
enum Tile { Small, Large }
class Canvas { pub side: f64
    fn init(self, side: f64) { self.side = side } }

bind Shape to Circle {
    fn area(&self) -> f64 { 3.14159265 * self.r * self.r }
    fn name(&self) -> String { "circle" }
}
bind Shape to Tile {
    fn area(&self) -> f64 { match self { Tile::Small => 1.0, Tile::Large => 4.0 } }
    fn name(&self) -> String { "tile" }
}
bind Shape to Canvas {
    fn area(&self) -> f64 { self.side * self.side }
    fn name(&self) -> String { "canvas" }
}

// Runtime dispatch: any Shape at all.
fn describe(s: dyn Shape) -> String { s.summary() }

// Compile-time dispatch: monomorphised per type, and inlinable.
fn describeStatic<T: Shape>(s: T) -> String { s.summary() }

fn total(shapes: [dyn Shape]) -> f64 {
    var sum = 0.0
    for s in shapes { sum += s.area() }
    sum
}

fn main() -> i64 {
    io::println(describe(Circle { r: 1.0 }))
    io::println(describe(Tile::Large))
    io::println(describe(Canvas(3.0)))
    io::println(describeStatic(Circle { r: 1.0 }))

    let mixed: [3:dyn Shape] = [Circle { r: 2.0 }, Tile::Small, Canvas(2.0)]
    io::println(total(mixed))
    0
}
```

|  | `dyn Mark` | `<T: Mark>` |
| --- | --- | --- |
| Dispatch | through a table, at run time | resolved at compile time |
| Code size | one copy | one copy per type used |
| Inlining | no | yes |
| Mixed collections | yes | no |
| Representation | value plus table, two words | the value itself |

*Choosing between them*

**Boxing a type that is not bound**

```rune
mark Show { fn show(&self) -> String }
struct Point { x: i64 }

fn render(s: dyn Show) -> String { s.show() }

fn main() -> i64 {
    render(Point { x: 1 })
    0
}
```
