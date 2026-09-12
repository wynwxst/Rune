# Options of reference types

An `Option` of a class or a `String` is an ordinary enum, so it participates in reference counting like anything else. A `None` holds nothing to release.

**The class is released when the Option is**

```rune
import std::io

class Session {
    pub id: i64
    fn init(self, id: i64) { self.id = id }
    fn deinit(self) { io::println("  session " + self.id.$str() + " closed") }
}

fn find(id: i64) -> Session? {
    if id > 0 { return Session(id) }
    nil
}

fn main() -> i64 {
    match find(7) {
        Some(s) => io::println("opened " + s.id.$str()),
        None => io::println("not found"),
    }
    io::println("---")
    match find(-1) {
        Some(s) => io::println("opened " + s.id.$str()),
        None => io::println("not found"),
    }
    0
}
```
