# Declaring a mark

A method with no body is a requirement. A method with a body is a default that a binding may accept or replace.

**Requirements and defaults**

```rune
import std::io

mark Show {
    fn show(&self) -> String                        // required
    fn shout(&self) -> String { self.show() + "!" } // default
}

struct Point { x: i64, y: i64 }

bind Show to Point {
    fn show(&self) -> String { "(" + self.x.$str() + "," + self.y.$str() + ")" }
}

struct Loud { text: String }

bind Show to Loud {
    fn show(&self) -> String { self.text.$clone() }
    fn shout(&self) -> String { self.text.$repeat(3) }   // replaces the default
}

fn main() -> i64 {
    io::println(Point { x: 1, y: 2 }.show())
    io::println(Point { x: 1, y: 2 }.shout())
    io::println(Loud { text: "ha" }.shout())
    0
}
```

> [!NOTE]
> **How defaults resolve**
>
> Each binding gets its **own copy** of the mark's defaults, so inside a default `Self` is the concrete type and a call to a sibling method reaches that type's implementation.
