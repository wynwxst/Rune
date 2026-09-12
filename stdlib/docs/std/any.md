# std::any

`Any` holds one value of any type and remembers which. It gives the value
back only as the type it really is: the answer comes from a descriptor in the
value's own object header, so it is one pointer comparison and it is right
across separately compiled packages.

## Putting things in, and asking what they are

```rune
import std::io

struct Point { pub x: f64, pub y: f64 }

fn describe(v: Any) -> String {
    if v is i64 { return "int " + v.expect::<i64>().$str() }
    if v is String { return "text " + v.expect::<String>() }
    "other " + v.typeName()
}

fn main() -> i64 {
    let boxed: Any = 41
    io::println(boxed.typeName())
    io::println(boxed.holds::<i64>())
    io::println(boxed.get::<f64>().isNil())         // the wrong type is nil, not an abort
    io::println(boxed.get::<i64>() ?? 0)
    io::println(describe(7))
    io::println(describe("seven"))
    io::println(describe(Point { x: 1.0, y: 2.0 }))
    let things: [3:Any] = [1, "two", 3.0]
    for t in things { io::println(t.typeName()) }
    0
}
```

## What it is for

A heterogeneous collection, or a value that has to be carried through code
that does not know its type — with the question asked again on the far side.

```rune
import std::io
import std::collections::vector

fn main() -> i64 {
    var bag = vector::Vector<Any>()
    bag.push(1)
    bag.push("text")
    bag.push(2.5)
    var ints = 0
    var total = 0
    for item in bag {
        if item is i64 { ints += 1; total += item.expect::<i64>() }
    }
    io::println(ints)
    io::println(total)
    0
}
```
