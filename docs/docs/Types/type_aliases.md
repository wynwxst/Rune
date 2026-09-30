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

An alias may take parameters of its own, and then it stands for a different type at each use: `Row<i64>` is a slice of integers and `Row<String>` a slice of strings, exactly as if each had been written out.

**An alias with parameters of its own**

```rune
import std::io
import std::collections::vector

type Row<T> = [T]
type Pairing<A, B> = (A, B)
type Table<T> = vector::Vector<T>

fn total(values: Row<i64>) -> i64 {
    var t = 0
    for v in values { t += v }
    t
}

fn joined(words: Row<String>) -> String {
    var s = ""
    for w in words { s += w }
    s
}

fn main() -> i64 {
    let ns: [3:i64] = [1, 2, 3]
    let ws: [2:String] = ["a", "b"]
    io::println(total(ns).$str())
    io::println(joined(ws))

    let both: Pairing<i64, String> = (7, "seven")
    io::println(both.1 + " " + both.0.$str())

    // Built through the alias, as the type it stands for.
    var t = Table<i64>()
    t.push(9)
    io::println(t.at(0).or(0).$str())
    0
}
```

> [!NOTE]
> **One for one**
>
> The arguments have to match what the alias declares: `Row` takes one, `Pairing` two. An alias that declares none takes none.
