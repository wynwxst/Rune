# Binding to builtin types

A mark may be bound to any type at all, including `i64` and `String`. That is exactly how `io::Display` makes `println` work for everything. On a builtin, `&self` passes the value, since there is nothing to look inside.

**Marks on `i64`, `f64`, `String` and `bool`**

```rune
import std::io

mark Doubled {
    fn doubled(&self) -> String
}

bind Doubled to i64 {
    fn doubled(&self) -> String { (self * 2).$str() }
}

bind Doubled to f64 {
    fn doubled(&self) -> String { (self * 2.0).$str() }
}

bind Doubled to String {
    fn doubled(&self) -> String { self + self }
}

bind Doubled to bool {
    fn doubled(&self) -> String { self.$str() + self.$str() }
}

fn show<T: Doubled>(v: T) -> String { v.doubled() }

fn main() -> i64 {
    io::println(show(21))
    io::println(show(1.25))
    io::println(show("ab"))
    io::println(show(true))
    0
}
```
