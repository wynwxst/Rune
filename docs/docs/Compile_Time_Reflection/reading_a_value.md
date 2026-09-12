# Reading a value

`describe` renders a value from its layout. It asks nothing of the type: no mark to bind and no `display` to write. It is the reading half of the same idea as `mem::hash` and `mem::equals` — the compiler already knows what a value is made of, so the program may as well be able to say it.

**A value, rendered from its layout**

```rune
import std::reflect
import std::io

struct Point { x: i64, y: f64 }
struct Wrap { name: String, at: Point, flags: [2:bool] }
enum Shape { Dot, Circle(f64), Rect { w: i64, h: i64 } }

struct Named { n: i64 }
bind io::Display to Named {
    fn display(&self) -> String { "Named#" + self.n.$str() }
}
struct Holder { inner: Named, other: i64 }

fn main() -> i64 {
    println!("{} {} {}", reflect::describe(42), reflect::describe("hi"),
             reflect::describe('z'))
    println!("{}", reflect::describe(Point { x: 1, y: 2.5 }))
    println!("{} {}", reflect::describe((1, "a")), reflect::describe([1, 2, 3]))
    println!("{}", reflect::describe(Wrap { name: "w",
                                            at: Point { x: 0, y: 0.0 },
                                            flags: [true, false] }))

    // Every enum shape, including the ones the language builds for you.
    let found: i64? = 5
    println!("{} {} {} {}",
             reflect::describe(Shape::Dot),
             reflect::describe(Shape::Circle(1.5)),
             reflect::describe(Shape::Rect { w: 3, h: 4 }),
             reflect::describe(found))

    // A type that decided how it prints keeps that decision, nested too.
    println!("{}", reflect::describe(Holder { inner: Named { n: 7 },
                                              other: 1 }))
    0
}
```

| Shape | Rendered as |
| --- | --- |
| struct | `Point { x: 1, y: 2.5 }` |
| tuple | `(1, "a")` |
| array | `[1, 2, 3]` |
| enum | `Dot`, `Circle(1.5)`, `Rect { w: 3, h: 4 }` |
| `String` | quoted, so an empty one is visible |
| class | `Node@0x...` — a reference is what it *is*, and following it could run forever around a cycle |
| binds `io::Display` | whatever `display` returns |

> [!NOTE]
> **It defers to `Display`**
>
> `describe` is for looking at values: logs, tests, a quick dump. A type that wants to control how it prints binds `io::Display`, and `describe` uses it wherever it finds one — including on a field inside something else.
