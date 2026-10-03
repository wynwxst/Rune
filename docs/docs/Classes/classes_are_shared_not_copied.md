# Classes are shared, not copied

**Reference semantics versus value semantics**

```rune
import std::io

class Tally { pub n: i64
    fn init(self) { self.n = 0 } }

struct Count { pub n: i64 }

fn main() -> i64 {
    let a = Tally()
    // Under reference counting (`--memory arc`, as here) `b` is the same
    // instance; under single ownership, the default, `a` is handed to `b`.
    let b = a                  // the same instance, not a copy
    b.n = 99
    io::println("class:  " + a.n.$str() + " " + b.n.$str())

    var c = Count { n: 1 }
    var d = c                  // a copy
    d.n = 99
    io::println("struct: " + c.n.$str() + " " + d.n.$str())
    0
}
```
