# Putting a value in

Anything with a run-time representation converts to `Any`, and the conversion is implicit — there is nothing to write.

**Five unrelated types in one array**

```rune
import std::io

struct Point { pub x: f64, pub y: f64 }

fn main() -> i64 {
    let values: [5:Any] = [
        7i64,
        1.5,
        "a string",
        Point { x: 3.0, y: 4.0 },
        (1i64, true),
    ]
    for v in values { io::println(v.typeName()) }
    0
}
```

> [!NOTE]
> **Note**
>
> `typeName` gives the *qualified* name, so two types called `Point` in two modules never read as one. Every sample on this page is compiled as its own module, which is where the `any_2::` prefix comes from; in your program it is your package's name.
