# Opaque results: `some Mark`

A generic parameter with a mark bound is chosen by the *caller*. `dyn Mark` is chosen at run time and boxed. `some Mark` is the third spelling: the *body* decides the concrete type, every call yields that same type, and callers see only the mark. It costs nothing at run time — there is no box and no table, the value is the concrete one — which is what makes it the right spelling for an iterator chain whose nested wrapper type nobody wants to write out.

**The body picks the type; callers see the mark**

```rune
import std::io
import std::iter

mark Shape { fn area(&self) -> f64 }
struct Sq { pub s: f64 }
struct Circle { pub r: f64 }
bind Shape to Sq { fn area(&self) -> f64 { self.s * self.s } }
bind Shape to Circle { fn area(&self) -> f64 { 3.0 * self.r * self.r } }

fn make(side: f64) -> some Shape { Sq { s: side } }

/// The point of it: a chain whose type nobody has to write out.
fn evens(limit: i64) -> some iter::Iterator {
    iter::counting(0).filter(||(n: i64) -> bool { n % 2 == 0 }).take(limit)
}

fn total<S: Shape>(a: S, b: S) -> f64 { a.area() + b.area() }

struct Box { pub w: f64 }
extend Box {
    pub fn shape(&self) -> some Shape { Sq { s: self.w } }
}

fn main() -> i64 {
    let s = make(3.0)
    io::println(s.area())
    io::println(total(make(1.0), make(2.0)))   // one type per function
    for n in evens(3) { io::println(n) }
    let d: dyn Shape = make(4.0)               // still boxes when asked
    io::println(d.area())
    io::println(Box { w: 2.0 }.shape().area())
    0
}
```

What is behind a `some` is the function's own business. A caller cannot name a field of it, treat it as the concrete type, or take it apart — only the mark's methods, and whatever that mark is itself bound to.

**The hidden type is not the caller's to name**

```rune
mark Shape { fn area(&self) -> f64 }
struct Sq { pub s: f64 }
bind Shape to Sq { fn area(&self) -> f64 { self.s * self.s } }

fn make(side: f64) -> some Shape { Sq { s: side } }

fn main() -> i64 {
    let s = make(3.0)
    let side = s.s
    0
}
```

A `some` stands for exactly one type, fixed by the first value the body returns. A second, different one is an error — unlike `dyn Mark`, which is how a function returns one of several.

**One function, one hidden type**

```rune
mark Shape { fn area(&self) -> f64 }
struct Sq { pub s: f64 }
struct Circle { pub r: f64 }
bind Shape to Sq { fn area(&self) -> f64 { self.s * self.s } }
bind Shape to Circle { fn area(&self) -> f64 { 3.0 * self.r * self.r } }

fn either(c: bool) -> some Shape {
    if c { return Sq { s: 1.0 } }
    Circle { r: 1.0 }
}

fn main() -> i64 { 0 }
```

|  | `dyn Mark` | `some Mark` | `<T: Mark>` |
| --- | --- | --- | --- |
| Who picks the type | the caller, at run time | the body, once | the caller, at compile time |
| Callers see | the mark | the mark | the type parameter |
| Representation | value plus table, two words | the value itself | the value itself |
| Mixed collections | yes | no — one type per function | no |
| Cost | a box, unless it is a class | none | none |

*Three ways to talk about a mark*

> [!NOTE]
> **The word is not reserved**
>
> `some` is not a keyword. It is recognised only where a type is expected and a mark name follows, so `option::some` and a local called `some` keep meaning what they did.
