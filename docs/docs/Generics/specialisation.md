# Specialisation

More than one `bind` may apply to one type. The narrower one wins, and which is narrower does not depend on the order they were written in.

| Narrower | Than |
| --- | --- |
| a target written out — `Wrapper<i64>` | one with parameters — `Wrapper<T>` |
| more of the shape pinned down — `Boxed<Boxed<T>>` | less — `Boxed<T>` |
| more asked of the parameters — `where T: Tag` | less — no clause at all |

*What makes one binding narrower than another, in order*

**Three ways to be narrower**

```rune
import std::io

struct Wrapper<T> { value: T }
struct Boxed<T> { inner: T }
mark Show { fn show(&self) -> String }
mark Tag  { fn tag(&self) -> String }
bind Tag to i64 { fn tag(&self) -> String { "t" } }

// Written *after* the general case, and still the one that applies.
bind<T> Show to Wrapper<T> { fn show(&self) -> String { "some wrapper" } }
bind Show to Wrapper<i64>  { fn show(&self) -> String { "a wrapped i64" } }

mark Kind { fn kind(&self) -> String }
bind<T> Kind to Wrapper<T> { fn kind(&self) -> String { "any wrapper" } }
bind<T> Kind to Wrapper<T> where T: Tag { fn kind(&self) -> String { "a tagged one" } }

mark Depth { fn depth(&self) -> String }
bind<T> Depth to Boxed<T> { fn depth(&self) -> String { "plain" } }
bind<T> Depth to Boxed<Boxed<T>> { fn depth(&self) -> String { "nested" } }

fn main() -> i64 {
    io::println((Wrapper<i64> { value: 1 }).show())
    io::println((Wrapper<f64> { value: 1.0 }).show())
    io::println((Wrapper<i64> { value: 1 }).kind())
    io::println((Wrapper<f64> { value: 1.0 }).kind())
    io::println((Boxed<i64> { inner: 1 }).depth())
    io::println((Boxed<Boxed<i64>> { inner: Boxed<i64> { inner: 1 } }).depth())
    0
}
```

Two bindings of one mark that are equally specific are reported rather than decided by the order they were reached in.

**Neither one is narrower**

```rune
import std::io

struct Wrapper<T> { value: T }
mark Show { fn show(&self) -> String }
mark Tag  { fn tag(&self) -> String }
mark Mood { fn mood(&self) -> String }
bind Tag to i64  { fn tag(&self) -> String { "t" } }
bind Mood to i64 { fn mood(&self) -> String { "m" } }

bind<T> Show to Wrapper<T> where T: Tag  { fn show(&self) -> String { "tagged" } }
bind<T> Show to Wrapper<T> where T: Mood { fn show(&self) -> String { "other" } }

fn main() -> i64 {
    io::println((Wrapper<i64> { value: 1 }).show())
    0
}
```

> [!NOTE]
> **What is not a clash**
>
> Clauses that cannot both hold are not a clash — one type takes the first and another the second. Neither are two *different* marks that happen to name a method the same way: `Iterator` and `Sequence` both supply `map`, and which one answers is settled by the mark asked for.
