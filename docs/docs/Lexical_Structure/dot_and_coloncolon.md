# `.` and `::`

Two separators, and the rule is about *what* is on the left, never about what is on the right. `::` walks a path through things the compiler knows by name — modules, types, marks, enums. `.` reaches into a value you have in your hand.

| Written | Left side | Means |
| --- | --- | --- |
| `std::io` | a module | a module inside another |
| `io::println(x)` | a module | a function in it |
| `math::PI` | a module | a global in it |
| `Shape::Circle` | an enum | one of its variants |
| `Sheep::new("d")` | a type | a method with no `self` |
| `Animal::new("d")` | a mark | a static requirement, for the type in context |
| `Pair<i64, String>` | a type | generic arguments, no separator |
| `total::<i64>(xs)` | a function | generic arguments at a call |
| — | — | — |
| `point.x` | a value | a field |
| `point.length()` | a value | a method |
| `text.$length()` | a value | a method the *compiler* provides |
| `pair.0` | a value | a tuple element |
| `value::Mark.name()` | a value, then a mark | the method that mark binds for this type |

*The last row is the only place the two meet: `::` picks the mark, `.` then reaches the method through it.*

**Both, side by side**

```rune
import std::io
import std::math

enum Shape { Circle(f64), Square(f64) }

struct Point { x: f64, y: f64 }

extend Point {
    pub fn distance(&self) -> f64 {
        math::squareRoot(self.x * self.x + self.y * self.y)
    }
}

fn main() -> i64 {
    // `::` through modules and types.
    io::println(math::PI.$str())
    let s = Shape::Circle(2.0)

    // `.` into a value.
    let p = Point { x: 3.0, y: 4.0 }
    io::println(p.x.$str())
    io::println(p.distance().$str())

    // And `$` for what the compiler provides, on any value.
    io::println("hello".$length().$str())
    0
}
```

> [!NOTE]
> **Resolution order**
>
> A path is resolved left to right, so `a::b::c` needs every step to name something. If the first step is a local variable, you are writing the mark-qualified form and the rest must name a mark.
