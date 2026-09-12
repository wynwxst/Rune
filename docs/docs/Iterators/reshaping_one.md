# Reshaping one

Every `Iterator` carries the adaptors, and so does every `Sequence` — which asks for a cursor first. Each wraps an iterator in another iterator.

| Written | What it yields |
| --- | --- |
| `.map(f)` | every value with `f` applied to it |
| `.filter(keep)` | only the values `keep` says yes to |
| `.zip(other)` | pairs, ending as soon as either side does |
| `.take_while(keep)` | the values up to the first `false`, and no further |
| `.skip_while(drop)` | everything from the first value `drop` says no to onwards |
| `.take(n)` | at most `n` values |
| `.skip(n)` | everything after the first `n` |
| `.enumerate()` | each value paired with its position |
| `.chain(other)` | this one's values, then the other's |
| `.as_iter()` | a cursor — `iterate` on a `Sequence`, and itself on an `Iterator`, so a chain reads the same either way |

*The adaptors*

**A chain over a container**

```rune
import std::io
import std::collections::vector

struct Person { pub name: String, pub age: i64 }

fn main() -> i64 {
    var people = vector::Vector<Person>()
    people.push(Person { name: "ada", age: 36 })
    people.push(Person { name: "tom", age: 11 })
    people.push(Person { name: "grace", age: 45 })

    for name in people.filter(||(p: Person) -> bool { p.age >= 18 })
                      .map(||(p: Person) -> String { p.name }) {
        io::println(name)
    }
    0
}
```

> [!NOTE]
> **Note**
>
> Nothing is computed on the way in. A value moves through the chain only when the loop at the end asks for it, so a chain over a million elements allocates nothing and walks the source once.
