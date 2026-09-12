# Asking directly

The bound is what belongs in a signature, because it explains itself when it fails. `std::reflect` asks the same question where code wants to take a different path rather than refuse.

**Send, as a question**

```rune
import std::reflect

struct Pair { a: i64, b: String }

class Counter {
    var n: i64
    fn init(self, n: i64) { self.n = n }
}

struct Holder { c: Counter }

fn main() -> i64 {
    println!("{} {} {}", reflect::isSend<i64>(), reflect::isSend<String>(),
             reflect::isSend<Pair>())
    // A class, and anything that holds one.
    println!("{} {}", reflect::isSend<Counter>(), reflect::isSend<Holder>())
    0
}
```
