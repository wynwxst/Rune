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
