# What the answer is based on

An `Any` is one pointer: the value itself when it is a class, and a reference-counted box around it otherwise. Either way the object header in front of it carries a descriptor naming the type, and every question above is answered from that descriptor — from the value, never from a promise the program made about it. Each type gets one descriptor across the whole program, so the comparison is a pointer comparison.

A class is matched the way `is` matches classes everywhere: a `Dog` also holds as an `Animal`. Everything else is matched exactly — a `u32` does not hold as an `i64`, even though one converts to the other.

**A class keeps its hierarchy; nothing else widens**

```rune
import std::io

class Animal {
    pub name: String
    fn init(self, name: String) { self.name = name }
    pub fn speak(&self) -> String { "..." }
}
class Dog : Animal {
    fn init(self, name: String) { super.init(name) }
    pub fn speak(&self) -> String { "woof" }
}

fn main() -> i64 {
    let d: Any = Dog("rex")
    io::println(d is Dog)
    io::println(d is Animal)
    io::println(d.expect::<Animal>().speak())

    let a: Any = Animal("generic")
    io::println(a is Dog)

    let n: Any = 7i64
    io::println(n.holds::<u32>())
    0
}
```
