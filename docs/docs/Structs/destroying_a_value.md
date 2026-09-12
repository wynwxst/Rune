# Destroying a value

A struct may declare a `deinit`, and it runs when the value it lives in is destroyed. That is what lets a value own something reference counting cannot see — a file descriptor, a lock, a handle from a C library — rather than only memory. It may be written in the body, in an `extend`, or supplied by a `bind`; all three are the same method to the compiler.

**A `deinit` in an `extend`**

```rune
import std::io

struct Handle { pub id: i64 }

extend Handle {
    // `&var self`, not `self`: taking it by value would run on a copy and
    // leave the original holding what it was meant to release.
    fn deinit(&var self) {
        if self.id >= 0 {
            io::println("closing " + self.id.$str())
            self.id = -1
        }
    }
}

fn main() -> i64 {
    io::println("before")
    {
        let h = Handle { id: 1 }
        io::println("using " + h.id.$str())
    }
    io::println("after")
    0
}
```

It runs from wherever the value is: a local at the end of its scope, a statement whose value is thrown away, and anything *containing* one — a field of a struct or a class, an element of an array, a member of a tuple.

**Reached through whatever holds it**

```rune
import std::io

struct Handle { pub id: i64 }
extend Handle { fn deinit(&self) { io::println("closing " + self.id.$str()) } }

struct Pair { pub a: Handle, pub b: Handle }
class Owner {
    h: Handle
    fn init(self) { self.h = Handle { id: 99 } }
}

fn main() -> i64 {
    { let p = Pair { a: Handle { id: 10 }, b: Handle { id: 11 } } }
    { let arr: [2:Handle] = [Handle { id: 20 }, Handle { id: 21 }] }
    { let o = Owner() }
    0
}
```
