# `Self`: requirements that build

A requirement with no `self` parameter is a static one, and `Self` in its signature stands for whichever type implements it. That is how a mark describes a constructor.

**A mark that constructs**

```rune
import std::io

struct Sheep { naked: bool, name: String }

mark Animal {
    fn new(name: String) -> Self      // static: no `self`

    fn name(&self) -> String
    fn noise(&self) -> String
    fn speak(&self) { io::println(self.name() + " says " + self.noise()) }
}

bind Animal to Sheep {
    fn new(name: String) -> Self { Sheep { naked: false, name: name } }
    fn name(&self) -> String { self.name }
    fn noise(&self) -> String { if self.naked { "baaaa!" } else { "baaaa?" } }
}

fn main() -> i64 {
    // The annotation is what picks the implementation.
    let dolly: Sheep = Animal::new("dolly")
    dolly.speak()
    0
}
```

A static requirement has no receiver, so nothing about the call says which implementation to run. Rune takes it from the type the result flows into — an annotation, a parameter, or a return type. With nothing to go on, it asks.

**Nothing to infer from**

```rune
struct Sheep { naked: bool, name: String }

mark Animal {
    fn new(name: String) -> Self
    fn noise(&self) -> String
}

bind Animal to Sheep {
    fn new(name: String) -> Self { Sheep { naked: false, name: name } }
    fn noise(&self) -> String { "baaaa" }
}

fn main() -> i64 {
    let mystery = Animal::new("dolly")
    0
}
```

`Self` works as a parameter type too, which is what lets a mark describe an operation over its own type. Combined with a generic bound, one function then serves every implementation — including the builtin ones.

**`Self` on both sides**

```rune
import std::io

mark Monoid {
    fn zero() -> Self
    fn combine(&self, other: Self) -> Self
}

struct V2 { x: i64, y: i64 }

bind Monoid to V2 {
    fn zero() -> Self { V2 { x: 0, y: 0 } }
    fn combine(&self, other: Self) -> Self {
        V2 { x: self.x + other.x, y: self.y + other.y }
    }
}

bind Monoid to i64 {
    fn zero() -> Self { 0 }
    fn combine(&self, other: Self) -> Self { self + other }
}

bind Monoid to String {
    fn zero() -> Self { "" }
    fn combine(&self, other: Self) -> Self { self + other }
}

fn total<T: Monoid>(values: [T]) -> T {
    var acc: T = Monoid::zero()
    for v in values { acc = acc.combine(v) }
    acc
}

fn main() -> i64 {
    let ns: [4:i64] = [1, 2, 3, 4]
    let words: [3:String] = ["a", "b", "c"]
    let vs: [2:V2] = [V2 { x: 1, y: 2 }, V2 { x: 10, y: 20 }]
    io::println(total(ns).$str())
    io::println(total(words))
    let sum = total(vs)
    io::println(sum.x.$str() + "," + sum.y.$str())
    0
}
```

An implementation has to match the requirement once `Self` is read as the implementing type.

**A `Self` that does not line up**

```rune
struct Metre { v: i64 }
struct Foot { v: i64 }

mark Zeroed { fn zero() -> Self }

bind Zeroed to Metre {
    fn zero() -> Foot { Foot { v: 0 } }
}

fn main() -> i64 { 0 }
```

> [!WARNING]
> **Not through `dyn`**
>
> A static requirement is not reachable through a `dyn Mark` value. A mark object carries one implementation chosen at run time, and a call with no receiver has nothing to choose from — call it on a concrete type instead.
