# Explicit discriminants

A variant with no payload may be given a number. Such an enum converts to an integer with `as`; the numbering continues from the last value given.

**Enums with wire values**

```rune
import std::io

enum Status {
    Ok = 200,
    Created,            // 201
    NotFound = 404,
    Teapot = 418,
}

fn main() -> i64 {
    io::println(Status::Ok as i64)
    io::println(Status::Created as i64)
    io::println(Status::NotFound as i64)
    io::println(Status::Teapot as i64)
    io::println(Status::Ok == Status::Ok)
    0
}
```

A value may also be integer arithmetic on numbers and on the variants declared before it — `+ - * / %`, the shifts, `& | ^` and `~` — which is how flags and C's aliases are written. Two variants with one value are one tag: a `match` takes the first arm that names it. A call or a variable is not a constant, and is E0400.

**Flags, and an alias**

```rune
import std::io

enum Style {
    Titled = 1 << 0,
    Closable = 1 << 1,
    Resizable = 1 << 3,
    Standard = Titled | Closable | Resizable,
}

enum Family { Iso = 7, Osi = Iso, Ecma }    // Osi is Iso; Ecma is 8

fn main() -> i64 {
    io::println(Style::Standard as i64)
    io::println(Family::Osi as i64)
    io::println(Family::Ecma as i64)
    0
}
```
