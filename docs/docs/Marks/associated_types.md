# Associated types

A mark may declare a type each binding chooses, rather than fixing it up front. `type Item` inside the mark, `type Item = i64` inside the bind, and `Self::Item` wherever the requirement needs to name it.

**One mark, two element types**

```rune
import std::io

mark Container {
    type Item
    fn count(&self) -> i64
    fn first(&self) -> Self::Item
}

struct Bag { values: [3:i64] }
struct Names { values: [2:String] }

bind Container to Bag {
    type Item = i64
    fn count(&self) -> i64 { 3 }
    fn first(&self) -> Self::Item { self.values[0] }
}

bind Container to Names {
    type Item = String
    fn count(&self) -> i64 { 2 }
    fn first(&self) -> Self::Item { self.values[0] }
}

fn howMany<T: Container>(c: T) -> i64 { c.count() }

fn main() -> i64 {
    let b = Bag { values: [7, 8, 9] }
    let n = Names { values: ["ada", "grace"] }
    io::println(b.first().$str())
    io::println(n.first())
    io::println(howMany(b).$str() + " " + howMany(n).$str())
    0
}
```

A requirement can constrain what may be chosen. `type Item: io::Display` means every binding's `Item` has to be printable, and the bind is where that is checked.

**A choice that breaks its bound**

```rune
import std::io

struct Opaque { v: i64 }

mark Container {
    type Item: io::Display
    fn first(&self) -> Self::Item
}

struct Bad { v: Opaque }

bind Container to Bad {
    type Item = Opaque
    fn first(&self) -> Self::Item { self.v }
}

fn main() -> i64 { 0 }
```

**Forgetting to choose one**

```rune
mark Container {
    type Item
    fn first(&self) -> Self::Item
}

struct Bag { v: i64 }

bind Container to Bag {
    fn first(&self) -> i64 { self.v }
}

fn main() -> i64 { 0 }
```
