# When it will not compile

The interesting part is what `spawn` refuses. The bound is an ordinary one, so the error arrives at the call that tried it and says what to do instead:

**A class cannot cross**

```rune
import std::thread

class Counter {
    var n: i64
    fn init(self, n: i64) { self.n = n }
}

fn bump(c: Counter) -> i64 { c.n += 1; c.n }

fn main() -> i64 {
    var h = thread::spawn(bump, Counter(0))
    h.join()
    0
}
```

`Send` cannot be bound by hand either. It is an answer, not a promise, and letting one be written would put the whole guarantee behind a line nobody has to justify.
