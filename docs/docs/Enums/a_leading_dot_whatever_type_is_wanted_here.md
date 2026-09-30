# A leading dot: whatever type is wanted here

Where the type is already known — an annotation, an argument, a `match` on a value — saying it again adds nothing. A leading `.` names something on that type: a variant, a static method, anything the type owns.

**`.Name`, wherever the type is already said**

```rune
import std::io

enum Colour { Red, Green, Blue }
enum Shape { Circle(f64), Rect(f64, f64) }

struct Duration { ms: i64 }
extend Duration {
    fn seconds(n: i64) -> Duration { Duration { ms: n * 1000 } }
    fn zero() -> Duration { Duration { ms: 0 } }
}

fn paint(c: Colour) -> i64 { c as i64 }
fn wait(d: Duration) -> i64 { d.ms }

fn area(s: Shape) -> f64 {
    match s { .Circle(r) => r * r * 3.0, .Rect(w, h) => w * h }
}

fn main() -> i64 {
    let c: Colour = .Blue               // a variant
    let d: Duration = .seconds(5)       // a static method
    let list: [3:Colour] = [.Red, .Green, .Blue]

    io::println(paint(c))
    io::println(wait(.zero()))
    io::println(area(.Rect(2.0, 3.0)))
    io::println(paint(list[1]))
    0
}
```

It works in a pattern too, where it also says the name is a variant rather than a new binding — so `.Purple` on a `Colour` is a mistake, where a bare `Purple` would quietly have bound the value.

**No such variant**

```rune
enum Colour { Red, Green, Blue }

fn name(c: Colour) -> String {
    match c { .Red => "red", .Green => "green", .Purple => "?" }
}

fn main() -> i64 { 0 }
```

> [!NOTE]
> **Why not `::Name`**
>
> Only `.`, never `::`. A leading `::` reads as a path with an empty first segment, which several languages spell that way; keeping it free costs nothing, and one spelling is easier to read than two.
