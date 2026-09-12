# Destruction order

A destructor runs when the last reference goes away. Within one instance the order is: the class's own `deinit`, then its own fields, then the base class does the same.

**Derived first, then base; locals in reverse**

```rune
import std::io

class Base {
    pub tag: String
    fn init(self, tag: String) { self.tag = tag }
    fn deinit(self) { io::println("  Base.deinit " + self.tag) }
}

class Derived : Base {
    fn init(self, tag: String) { super.init(tag) }
    fn deinit(self) { io::println("  Derived.deinit " + self.tag) }
}

fn main() -> i64 {
    io::println("scope opens")
    {
        let first = Derived("one")
        let second = Derived("two")
        io::println("scope closing")
    }
    io::println("scope closed")
    0
}
```
