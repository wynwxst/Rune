# One enum extending another

`enum Derived : Base` puts the parent's variants **first**, with the numbers they had. So every `Base` is a `Derived`, and a `match` over the child covers the parent's variants by name.

**The parent's variants, and one more**

```rune
import std::io

enum Level { Low, High }
enum Extended : Level { Critical }

fn urgency(e: Extended) -> i64 { e as i64 }

fn main() -> i64 {
    let l: Level = .High
    io::println(urgency(l))          // a `Level` widens into an `Extended`

    let e: Extended = .Critical
    io::println(urgency(e))
    io::println(match e {
        .Low => "low",
        .High => "high",
        .Critical => "critical",
    })
    0
}
```

> [!NOTE]
> **Why the direction differs**
>
> A struct goes the other way: a `Derived` struct reads as its `Base` because the parent's *fields* come first, while a `Base` enum reads as its `Derived` because the parent's *variants* do. Both follow from putting the parent first.
