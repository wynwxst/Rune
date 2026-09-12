# Running something

`spawn` takes a top-level `fn` and one argument, and hands back a handle to join. The entry is a `fn` rather than a closure because a closure carries what it captured, and sharing that is the thing this module exists to prevent — so what the thread needs, it is given.

**Fork and join**

```rune
import std::io
import std::thread

/// Sums `from..to`. The bounds arrive as a tuple, which crosses as happily
/// as a number would: what matters is what it is made of, and these are
/// two `i64`.
fn sumRange(bounds: (i64, i64)) -> i64 {
    var total = 0
    var i = bounds.0
    while i <= bounds.1 { total += i; i += 1 }
    total
}

fn shout(name: String) -> String { name + "!" }

fn main() -> i64 {
    // One sum, split down the middle and done at the same time. The halves
    // add up to what doing it in one go would have given.
    var lower = thread::spawn(sumRange, (1, 500000))
    var upper = thread::spawn(sumRange, (500001, 1000000))
    println!("in two: {}", lower.join() + upper.join())
    println!("in one: {}", sumRange((1, 1000000)))

    // A String crosses in, and another comes back.
    var greeting = thread::spawn(shout, "hello" + " world")
    println!("{}", greeting.join())

    println!("this machine runs {} at once", thread::hardwareThreads())
    0
}
```

> [!NOTE]
> **Dropping a handle**
>
> A handle that goes out of scope without being joined waits for its thread anyway. Detaching instead would leave the task's memory with nobody to free it, and a leak is a worse default than a wait.
