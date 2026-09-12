# Conditional bindings

A generic binding may carry a `where` clause. The binding then applies only to the instantiations that satisfy it.

**`where` decides whether a binding applies**

```rune
import std::io

mark Show { fn show(&self) -> String }

struct Wrapper<T> { value: T }

bind Show to i64 { fn show(&self) -> String { self.$str() } }

bind<T> Show to Wrapper<T> where T: Show {
    fn show(&self) -> String { "Wrapper(" + self.value.show() + ")" }
}

struct Opaque { n: i64 }

fn main() -> i64 {
    io::println(Wrapper<i64> { value: 7 }.show())

    // Wrapper<Opaque> exists, but is not Show, because Opaque is not:
    let hidden = Wrapper<Opaque> { value: Opaque { n: 1 } }
    io::println(hidden.value.n)
    0
}
```
