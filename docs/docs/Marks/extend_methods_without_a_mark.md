# `extend`: methods without a mark

`extend` adds inherent methods to a type you did not declare, or to a builtin. There is no mark involved.

**Extending your types and the builtins**

```rune
import std::io

struct Point { x: f64, y: f64 }

extend Point {
    fn magnitude(&self) -> f64 {
        squareRoot(self.x * self.x + self.y * self.y)
    }
    fn scaled(&self, k: f64) -> Point {
        Point { x: self.x * k, y: self.y * k }
    }
}

extend i64 {
    fn squared(&self) -> i64 { self * self }
    fn isEven(&self) -> bool { self % 2 == 0 }
}

extend String {
    fn shout(&self) -> String { self + "!" }
}

extern "C" { fn sqrt(v: f64) -> f64 }

@safe("sqrt of a sum of squares is always in libm's domain")
fn squareRoot(v: f64) -> f64 { sqrt(v) }

fn main() -> i64 {
    io::println(Point { x: 3.0, y: 4.0 }.magnitude())
    io::println(Point { x: 1.0, y: 1.0 }.scaled(3.0).x)
    io::println(7.squared())
    io::println(8.isEven())
    io::println("hey".shout())
    0
}
```

A generic type is extended the same way. Written without arguments, `extend` uses the names the type declares; written with them, it names them itself. Either way the methods belong to the type, so every instantiation has them — and two blocks may both add to one type.

**Extending a generic type**

```rune
import std::io

struct Pair<A, B> { first: A, second: B }

extend Pair {
    /// `A` and `B` are the type's own parameters.
    fn swapped(&self) -> Pair<B, A> {
        Pair<B, A> { first: self.second.$clone(), second: self.first.$clone() }
    }
}

extend<X, Y> Pair<X, Y> {
    /// The same thing, with names of this block's choosing.
    fn describe(&self) -> String where X: io::Display, Y: io::Display {
        self.first.display() + "|" + self.second.display()
    }
}

fn main() -> i64 {
    let p = Pair<i64, String> { first: 3, second: "three" }
    io::println(p.describe())
    let q = p.swapped()
    io::println(q.first + " " + q.second.$str())
    0
}
```

> [!WARNING]
> **All of them, in order**
>
> The parameters line up one for one: `extend<A, B> Pair<A, B>`. Naming a shape instead — `extend<A> Pair<A, i64>` — would be a partial specialisation, methods on some instantiations and not others, which this language does not have.
