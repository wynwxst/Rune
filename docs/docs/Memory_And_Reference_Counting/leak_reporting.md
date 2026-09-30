# Leak reporting

At `--safety full` — the default — the runtime counts live objects at exit and reports anything left over on stderr. It is not a garbage collector; it is a check that the counts balanced.

**A strong cycle is reported at exit**

```rune
import std::io

class Ring {
    next: Ring?
    fn init(self) { self.next = nil }
}

fn main() -> i64 {
    let a = Ring()
    let b = Ring()
    a.next = b
    b.next = a      // strong both ways: neither can ever reach zero
    io::println("built a cycle")
    0
}
```

> [!NOTE]
> **Checking it yourself**
>
> `process::liveObjectCount()` is the same counter the leak report uses. It is a legitimate way to assert in a test that a data structure released everything it should.
