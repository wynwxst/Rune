# Tuples

**Building, indexing, destructuring, mutating**

```rune
import std::io

fn divide(a: i64, b: i64) -> (i64, i64) {
    (a / b, a % b)
}

fn main() -> i64 {
    let pair: (i64, String) = (1, "one")
    let triple = (1, 2.5, true)

    io::println(pair.0.$str() + " " + pair.1)
    io::println(triple.1)
    io::println(triple.2)

    let (quotient, remainder) = divide(17, 5)
    io::println(quotient.$str() + " r " + remainder.$str())

    // Nested, and reached by chained indices.
    let nested = ((1, 2), (3, 4))
    io::println(nested.0.1.$str() + " " + nested.1.0.$str())

    var mutable = (0, "start")
    mutable.0 = 9
    mutable.1 = "changed"
    io::println(mutable.0.$str() + " " + mutable.1)
    0
}
```

> [!NOTE]
> **Growing one**
>
> An array's length is part of its type, so it cannot grow. When you need one that can, `std::collections::vector` has `Vector<T>` — same indexing, plus `push` and `pop`.

> [!NOTE]
> **One element, and none**
>
> `(T)` in a type is just `T` in parentheses, not a one-element tuple. `()` is the unit type — the result of a function with no `->`. It carries no information, so it cannot be bound to a name.
