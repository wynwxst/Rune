# Getting it back out

| Written | Means |
| --- | --- |
| `value.typeName()` | the fully qualified name of the type inside |
| `value is T` | true when the value inside is a `T` |
| `value.holds::<T>()` | the same test, for a `T` with no bare-name spelling |
| `value.get::<T>()` | `T?` — the value, or `nil` when it is something else |
| `value.expect::<T>()` | `T` — the value, aborting when it is something else |

*Everything an `Any` will tell you*

`get` is the one to reach for. The test and the read happen together, so there is no way to read the value as a type it does not have.

**The five questions**

```rune
import std::io

fn main() -> i64 {
    let boxed: Any = 41i64

    io::println(boxed.typeName())
    io::println(boxed is i64)
    io::println(boxed.holds::<f64>())
    io::println(boxed.get::<i64>() ?? -1)
    io::println(boxed.get::<f64>() ?? -1.0)
    io::println(boxed.expect::<i64>() + 1)
    0
}
```

> [!NOTE]
> **Note**
>
> `is T` and `holds::<T>()` ask the same question. `is` reads better but only takes a name, because that is where a pattern would have gone. A type with no bare-name spelling — `[3:f64]`, `(i64, String)`, `dyn Shape` — goes through `holds` and `get`.

**Asking, then reading**

```rune
import std::io

struct Point { pub x: f64, pub y: f64 }

fn render(v: Any) -> String {
    if v is i64 { return "i64    " + v.expect::<i64>().$str() }
    if v is String { return "String " + v.expect::<String>() }
    if v is Point {
        let p = v.expect::<Point>()
        return "Point  " + p.x.$str() + ", " + p.y.$str()
    }
    match v.get::<(i64, bool)>() {
        Option::Some(pair) => "tuple  " + pair.0.$str() + ", " + pair.1.$str(),
        Option::None => "?      " + v.typeName(),
    }
}

fn main() -> i64 {
    io::println(render(7i64))
    io::println(render("text"))
    io::println(render(Point { x: 1.0, y: 2.0 }))
    io::println(render((3i64, true)))
    io::println(render(2.5))
    0
}
```
