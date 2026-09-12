# Type aliases

**A name for a shape you keep repeating**

```rune
import std::io

type Celsius = f64
type Reading = (Celsius, String)

fn describe(r: Reading) -> String { r.1 + " at " + r.0.$str() }

fn main() -> i64 {
    let now: Reading = (21.5, "kitchen")
    io::println(describe(now))
    0
}
```

`type` gives a name to any type. It is a pure alias: the two names are interchangeable everywhere.

```rune
import std::result

type Pair = (i64, i64)
type Grid = [9:i64]
type Callback = @function(i64) -> bool
type Parsed = result::Result<i64, String>
type Maybe = i64?

fn first(p: Pair) -> i64 { p.0 }
fn size(g: Grid) -> i64 { g.$length() }
```
