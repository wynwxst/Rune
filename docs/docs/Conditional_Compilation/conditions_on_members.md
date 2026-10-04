# Conditions on members

A field, a method, an enum variant or a foreign declaration may carry one of its own. Fields that survive are renumbered, so a struct is laid out as though the others were never written, and a `match` is exhaustive over the variants that exist.

**A type with a different shape per platform**

```rune
import std::io

struct Handle {
    pub id: i64,
    #Config(os == "windows")
    pub winHandle: i64,
    #Config(family == "unix")
    pub fd: i64,
}

extend Handle {
    #Config(family == "unix")
    pub fn describe(&self) -> String { "fd " + self.fd.$str() }

    #Config(os == "windows")
    pub fn describe(&self) -> String { "handle " + self.winHandle.$str() }
}

fn main() -> i64 {
    io::println(Handle { id: 1, fd: 3 }.describe())
    0
}
```
